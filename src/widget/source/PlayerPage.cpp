//
// Created by Anlk on 2026/6/3.
// PlayerPage 实现。
//

#include "../include/PlayerPage.h"
#include "../include/VideoGLWidget.h"

#include "../../ffmpeg/include/MediaPlayer.h"
#include "../../network/include/VideoAPI.h"

#include <QAction>
#include <QCursor>
#include <QDateTime>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QPushButton>
#include <QResizeEvent>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace Mixed::Player {

    PlayerPage::PlayerPage(QWidget *parent) : QWidget(parent) {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);

        // 顶部信息区（标题/统计 + 作者），保持紧凑高度。
        layout->addWidget(buildTopInfoArea(), /*stretch=*/0);

        // 中间播放区：左侧播放器(7) + 右侧相关视频(3)，占据剩余全部高度。
        m_playLayout = new QHBoxLayout();
        m_playLayout->setSpacing(12);

        m_playLayout->addWidget(buildPlayerArea(), 7);

        // 右侧：相关视频列表（占位）
        m_relatedVideos = new QWidget(this);
        m_relatedVideos->setObjectName("relatedVideos");
        m_relatedVideos->setMinimumWidth(220);
        m_playLayout->addWidget(m_relatedVideos, 3);

        layout->addLayout(m_playLayout, /*stretch=*/1);

        // 控制条自动隐藏定时器（鼠标离开 5s 后隐藏）。
        m_hideTimer = new QTimer(this);
        m_hideTimer->setSingleShot(true);
        m_hideTimer->setInterval(5000);
        connect(m_hideTimer, &QTimer::timeout, this, [this] {
            // 鼠标仍在播放区域内则不隐藏，继续等待。
            const QPoint local = m_playerContainer->mapFromGlobal(QCursor::pos());
            if (m_playerContainer->rect().contains(local)) {
                scheduleHideControls();
                return;
            }
            // 通过移出可视区隐藏（不调用 hide），保持堆叠层级稳定。
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

        connect(m_qualityButton, &QPushButton::clicked, this, &PlayerPage::showQualityMenu);
        connect(m_speedButton, &QPushButton::clicked, this, &PlayerPage::showSpeedMenu);
        connect(m_volumeButton, &QPushButton::clicked, this, [this] {
            if (!m_volumePopup) return;
            m_volumePopupVisible = !m_volumePopupVisible;
            m_controlsVisible = true;
            layoutOverlay(); // 重新布局并定位弹窗
            scheduleHideControls();
        });
        connect(m_subtitleButton, &QPushButton::clicked, this, &PlayerPage::toggleSubtitles);
        connect(m_fullscreenButton, &QPushButton::clicked, this, &PlayerPage::toggleFullscreen);

        m_player = new MediaPlayer(this);
        // 解码引擎与显示控件通过信号解耦：引擎出帧，控件上屏。
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
        connect(m_player, &MediaPlayer::positionChanged, this, &PlayerPage::onPositionChanged);
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

    void PlayerPage::showEvent(QShowEvent *event) {
        QWidget::showEvent(event);
        layoutOverlay();
        showControls();
        if (!m_initialized) {
            m_initialized = true;
            fetchPlaylist();
        }
    }

    void PlayerPage::hideEvent(QHideEvent *event) {
        QWidget::hideEvent(event);
        if (m_player && m_player->isPlaying() && !m_player->isPaused()) {
            m_player->pause();
            m_playPauseButton->setText(QStringLiteral("▶"));
        }
    }

    PlayerPage::~PlayerPage() {
        if (m_player) m_player->stop();
        delete m_api;
    }

    QWidget *PlayerPage::buildTopInfoArea() {
        auto *area = new QWidget(this);
        auto *layout = new QHBoxLayout(area);
        layout->setContentsMargins(12, 12, 12, 12);

        // 左侧：标题和统计信息
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

        // 右侧：作者信息
        auto *rightLayout = new QVBoxLayout();
        m_authorLabel = new QLabel(QStringLiteral("作者名称"), area);
        m_authorLabel->setObjectName("authorName");
        rightLayout->addWidget(m_authorLabel);
        rightLayout->addStretch();

        layout->addLayout(rightLayout, 3);
        return area;
    }

    QWidget *PlayerPage::buildPlayerArea() {
        // 堆叠布局：视频画面按宽高比居中铺放，控制条/字幕以子控件悬浮其上。
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

        // 字幕叠加层。
        m_subtitleLabel = new QLabel(m_playerContainer);
        m_subtitleLabel->setObjectName("playerSubtitle");
        m_subtitleLabel->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        m_subtitleLabel->setWordWrap(true);
        m_subtitleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_subtitleLabel->hide();

        // 监听容器尺寸变化与鼠标活动。
        m_playerContainer->installEventFilter(this);
        m_video->installEventFilter(this);
        m_controlBar->installEventFilter(this);
        return m_playerContainer;
    }

    QWidget *PlayerPage::buildControlBar() {
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

        // 状态文本独立悬浮在控制条上方左下角，承载加载/错误提示。
        m_status = new QLabel(QStringLiteral("正在获取视频列表..."), m_playerContainer);
        m_status->setObjectName("playerStatus");
        m_status->setAttribute(Qt::WA_TransparentForMouseEvents);

        return bar;
    }

    void PlayerPage::buildVolumePopup() {
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

        connect(m_volumeSlider, &QSlider::valueChanged, this, &PlayerPage::onVolumeChanged);

        m_volumePopup->installEventFilter(this);
        // 不调用 hide()，改为初始移到可视区外，避免后续 show 破坏堆叠层级。
        m_volumePopup->move(0, 5000);
    }

    QRect PlayerPage::videoRect() const {
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

    void PlayerPage::layoutOverlay() {
        if (!m_playerContainer || !m_video) return;
        const QRect vr = videoRect();
        m_video->setGeometry(vr);

        // 关键：悬浮控件始终保持 show() 状态，仅通过移动到可视区外来“隐藏”，
        // 从而避免在 QOpenGLWidget 之上反复 hide/show 破坏堆叠层级。
        const int barHeight = 48;
        const int offY = m_playerContainer->height() + 80; // 容器外（被裁剪不可见）

        // 字幕：与控制条无关，始终位于画面底部上方。
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

    void PlayerPage::showControls() {
        if (!m_controlBar) return;
        // 仅在“隐藏->显示”切换时重新布局，避免每次鼠标移动都重排/raise，
        // 既减少开销，也避免在点击过程中打断按钮的 press/release。
        if (!m_controlsVisible) {
            m_controlsVisible = true;
            layoutOverlay();
        }
        scheduleHideControls();
    }

    void PlayerPage::scheduleHideControls() {
        if (m_hideTimer) m_hideTimer->start();
    }

    bool PlayerPage::eventFilter(QObject *watched, QEvent *event) {
        const QEvent::Type t = event->type();

        if (watched == m_playerContainer && t == QEvent::Resize) {
            layoutOverlay();
        }

        // 鼠标在播放区域活动 -> 显示控制条并重置隐藏计时。
        if (watched == m_playerContainer || watched == m_video ||
            watched == m_controlBar || watched == m_volumePopup) {
            if (t == QEvent::MouseMove || t == QEvent::Enter ||
                t == QEvent::HoverMove) {
                showControls();
            } else if (t == QEvent::Leave) {
                scheduleHideControls();
            }
        }

        // 全屏下按 Esc 退出。
        if (watched == m_playerContainer && t == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(event);
            if (ke->key() == Qt::Key_Escape && m_isFullscreen) {
                exitFullscreen();
                return true;
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    void PlayerPage::showQualityMenu() {
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

    void PlayerPage::applyQuality(int playInfoIndex) {
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

    void PlayerPage::populateQuality(const Models::VideoData &video) {
        m_currentQuality = -1;
        m_qualityButton->setEnabled(!video.playInfo.empty());
        m_qualityButton->setText(video.playInfo.empty() ? QStringLiteral("画质")
                                                        : QStringLiteral("自动"));
    }

    void PlayerPage::showSpeedMenu() {
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

    void PlayerPage::onVolumeChanged(int value) {
        m_volume = value / 100.0;
        m_muted = (value == 0);
        if (m_player) m_player->setVolume(m_volume);
        updateVolumeIcon();
    }

    void PlayerPage::updateVolumeIcon() {
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

    void PlayerPage::toggleFullscreen() {
        if (m_isFullscreen) {
            exitFullscreen();
        } else {
            enterFullscreen();
        }
    }

    void PlayerPage::enterFullscreen() {
        if (m_isFullscreen) return;
        m_isFullscreen = true;

        m_playLayout->removeWidget(m_playerContainer);
        m_playerContainer->setParent(nullptr);
        m_playerContainer->setWindowFlags(Qt::Window);
        // 全屏时画面留黑边（而非透出桌面/页面背景）。
        m_playerContainer->setStyleSheet(
            QStringLiteral("QWidget#playerContainer{background-color:#000000;}"));
        m_playerContainer->showFullScreen();
        m_playerContainer->setFocus();

        m_fullscreenButton->setText(QStringLiteral("⤡"));
        m_fullscreenButton->setToolTip(QStringLiteral("退出全屏"));

        layoutOverlay();
        showControls();
    }

    void PlayerPage::exitFullscreen() {
        if (!m_isFullscreen) return;
        m_isFullscreen = false;

        m_playerContainer->setWindowFlags(Qt::Widget);
        m_playerContainer->setStyleSheet(QString()); // 恢复透明背景
        // 重新放回播放区左侧（相关视频此时位于索引 0）。
        m_playLayout->insertWidget(0, m_playerContainer, 7);
        m_playerContainer->show();

        m_fullscreenButton->setText(QStringLiteral("⛶"));
        m_fullscreenButton->setToolTip(QStringLiteral("全屏"));

        layoutOverlay();
        showControls();
    }

    void PlayerPage::toggleSubtitles() {
        m_subtitlesEnabled = !m_subtitlesEnabled;
        m_subtitleButton->setProperty("active", m_subtitlesEnabled);
        m_subtitleButton->style()->unpolish(m_subtitleButton);
        m_subtitleButton->style()->polish(m_subtitleButton);
        if (!m_subtitlesEnabled) {
            m_subtitleLabel->hide();
        }
    }

    void PlayerPage::onPositionChanged(double position, double duration) {
        // 部分流不上报总时长，回退到接口返回的 duration。
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

    QString PlayerPage::formatTime(double seconds) {
        if (seconds < 0) seconds = 0;
        const int total = static_cast<int>(seconds);
        const int m = total / 60;
        const int s = total % 60;
        return QStringLiteral("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    }

    void PlayerPage::fetchPlaylist() {
        // 注意：date=0 时开眼接口返回空列表，必须传入真实的毫秒时间戳。
        m_api->getFeed(QDateTime::currentMSecsSinceEpoch(),
            [this](const Models::FeedResponse &response, bool success) {
                if (!success) {
                    m_status->setText(QStringLiteral("获取列表失败"));
                    return;
                }
                m_videos.clear();
                for (const auto &issue : response.issueList) {
                    for (const auto &item : issue.itemList) {
                        QString type = QString::fromStdString(item.type);
                        const auto &data = item.data;
                        if (type.contains("video", Qt::CaseInsensitive) &&
                            !data.playUrl.empty() && !data.ad) {
                            m_videos.push_back(data);
                        }
                    }
                }

                if (m_videos.empty()) {
                    m_status->setText(QStringLiteral("没有可播放的视频"));
                    return;
                }
                m_nextButton->setEnabled(true);
                m_prevButton->setEnabled(true);
                playIndex(0);
            },
            [this](const QString &err) {
                m_status->setText(QStringLiteral("网络错误: ") + err);
            });
    }

    void PlayerPage::playIndex(int index) {
        if (index < 0 || index >= static_cast<int>(m_videos.size())) return;
        m_current = index;

        const auto &video = m_videos[index];
        QString title = QString::fromStdString(video.title);
        if (title.isEmpty()) title = QStringLiteral("(无标题)");

        QString author = QString::fromStdString(video.author.name);
        int minutes = video.duration / 60;
        int seconds = video.duration % 60;

        // 更新顶部信息区
        m_titleLabel->setText(title);
        m_authorLabel->setText(author.isEmpty() ? QStringLiteral("未知作者") : author);

        QString stats = QStringLiteral("%1次播放").arg(video.consumption.collectionCount);
        if (video.releaseTime > 0) {
            QDateTime dt = QDateTime::fromMSecsSinceEpoch(video.releaseTime);
            stats += QStringLiteral(" · %1").arg(dt.toString("yyyy-MM-dd"));
        }
        m_statsLabel->setText(stats);

        // 时间标签初始化为 00:00 / 总时长。
        m_timeLabel->setText(QStringLiteral("00:00 / %1:%2")
                                 .arg(minutes, 2, 10, QChar('0'))
                                 .arg(seconds, 2, 10, QChar('0')));

        // 切换视频时清空字幕与状态提示。
        m_subtitleLabel->clear();
        m_subtitleLabel->hide();
        m_status->clear();
        m_status->hide();

        populateQuality(video);

        m_player->play(QString::fromStdString(video.playUrl));
        m_playPauseButton->setEnabled(true);
        m_playPauseButton->setText(QStringLiteral("⏸"));
        showControls();
    }

} // namespace Mixed::Player
