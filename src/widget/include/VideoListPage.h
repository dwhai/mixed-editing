//
// Created by Anlk on 2026/6/6.
// 短剧页：以网格形式渲染视频列表（VideoCard）。沿用游标式翻页拉取首页 feed，
// 滚动到底部附近时自动加载下一页。点击卡片打开非模态 PlayerDialog 播放。
//

#ifndef MIXEDEDITING_VIDEOLISTPAGE_H
#define MIXEDEDITING_VIDEOLISTPAGE_H

#include <QWidget>
#include <QPointer>
#include <vector>
#include "../../network/include/models/VideoData.h"

class QGridLayout;
class QLabel;
class QScrollArea;
class QNetworkAccessManager;

namespace Mixed {
    namespace API { class VideoAPI; }
    namespace Models { struct FeedResponse; }
    namespace Player { class PlayerDialog; }

    class VideoListPage : public QWidget {
        Q_OBJECT

    public:
        explicit VideoListPage(QWidget *parent = nullptr);
        ~VideoListPage() override;

    protected:
        void showEvent(QShowEvent *event) override;
        void resizeEvent(QResizeEvent *event) override;

    private:
        void fetchFirstPage();
        void loadNextPage();
        void maybeFillViewport();
        void appendVideos(const Models::FeedResponse &response);
        static QString parseDateCursor(const std::string &nextPageUrl);
        void relayoutGrid();
        void rebuildGrid();
        int columnCount() const;
        void openPlayer(int index);
        void onScrolled(int value);

        QScrollArea *m_scrollArea = nullptr;
        QWidget *m_grid = nullptr;
        QGridLayout *m_gridLayout = nullptr;
        QLabel *m_status = nullptr;

        QNetworkAccessManager *m_network = nullptr;
        API::VideoAPI *m_api = nullptr;
        QPointer<Player::PlayerDialog> m_playerDialog;

        std::vector<Models::VideoData> m_videos;
        int m_columns = 0;
        bool m_initialized = false;

        // 游标式翻页状态。
        QString m_nextDate;
        bool m_loadingPage = false;
        bool m_hasMore = true;
    };
} // Mixed

#endif //MIXEDEDITING_VIDEOLISTPAGE_H
