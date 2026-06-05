//
// Created by Anlk on 2026/6/6.
// 播放器对话框：从原 PlayerPage 抽离而来的独立非模态 QDialog。
// 不再自行拉取首页列表，改由列表页通过 setPlaylist() 传入播放列表与起始索引；
// 右侧「相关视频」面板通过 /api/v4/video/related 接口拉取并渲染。
//

#ifndef MIXEDEDITING_PLAYERDIALOG_H
#define MIXEDEDITING_PLAYERDIALOG_H

#include <QDialog>
#include <vector>
#include "../../network/include/models/VideoData.h"

class QLabel;
class QPushButton;
class QSlider;
class QProgressBar;
class QHBoxLayout;
class QVBoxLayout;
class QTimer;
class QNetworkAccessManager;

namespace Mixed {
    namespace API { class VideoAPI; }

    namespace Player {
        class VideoGLWidget;
        class MediaPlayer;

        class PlayerDialog : public QDialog {
            Q_OBJECT

        public:
            explicit PlayerDialog(QWidget *parent = nullptr);
            ~PlayerDialog() override;

            // 设置（或替换）播放列表并从 startIndex 开始播放。可在对话框已显示时再次调用。
            void setPlaylist(const std::vector<Models::VideoData> &videos, int startIndex);

        protected:
            void closeEvent(QCloseEvent *event) override;
            bool eventFilter(QObject *watched, QEvent *event) override;

        private:
            void playIndex(int index);
            void playVideo(const Models::VideoData &video);
            QWidget *buildTopInfoArea();
            QWidget *buildPlayerArea();
            QWidget *buildControlBar();
            QWidget *buildRelatedPanel();
            void buildVolumePopup();
            void layoutOverlay();
            QRect videoRect() const;

            // 相关视频
            void fetchRelated(int videoId);
            void clearRelated();

            // 控制条显隐
            void showControls();
            void scheduleHideControls();

            // 播放控制
            void togglePlayPause();

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
            QProgressBar *m_progressBar = nullptr; // 视频底部进度条（仅展示，不支持拖动跳转）
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
            QLabel *m_authorDescLabel = nullptr;
            QLabel *m_statsLabel = nullptr;

            // 右侧相关视频面板
            QWidget *m_relatedPanel = nullptr;
            QWidget *m_relatedContent = nullptr;
            QVBoxLayout *m_relatedLayout = nullptr;
            QHBoxLayout *m_playLayout = nullptr;

            QTimer *m_hideTimer = nullptr;
            // 区分单击/双击：单击延迟到双击判定窗口结束后再执行播放/暂停，
            // 若期间收到双击则取消单击、改为切换全屏。
            QTimer *m_clickTimer = nullptr;
            // 双击后 Qt 还会再补发一次 Release，需忽略以免误触发单击。
            bool m_ignoreNextRelease = false;

            QNetworkAccessManager *m_network = nullptr;
            API::VideoAPI *m_api = nullptr;

            // 当前播放列表（由列表页传入的快照）与当前索引。
            std::vector<Models::VideoData> m_videos;
            int m_current = -1;

            // 播放器运行状态
            double m_videoAspect = 16.0 / 9.0;
            bool m_isFullscreen = false;
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

#endif //MIXEDEDITING_PLAYERDIALOG_H
