//
// Created by Anlk on 2026/6/3.
// 播放页：通过 VideoAPI 拉取首页视频列表，取其播放地址（playUrl）后用
// MediaPlayer 播放，演示“视频源通过调用结果获取”的完整链路。
//

#ifndef MIXEDEDITING_PLAYERPAGE_H
#define MIXEDEDITING_PLAYERPAGE_H

#include <QWidget>
#include <vector>
#include "../../network/include/models/VideoData.h"

class QLabel;
class QPushButton;
class QSlider;
class QHBoxLayout;
class QTimer;
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

        protected:
            void showEvent(QShowEvent *event) override;
            void hideEvent(QHideEvent *event) override;
            bool eventFilter(QObject *watched, QEvent *event) override;

        private:
            void fetchPlaylist();
            void playIndex(int index);
            QWidget *buildTopInfoArea();
            QWidget *buildPlayerArea();
            QWidget *buildControlBar();
            void buildVolumePopup();
            void layoutOverlay();
            QRect videoRect() const;

            // 控制条显隐
            void showControls();
            void scheduleHideControls();

            // 各功能
            void showQualityMenu();
            void applyQuality(int playInfoIndex);
            void populateQuality(const Models::VideoData &video);
            void showSpeedMenu();
            void onVolumeChanged(int value);
            void updateVolumeIcon();
            void toggleFullscreen();
            void enterFullscreen();
            void exitFullscreen();
            void toggleSubtitles();
            void onPositionChanged(double position, double duration);
            static QString formatTime(double seconds);

            VideoGLWidget *m_video = nullptr;
            MediaPlayer *m_player = nullptr;

            // 播放器容器与悬浮控制条（堆叠布局：控制条覆盖在画面之上）
            QWidget *m_playerContainer = nullptr;
            QWidget *m_controlBar = nullptr;
            QLabel *m_subtitleLabel = nullptr;

            QPushButton *m_prevButton = nullptr;
            QPushButton *m_playPauseButton = nullptr;
            QPushButton *m_nextButton = nullptr;
            QLabel *m_timeLabel = nullptr;
            QLabel *m_status = nullptr;

            QPushButton *m_qualityButton = nullptr;
            QPushButton *m_speedButton = nullptr;
            QPushButton *m_subtitleButton = nullptr;
            QPushButton *m_volumeButton = nullptr;
            QPushButton *m_fullscreenButton = nullptr;

            // 音量弹出滑块
            QWidget *m_volumePopup = nullptr;
            QSlider *m_volumeSlider = nullptr;

            // 顶部信息区
            QLabel *m_titleLabel = nullptr;
            QLabel *m_authorLabel = nullptr;
            QLabel *m_statsLabel = nullptr;

            QWidget *m_relatedVideos = nullptr;
            QHBoxLayout *m_playLayout = nullptr;

            QTimer *m_hideTimer = nullptr;

            QNetworkAccessManager *m_network = nullptr;
            API::VideoAPI *m_api = nullptr;

            // 从接口结果中提取出的视频数据。
            std::vector<Models::VideoData> m_videos;
            int m_current = -1;
            bool m_initialized = false;

            // 播放器运行状态
            double m_videoAspect = 16.0 / 9.0;
            bool m_isFullscreen = false;
            // 控制条/音量弹窗的可见状态（通过移出可视区实现隐藏，避免
            // hide/show 在 QOpenGLWidget 之上破坏层级导致点击失效）。
            bool m_controlsVisible = true;
            bool m_volumePopupVisible = false;
            bool m_subtitlesEnabled = true;
            qreal m_volume = 1.0;
            bool m_muted = false;
            int m_currentQuality = -1; // 选中的 playInfo 索引，-1 表示默认 playUrl
            double m_playbackSpeed = 1.0;
        };
    } // namespace Player
} // namespace Mixed

#endif //MIXEDEDITING_PLAYERPAGE_H
