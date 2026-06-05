//
// Created by Anlk on 2026/6/6.
// PlayerDialog 实现（自原 PlayerPage 抽离）。
//

#include "../include/PlayerDialog.h"
#include "../include/VideoGLWidget.h"
#include "../include/VideoCard.h"

#include "../../ffmpeg/include/MediaPlayer.h"
#include "../../network/include/VideoAPI.h"

#include <QAction>
#include <QCloseEvent>
#include <QCursor>
#include <QDateTime>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace Mixed::Player {

    PlayerDialog::PlayerDialog(QWidget *parent) : QDialog(parent) {
        setObjectName("playerDialog");
        setWindowTitle(QStringLiteral("视频播放"));
        // 非模态：用户可在播放时返回列表继续浏览。
        setModal(false);
        setMinimumSize(900, 560);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);

        // 顶部信息区（标题/统计 + 作者），保持紧凑高度。
        layout->addWidget(buildTopInfoArea(), /*stretch=*/0);

        // 中间播放区：左侧播放器(7) + 右侧相关视频(3)，占据剩余全部高度。
        m_playLayout = new QHBoxLayout();
        m_playLayout->setSpacing(12);

        m_playLayout->addWidget(buildPlayerArea(), 7);
        m_playLayout->addWidget(buildRelatedPanel(), 3);

        layout->addLayout(m_playLayout, /*stretch=*/1);

        // 控制条自动隐藏定时器（鼠标离开 3s 后隐藏）。
        m_hideTimer = new QTimer(this);
        m_hideTimer->setSingleShot(true);
        m_hideTimer->setInterval(3000);
        connect(m_hideTimer, &QTimer::timeout, this, [this] {
            const QPoint local = m_playerContainer->mapFromGlobal(QCursor::pos());
            if (m_playerContainer->rect().contains(local)) {
                scheduleHideControls();
                return;
            }
            m_controlsVisible = false;
            m_volumePopupVisible = false;
            layoutOverlay();
        });

        connect(m_playPauseButton, &QPushButton::clicked, this, [this] {
            if (!m_player) return;
            if (m_player->isPaused()) {
                m_player->resume();
                m_playPauseButton->setText(QStringLiteral("⏸"));
            } else {
                m_player->pause();
                m_playPauseButton->setText(QStringLiteral("▶"));
            }
        });

        connect(m_prevButton, &QPushButton::clicked, this, [this] {
            if (m_videos.empty()) return;
            int n = static_cast<int>(m_videos.size());
            playIndex((m_current - 1 + n) % n);
        });

        connect(m_nextButton, &QPushButton::clicked, this, [this] {
            if (m_videos.empty()) return;
            playIndex((m_current + 1) % static_cast<int>(m_videos.size()));
        });

        connect(m_qualityButton, &QPushButton::clicked, this, &PlayerDialog::showQualityMenu);
        connect(m_speedButton, &QPushButton::clicked, this, &PlayerDialog::showSpeedMenu);
        connect(m_volumeButton, &QPushButton::clicked, this, [this] {
            if (!m_volumePopup) return;
            m_volumePopupVisible = !m_volumePopupVisible;
            m_controlsVisible = true;
            layoutOverlay();
            scheduleHideControls();
        });
        connect(m_subtitleButton, &QPushButton::clicked, this, &PlayerDialog::toggleSubtitles);
        connect(m_fullscreenButton, &QPushButton::clicked, this, &PlayerDialog::toggleFullscreen);

        m_player = new MediaPlayer(this);
        connect(m_player, &MediaPlayer::frameReady, this, [this](const VideoFrame &f) {
            if (f.valid() && f.height > 0) {
                const double aspect = static_cast<double>(f.width) / f.height;
                if (qAbs(aspect - m_videoAspect) > 0.001) {
                    m_videoAspect = aspect;
                    layoutOverlay();
                }
            }
            m_video->setFrame(f);
        });
        connect(m_player, &MediaPlayer::cleared, this, [this] {
            m_video->clearFrame();
        });
        connect(m_player, &MediaPlayer::errorOccurred, this, [this](const QString &msg) {
            m_status->setText(QStringLiteral("播放错误: ") + msg);
            m_status->show();
        });
        connect(m_player, &MediaPlayer::positionChanged, this, &PlayerDialog::onPositionChanged);
        connect(m_player, &MediaPlayer::subtitleChanged, this, [this](const QString &text) {
            if (m_subtitlesEnabled && !text.isEmpty()) {
                m_subtitleLabel->setText(text);
                m_subtitleLabel->show();
                m_subtitleLabel->raise();
            } else {
                m_subtitleLabel->hide();
            }
        });

        m_network = new QNetworkAccessManager(this);
        m_api = new API::VideoAPI(m_network);
    }

    PlayerDialog::~PlayerDialog() {
        if (m_player) m_player->stop();
        delete m_api;
    }

    void PlayerDialog::closeEvent(QCloseEvent *event) {
        // 关闭时停止播放，释放资源（对话框可被复用，下次 setPlaylist 重新开始）。
        if (m_isFullscreen) exitFullscreen();
        if (m_player) m_player->stop();
        if (m_playPauseButton) m_playPauseButton->setText(QStringLiteral("▶"));
        QDialog::closeEvent(event);
    }

    void PlayerDialog::setPlaylist(const std::vector<Models::VideoData> &videos, int startIndex) {
        m_videos = videos;
        const bool hasList = !m_videos.empty();
        m_prevButton->setEnabled(hasList);
        m_nextButton->setEnabled(hasList);
        if (!hasList) return;
        if (startIndex < 0) startIndex = 0;
        if (startIndex >= static_cast<int>(m_videos.size())) {
            startIndex = static_cast<int>(m_videos.size()) - 1;
        }
        layoutOverlay();
        playIndex(startIndex);
    }

    QWidget *PlayerDialog::buildTopInfoArea() {
        auto *area = new QWidget(this);
        auto *layout = new QHBoxLayout(area);
        layout->setContentsMargins(16, 14, 16, 4);

        // 左侧：标题和统计信息（播放量 · 发布时间）。
        auto *leftLayout = new QVBoxLayout();
        m_titleLabel = new QLabel(QStringLiteral("视频标题"), area);
        m_titleLabel->setObjectName("videoTitle");
        m_titleLabel->setWordWrap(true);
        leftLayout->addWidget(m_titleLabel);

        m_statsLabel = new QLabel(QStringLiteral("播放量 · 发布时间"), area);
        m_statsLabel->setObjectName("videoStats");
        leftLayout->addWidget(m_statsLabel);
        leftLayout->addStretch();

        layout->addLayout(leftLayout, 7);

        // 右侧：作者信息及简介。
        auto *rightLayout = new QVBoxLayout();
        m_authorLabel = new QLabel(QStringLiteral("作者名称"), area);
        m_authorLabel->setObjectName("authorName");
        rightLayout->addWidget(m_authorLabel);

        m_authorDescLabel = new QLabel(QStringLiteral("作者简介"), area);
        m_authorDescLabel->setObjectName("authorDesc");
        m_authorDescLabel->setWordWrap(true);
        rightLayout->addWidget(m_authorDescLabel);
        rightLayout->addStretch();

        layout->addLayout(rightLayout, 3);
        return area;
    }

    QWidget *PlayerDialog::buildPlayerArea() {
        m_playerContainer = new QWidget(this);
        m_playerContainer->setObjectName("playerContainer");
        m_playerContainer->setMinimumSize(480, 300);
        m_playerContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_playerContainer->setMouseTracking(true);
        m_playerContainer->setFocusPolicy(Qt::StrongFocus);

        m_video = new VideoGLWidget(m_playerContainer);
        m_video->setMouseTracking(true);

        m_controlBar = buildControlBar();
        buildVolumePopup();

        m_subtitleLabel = new QLabel(m_playerContainer);
        m_subtitleLabel->setObjectName("playerSubtitle");
        m_subtitleLabel->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        m_subtitleLabel->setWordWrap(true);
        m_subtitleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_subtitleLabel->hide();

        m_playerContainer->installEventFilter(this);
        m_video->installEventFilter(this);
        m_controlBar->installEventFilter(this);
        return m_playerContainer;
    }

    QWidget *PlayerDialog::buildRelatedPanel() {
        m_relatedPanel = new QWidget(this);
        m_relatedPanel->setObjectName("relatedVideos");
        m_relatedPanel->setMinimumWidth(240);

        auto *outer = new QVBoxLayout(m_relatedPanel);
        outer->setContentsMargins(12, 12, 12, 12);
        outer->setSpacing(10);

        auto *title = new QLabel(QStringLiteral("相关视频"), m_relatedPanel);
        title->setObjectName("relatedTitle");
        outer->addWidget(title);

        auto *scroll = new QScrollArea(m_relatedPanel);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setFrameShape(QFrame::NoFrame);
        outer->addWidget(scroll, 1);

        m_relatedContent = new QWidget(scroll);
        m_relatedContent->setObjectName("relatedContent");
        scroll->setWidget(m_relatedContent);

        m_relatedLayout = new QVBoxLayout(m_relatedContent);
        m_relatedLayout->setContentsMargins(0, 0, 0, 0);
        m_relatedLayout->setSpacing(12);
        m_relatedLayout->addStretch();

        return m_relatedPanel;
    }

    QWidget *PlayerDialog::buildControlBar() {
        auto *bar = new QWidget(m_playerContainer);
        bar->setObjectName("playerControlBar");

        auto *layout = new QHBoxLayout(bar);
        layout->setContentsMargins(14, 0, 14, 0);
        layout->setSpacing(10);

        auto makeIconButton = [&](const QString &glyph, const QString &tip,
                                  bool enabled = true) {
            auto *btn = new QPushButton(glyph, bar);
            btn->setObjectName("playerIconButton");
            btn->setToolTip(tip);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setEnabled(enabled);
            btn->setFocusPolicy(Qt::NoFocus);
            return btn;
        };
        auto makeTextButton = [&](const QString &text, const QString &tip) {
            auto *btn = new QPushButton(text, bar);
            btn->setObjectName("playerTextButton");
            btn->setToolTip(tip);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setFocusPolicy(Qt::NoFocus);
            return btn;
        };

        // 左侧：上一个 / 播放暂停 / 下一个 + 时间。
        m_prevButton = makeIconButton(QStringLiteral("⏮"), QStringLiteral("上一个"), false);
        layout->addWidget(m_prevButton);

        m_playPauseButton = makeIconButton(QStringLiteral("▶"), QStringLiteral("播放/暂停"), false);
        layout->addWidget(m_playPauseButton);

        m_nextButton = makeIconButton(QStringLiteral("⏭"), QStringLiteral("下一个"), false);
        layout->addWidget(m_nextButton);

        m_timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), bar);
        m_timeLabel->setObjectName("playerTime");
        layout->addWidget(m_timeLabel);

        layout->addStretch(1);

        // 右侧：画质 / 倍速 / 字幕 / 音量 / 全屏。
        m_qualityButton = makeTextButton(QStringLiteral("自动"), QStringLiteral("画质切换"));
        layout->addWidget(m_qualityButton);

        m_speedButton = makeTextButton(QStringLiteral("倍速"), QStringLiteral("播放倍速"));
        layout->addWidget(m_speedButton);

        m_subtitleButton = makeTextButton(QStringLiteral("字幕"), QStringLiteral("字幕开关"));
        m_subtitleButton->setProperty("active", true);
        layout->addWidget(m_subtitleButton);

        m_volumeButton = makeIconButton(QStringLiteral("🔊"), QStringLiteral("音量"));
        layout->addWidget(m_volumeButton);

        m_fullscreenButton = makeIconButton(QStringLiteral("⛶"), QStringLiteral("全屏"));
        layout->addWidget(m_fullscreenButton);

        m_status = new QLabel(QStringLiteral(""), m_playerContainer);
        m_status->setObjectName("playerStatus");
        m_status->setAttribute(Qt::WA_TransparentForMouseEvents);

        return bar;
    }

    void PlayerDialog::buildVolumePopup() {
        m_volumePopup = new QWidget(m_playerContainer);
        m_volumePopup->setObjectName("volumePopup");
        m_volumePopup->setFixedSize(40, 130);

        auto *vl = new QVBoxLayout(m_volumePopup);
        vl->setContentsMargins(6, 10, 6, 10);

        m_volumeSlider = new QSlider(Qt::Vertical, m_volumePopup);
        m_volumeSlider->setObjectName("volumeSlider");
        m_volumeSlider->setRange(0, 100);
        m_volumeSlider->setValue(100);
        vl->addWidget(m_volumeSlider, 0, Qt::AlignHCenter);

        connect(m_volumeSlider, &QSlider::valueChanged, this, &PlayerDialog::onVolumeChanged);

        m_volumePopup->installEventFilter(this);
        m_volumePopup->move(0, 5000);
    }

    QRect PlayerDialog::videoRect() const {
        const QRect r = m_playerContainer->rect();
        const double cw = r.width();
        const double ch = r.height();
        if (cw <= 0 || ch <= 0) return r;

        const double aspect = m_videoAspect > 0 ? m_videoAspect : 16.0 / 9.0;
        double w = cw;
        double h = cw / aspect;
        if (h > ch) {
            h = ch;
            w = ch * aspect;
        }
        const int x = static_cast<int>((cw - w) / 2.0);
        const int y = static_cast<int>((ch - h) / 2.0);
        return QRect(x, y, static_cast<int>(w), static_cast<int>(h));
    }

    void PlayerDialog::layoutOverlay() {
        if (!m_playerContainer || !m_video) return;
        const QRect vr = videoRect();
        m_video->setGeometry(vr);

        const int barHeight = 48;
        const int offY = m_playerContainer->height() + 80;

        if (m_subtitleLabel) {
            const int subH = 64;
            m_subtitleLabel->setGeometry(vr.x() + 24,
                                         vr.bottom() - barHeight - subH - 4,
                                         vr.width() - 48, subH);
            m_subtitleLabel->raise();
        }

        if (m_status) {
            const int y = m_controlsVisible ? (vr.bottom() - barHeight - 24) : offY;
            m_status->setGeometry(vr.x() + 14, y, vr.width() - 28, 20);
            m_status->raise();
        }

        if (m_controlBar) {
            const int y = m_controlsVisible ? (vr.bottom() - barHeight + 1) : offY;
            m_controlBar->setGeometry(vr.x(), y, vr.width(), barHeight);
            m_controlBar->raise();
        }

        if (m_volumePopup) {
            if (m_volumePopupVisible && m_controlsVisible) {
                const QPoint btnTopLeft = m_volumeButton->mapTo(m_playerContainer,
                                                               QPoint(0, 0));
                const int x = btnTopLeft.x() + m_volumeButton->width() / 2 -
                              m_volumePopup->width() / 2;
                const int y = btnTopLeft.y() - m_volumePopup->height() - 6;
                m_volumePopup->move(x, y);
            } else {
                m_volumePopup->move(0, offY);
            }
            m_volumePopup->raise();
        }
    }

    void PlayerDialog::showControls() {
        if (!m_controlBar) return;
        if (!m_controlsVisible) {
            m_controlsVisible = true;
            layoutOverlay();
        }
        scheduleHideControls();
    }

    void PlayerDialog::scheduleHideControls() {
        if (m_hideTimer) m_hideTimer->start();
    }

    bool PlayerDialog::eventFilter(QObject *watched, QEvent *event) {
        const QEvent::Type t = event->type();

        if (watched == m_playerContainer && t == QEvent::Resize) {
            layoutOverlay();
        }

        if (watched == m_playerContainer || watched == m_video ||
            watched == m_controlBar || watched == m_volumePopup) {
            if (t == QEvent::MouseMove || t == QEvent::Enter ||
                t == QEvent::HoverMove) {
                showControls();
            } else if (t == QEvent::Leave) {
                scheduleHideControls();
            }
        }

        if (watched == m_playerContainer && t == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(event);
            if (ke->key() == Qt::Key_Escape && m_isFullscreen) {
                exitFullscreen();
                return true;
            }
        }
        return QDialog::eventFilter(watched, event);
    }

    void PlayerDialog::showQualityMenu() {
        if (m_current < 0 || m_current >= static_cast<int>(m_videos.size())) return;
        const auto &video = m_videos[m_current];
        if (video.playInfo.empty()) return;

        QMenu menu(this);
        for (int i = 0; i < static_cast<int>(video.playInfo.size()); ++i) {
            const auto &pi = video.playInfo[i];
            QString label = QString::fromStdString(pi.name);
            if (label.isEmpty()) label = QStringLiteral("画质%1").arg(i + 1);
            if (pi.height > 0) label += QStringLiteral(" (%1P)").arg(pi.height);

            QAction *act = menu.addAction(label);
            act->setCheckable(true);
            act->setChecked(i == m_currentQuality);
            connect(act, &QAction::triggered, this, [this, i] { applyQuality(i); });
        }
        menu.exec(m_qualityButton->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
    }

    void PlayerDialog::applyQuality(int playInfoIndex) {
        if (m_current < 0 || m_current >= static_cast<int>(m_videos.size())) return;
        const auto &video = m_videos[m_current];
        if (playInfoIndex < 0 || playInfoIndex >= static_cast<int>(video.playInfo.size())) return;

        const auto &pi = video.playInfo[playInfoIndex];
        QString url = QString::fromStdString(pi.url);
        if (url.isEmpty() && !pi.urlList.empty()) {
            url = QString::fromStdString(pi.urlList.front().url);
        }
        if (url.isEmpty()) return;

        m_currentQuality = playInfoIndex;
        QString name = QString::fromStdString(pi.name);
        m_qualityButton->setText(name.isEmpty() ? QStringLiteral("画质") : name);

        m_player->play(url);
        m_playPauseButton->setText(QStringLiteral("⏸"));
    }

    void PlayerDialog::populateQuality(const Models::VideoData &video) {
        m_currentQuality = -1;
        m_qualityButton->setEnabled(!video.playInfo.empty());
        m_qualityButton->setText(video.playInfo.empty() ? QStringLiteral("画质")
                                                        : QStringLiteral("自动"));
    }

    void PlayerDialog::showSpeedMenu() {
        if (!m_player) return;
        QMenu menu(this);
        const double speeds[] = {0.5, 0.75, 1.0, 1.25, 1.5, 2.0};
        for (double s : speeds) {
            QString label = qFuzzyCompare(s, 1.0)
                                ? QStringLiteral("正常 (1.0x)")
                                : QStringLiteral("%1x").arg(s);
            QAction *act = menu.addAction(label);
            act->setCheckable(true);
            act->setChecked(qAbs(s - m_playbackSpeed) < 1e-3);
            connect(act, &QAction::triggered, this, [this, s] {
                m_playbackSpeed = s;
                m_player->setSpeed(s);
                m_speedButton->setText(qFuzzyCompare(s, 1.0) ? QStringLiteral("倍速")
                                                            : QStringLiteral("%1x").arg(s));
            });
        }
        menu.exec(m_speedButton->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
    }

    void PlayerDialog::onVolumeChanged(int value) {
        m_volume = value / 100.0;
        m_muted = (value == 0);
        if (m_player) m_player->setVolume(m_volume);
        updateVolumeIcon();
    }

    void PlayerDialog::updateVolumeIcon() {
        QString glyph;
        if (m_muted || m_volume <= 0.0) {
            glyph = QStringLiteral("🔇");
        } else if (m_volume < 0.5) {
            glyph = QStringLiteral("🔉");
        } else {
            glyph = QStringLiteral("🔊");
        }
        m_volumeButton->setText(glyph);
    }

    void PlayerDialog::toggleFullscreen() {
        if (m_isFullscreen) {
            exitFullscreen();
        } else {
            enterFullscreen();
        }
    }

    void PlayerDialog::enterFullscreen() {
        if (m_isFullscreen) return;
        m_isFullscreen = true;

        m_playLayout->removeWidget(m_playerContainer);
        m_playerContainer->setParent(nullptr);
        m_playerContainer->setWindowFlags(Qt::Window);
        m_playerContainer->setStyleSheet(
            QStringLiteral("QWidget#playerContainer{background-color:#000000;}"));
        m_playerContainer->showFullScreen();
        m_playerContainer->setFocus();

        m_fullscreenButton->setText(QStringLiteral("⤡"));
        m_fullscreenButton->setToolTip(QStringLiteral("退出全屏"));

        layoutOverlay();
        showControls();
    }

    void PlayerDialog::exitFullscreen() {
        if (!m_isFullscreen) return;
        m_isFullscreen = false;

        m_playerContainer->setWindowFlags(Qt::Widget);
        m_playerContainer->setStyleSheet(QString());
        // 重新放回播放区左侧（相关视频此时位于索引 0）。
        m_playLayout->insertWidget(0, m_playerContainer, 7);
        m_playerContainer->show();

        m_fullscreenButton->setText(QStringLiteral("⛶"));
        m_fullscreenButton->setToolTip(QStringLiteral("全屏"));

        layoutOverlay();
        showControls();
    }

    void PlayerDialog::toggleSubtitles() {
        m_subtitlesEnabled = !m_subtitlesEnabled;
        m_subtitleButton->setProperty("active", m_subtitlesEnabled);
        m_subtitleButton->style()->unpolish(m_subtitleButton);
        m_subtitleButton->style()->polish(m_subtitleButton);
        if (!m_subtitlesEnabled) {
            m_subtitleLabel->hide();
        }
    }

    void PlayerDialog::onPositionChanged(double position, double duration) {
        if (duration <= 0 && m_current >= 0 &&
            m_current < static_cast<int>(m_videos.size())) {
            duration = m_videos[m_current].duration;
        }
        QString text = formatTime(position);
        if (duration > 0) {
            text += QStringLiteral(" / ") + formatTime(duration);
        }
        m_timeLabel->setText(text);
    }

    QString PlayerDialog::formatTime(double seconds) {
        if (seconds < 0) seconds = 0;
        const int total = static_cast<int>(seconds);
        const int m = total / 60;
        const int s = total % 60;
        return QStringLiteral("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    }

    void PlayerDialog::playIndex(int index) {
        if (index < 0 || index >= static_cast<int>(m_videos.size())) return;
        m_current = index;
        playVideo(m_videos[index]);
    }

    void PlayerDialog::playVideo(const Models::VideoData &video) {
        QString title = QString::fromStdString(video.title);
        if (title.isEmpty()) title = QStringLiteral("(无标题)");

        QString author = QString::fromStdString(video.author.name);
        QString authorDesc = QString::fromStdString(video.author.description);
        int minutes = video.duration / 60;
        int seconds = video.duration % 60;

        // 更新顶部信息区
        m_titleLabel->setText(title);
        m_authorLabel->setText(author.isEmpty() ? QStringLiteral("未知作者") : author);
        m_authorDescLabel->setText(authorDesc);
        m_authorDescLabel->setVisible(!authorDesc.isEmpty());

        QString stats = QStringLiteral("%1次播放").arg(video.consumption.collectionCount);
        if (video.releaseTime > 0) {
            QDateTime dt = QDateTime::fromMSecsSinceEpoch(video.releaseTime);
            stats += QStringLiteral(" · %1").arg(dt.toString("yyyy-MM-dd"));
        }
        m_statsLabel->setText(stats);

        m_timeLabel->setText(QStringLiteral("00:00 / %1:%2")
                                 .arg(minutes, 2, 10, QChar('0'))
                                 .arg(seconds, 2, 10, QChar('0')));

        m_subtitleLabel->clear();
        m_subtitleLabel->hide();
        m_status->clear();
        m_status->hide();

        populateQuality(video);

        m_player->play(QString::fromStdString(video.playUrl));
        m_playPauseButton->setEnabled(true);
        m_playPauseButton->setText(QStringLiteral("⏸"));
        showControls();

        // 拉取相关视频填充右侧面板。
        fetchRelated(video.id);
    }

    void PlayerDialog::clearRelated() {
        if (!m_relatedLayout) return;
        // 移除除末尾 stretch 外的所有项。
        while (m_relatedLayout->count() > 1) {
            QLayoutItem *item = m_relatedLayout->takeAt(0);
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }
    }

    void PlayerDialog::fetchRelated(int videoId) {
        if (!m_api || videoId <= 0) return;
        clearRelated();

        m_api->getRelated(videoId,
            [this](const Models::RankResponse &response, bool success) {
                if (!success) return;
                clearRelated();
                int idx = 0;
                for (const auto &item : response.itemList) {
                    QString type = QString::fromStdString(item.type);
                    const auto &data = item.data;
                    // 仅保留可播放的视频小卡，且作者信息齐全。
                    if (!type.contains("video", Qt::CaseInsensitive)) continue;
                    if (data.playUrl.empty() || data.ad) continue;

                    auto *card = new VideoCard(data, idx, m_network, m_relatedContent);
                    // 末尾 stretch 始终在最后，故插入到 count()-1 位置。
                    m_relatedLayout->insertWidget(m_relatedLayout->count() - 1, card);

                    Models::VideoData copy = data;
                    connect(card, &VideoCard::clicked, this,
                            [this, copy](int) { playVideo(copy); });
                    ++idx;
                }
            },
            [](const QString &) { /* 相关视频拉取失败时静默，不影响主播放 */ });
    }

} // namespace Mixed::Player
