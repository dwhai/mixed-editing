//
// Created by Anlk on 2026/6/3.
// 播放页：通过 VideoAPI 拉取首页视频列表，取其播放地址（playUrl）后用
// MediaPlayer 播放，演示“视频源通过调用结果获取”的完整链路。
//

#ifndef MIXEDEDITING_PLAYERPAGE_H
#define MIXEDEDITING_PLAYERPAGE_H

#include <QWidget>
#include <vector>

class QLabel;
class QPushButton;
class QNetworkAccessManager;

namespace Mixed {
    namespace API { class VideoAPI; }

    namespace Player {
        class VideoGLWidget;
        class MediaPlayer;

        class PlayerPage : public QWidget {
            Q_OBJECT

        public:
            explicit PlayerPage(QWidget *parent = nullptr);
            ~PlayerPage() override;

        private:
            void fetchPlaylist();
            void playIndex(int index);

            VideoGLWidget *m_video = nullptr;
            MediaPlayer *m_player = nullptr;
            QLabel *m_status = nullptr;
            QPushButton *m_nextButton = nullptr;

            QNetworkAccessManager *m_network = nullptr;
            API::VideoAPI *m_api = nullptr;

            // 从接口结果中提取出的可播放地址与标题。
            std::vector<QString> m_urls;
            std::vector<QString> m_titles;
            int m_current = -1;
        };
    } // namespace Player
} // namespace Mixed

#endif //MIXEDEDITING_PLAYERPAGE_H
