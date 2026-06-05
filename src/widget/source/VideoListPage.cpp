//
// Created by Anlk on 2026/6/6.
// VideoListPage 实现。
//

#include "../include/VideoListPage.h"
#include "../include/VideoCard.h"
#include "../include/PlayerDialog.h"

#include "../../network/include/VideoAPI.h"

#include <QGridLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QPixmapCache>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace Mixed {

    namespace {
        constexpr int kCardMinWidth = 240; // 单卡期望宽度，用于推算列数
        constexpr int kGridSpacing = 16;
    }

    VideoListPage::VideoListPage(QWidget *parent) : QWidget(parent) {
        // 提高进程级图片缓存上限：封面较多，默认 ~10MB 很快被挤出导致重排时重复下载。
        QPixmapCache::setCacheLimit(100 * 1024); // 单位 KB，约 100MB

        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        m_scrollArea = new QScrollArea(this);
        m_scrollArea->setWidgetResizable(true);
        m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        outer->addWidget(m_scrollArea);

        m_grid = new QWidget(m_scrollArea);
        m_grid->setObjectName("videoGrid");
        m_scrollArea->setWidget(m_grid);

        m_gridLayout = new QGridLayout(m_grid);
        m_gridLayout->setContentsMargins(16, 16, 16, 16);
        m_gridLayout->setSpacing(kGridSpacing);
        m_gridLayout->setAlignment(Qt::AlignTop);

        m_status = new QLabel(QStringLiteral("正在获取视频列表..."), m_grid);
        m_status->setObjectName("listStatus");
        m_status->setAlignment(Qt::AlignCenter);
        m_gridLayout->addWidget(m_status, 0, 0);

        m_network = new QNetworkAccessManager(this);
        m_api = new API::VideoAPI(m_network);

        connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged,
                this, &VideoListPage::onScrolled);
    }

    VideoListPage::~VideoListPage() {
        delete m_api;
    }

    void VideoListPage::showEvent(QShowEvent *event) {
        QWidget::showEvent(event);
        if (!m_initialized) {
            m_initialized = true;
            fetchFirstPage();
        }
    }

    void VideoListPage::resizeEvent(QResizeEvent *event) {
        QWidget::resizeEvent(event);
        // 列数随宽度变化时才重排，避免无谓重建。
        if (columnCount() != m_columns) {
            relayoutGrid();
        }
        // 视口变大（如窗口拉高/最大化）后内容可能不再铺满，按需补一页。
        if (m_initialized) {
            maybeFillViewport();
        }
    }

    int VideoListPage::columnCount() const {
        const int avail = m_scrollArea ? m_scrollArea->viewport()->width() : width();
        const int usable = avail - 32; // 减去左右边距
        int cols = (usable + kGridSpacing) / (kCardMinWidth + kGridSpacing);
        return qMax(1, cols);
    }

    void VideoListPage::onScrolled(int value) {
        auto *bar = m_scrollArea->verticalScrollBar();
        if (!bar) return;
        // 必须存在可滚动空间，且用户确实滚动了一段（value>0），
        // 才在接近底部（剩余不足半屏）时加载，避免内容刚铺满时在顶部误判触底。
        if (bar->maximum() <= 0 || value <= 0) return;
        const int threshold = m_scrollArea->viewport()->height() / 2;
        if (value >= bar->maximum() - threshold) {
            loadNextPage();
        }
    }

    void VideoListPage::fetchFirstPage() {
        m_videos.clear();
        m_nextDate.clear();
        m_hasMore = true;
        m_loadingPage = false;
        if (m_status) {
            m_status->setText(QStringLiteral("正在获取视频列表..."));
            m_status->show();
        }
        loadNextPage();
    }

    QString VideoListPage::parseDateCursor(const std::string &nextPageUrl) {
        const QString url = QString::fromStdString(nextPageUrl);
        const int q = url.indexOf(QLatin1Char('?'));
        if (q < 0) return QString();
        const QString query = url.mid(q + 1);
        const QStringList parts = query.split(QLatin1Char('&'), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            if (part.startsWith(QStringLiteral("date="))) {
                return part.mid(5);
            }
        }
        return QString();
    }

    void VideoListPage::appendVideos(const Models::FeedResponse &response) {
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
    }

    void VideoListPage::loadNextPage() {
        if (m_loadingPage || !m_hasMore) return;
        m_loadingPage = true;

        const bool firstPage = m_videos.empty();

        m_api->getFeed(m_nextDate,
            [this, firstPage](const Models::FeedResponse &response, bool success) {
                m_loadingPage = false;
                if (!success) {
                    if (firstPage && m_status) m_status->setText(QStringLiteral("获取列表失败"));
                    return;
                }

                const std::size_t before = m_videos.size();
                appendVideos(response);
                const bool gotNew = m_videos.size() > before;

                const QString next = parseDateCursor(response.nextPageUrl);
                m_hasMore = !next.isEmpty() && next != m_nextDate;
                m_nextDate = next;

                if (m_videos.empty()) {
                    if (m_hasMore) {
                        loadNextPage();
                    } else if (m_status) {
                        m_status->setText(QStringLiteral("没有可播放的视频"));
                    }
                    return;
                }

                if (gotNew) {
                    rebuildGrid();
                    // 内容若不足以铺满视口，继续向后翻页直到填满或没有更多数据，
                    // 之后再交由触底滚动按需加载。注意：必须延后判断，等事件循环完成
                    // 本次布局后再用真实滚动条状态衡量，否则同步读取的尺寸是过期的，
                    // 会误判“未填满”而无限连拉下一页。
                    maybeFillViewport();
                } else if (m_hasMore) {
                    // 本页没有新视频但仍有后续页，继续补齐。
                    loadNextPage();
                }
            },
            [this, firstPage](const QString &err) {
                m_loadingPage = false;
                if (firstPage && m_status) m_status->setText(QStringLiteral("网络错误: ") + err);
            });
    }

    void VideoListPage::maybeFillViewport() {
        if (!m_hasMore || m_loadingPage || !m_scrollArea) return;
        // 延后到事件循环：rebuildGrid 后 Qt 仅投递了 LayoutRequest，此刻同步读取的
        // 尺寸/滚动条仍是旧值。等布局真正生效后，再用滚动条是否出现来判断是否铺满。
        QTimer::singleShot(0, this, [this]() {
            if (!m_hasMore || m_loadingPage || !m_scrollArea) return;
            auto *bar = m_scrollArea->verticalScrollBar();
            // maximum>0 即说明内容已超出视口（出现可滚动空间），铺满，停止预取；
            // 否则还不够铺满一屏，再拉一页。
            if (bar && bar->maximum() > 0) return;
            loadNextPage();
        });
    }

    void VideoListPage::rebuildGrid() {
        // 清空现有网格内容（含状态标签），按当前列数重新铺放全部卡片。
        if (m_status) {
            m_status->hide();
        }
        // 移除所有现有 item。
        while (QLayoutItem *item = m_gridLayout->takeAt(0)) {
            if (item->widget() && item->widget() != m_status) {
                item->widget()->deleteLater();
            }
            delete item;
        }

        m_columns = columnCount();
        for (int i = 0; i < static_cast<int>(m_videos.size()); ++i) {
            auto *card = new VideoCard(m_videos[i], i, m_network, m_grid);
            connect(card, &VideoCard::clicked, this, &VideoListPage::openPlayer);
            m_gridLayout->addWidget(card, i / m_columns, i % m_columns);
        }
        // 让各列等宽。
        for (int c = 0; c < m_columns; ++c) {
            m_gridLayout->setColumnStretch(c, 1);
        }
    }

    void VideoListPage::relayoutGrid() {
        if (m_videos.empty()) return;
        rebuildGrid();
    }

    void VideoListPage::openPlayer(int index) {
        if (index < 0 || index >= static_cast<int>(m_videos.size())) return;

        if (!m_playerDialog) {
            // 顶层非模态对话框：parent 取窗口，便于居中且不被页面裁剪。
            m_playerDialog = new Player::PlayerDialog(window());
            m_playerDialog->resize(1100, 680);
        }
        m_playerDialog->setPlaylist(m_videos, index);
        m_playerDialog->show();
        m_playerDialog->raise();
        m_playerDialog->activateWindow();
    }
} // Mixed
