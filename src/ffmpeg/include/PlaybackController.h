//
// Created by Anlk on 2026/6/7.
// PlaybackController：时间线实时播放控制器。
//
// 设计与导出隔离一致——自带独立 CompositionEngine（在视频 worker 线程上逐帧合成），
// 把帧推入有界队列；自带 AudioOutput（QAudioSink，GUI 线程创建）作为音频主时钟；
// 音频 worker 复用 AudioMix 的 mixAudioRange 做流式分块混音，PCM 以「绝对时间线时间」
// 为 PTS 喂给声卡，故主时钟可直接映射回 timelineUs。GUI QTimer 按主时钟取帧出显。
//
// 线程模型：
//   - 视频 worker：composeAt(i) → frameQueue（队满阻塞 = 预合成节流）
//   - 音频 worker：mixAudioRange 分块 → S16 → AudioOutput::enqueue（按缓冲节流）
//   - GUI tick：master = 音频时钟(有声) / 墙钟(静音兜底)，挑到点帧 emit frameReady
//

#ifndef MIXEDEDITING_PLAYBACKCONTROLLER_H
#define MIXEDEDITING_PLAYBACKCONTROLLER_H

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <vector>

#include "PlayerTypes.h"
#include "AudioMix.h"
#include "../../db/include/DbModels.h"

class QTimer;

namespace Mixed::Player {

    class AudioOutput;

    class PlaybackController : public QObject {
        Q_OBJECT

    public:
        explicit PlaybackController(QObject *parent = nullptr);
        ~PlaybackController() override;

        void setCanvasSize(int width, int height);

        // 设置时间线快照（值语义，跨线程安全）。clipsByTrack/assetPathByClip 用 QHash
        // 与 EditorWindow::buildTimelineSnapshot 直接对接。
        void setTimeline(const std::vector<DB::Track> &tracks,
                         const QHash<QString, std::vector<DB::Clip>> &clipsByTrack,
                         const QHash<QString, QString> &assetPathByClip,
                         qint64 durationUs, int fps);

        void play(qint64 startUs);  // 从 startUs 开始连续播放
        void pause();
        void resume();
        void stop();                // 停止并 join worker、停声卡
        void seek(qint64 us);       // 跳转（= stop + play(us)），仅在播放中有意义

        void setVolume(qreal volume);

        bool isPlaying() const { return m_playing; }
        bool isPaused() const { return m_paused; }

    signals:
        void frameReady(const VideoFrame &frame);
        void positionChanged(qint64 timelineUs);
        void reachedEnd();

    private slots:
        void onTick();

    private:
        void startWorkers(qint64 startUs);
        void stopWorkers();
        void videoThreadMain(qint64 startFrame);
        void audioThreadMain(qint64 startUs);
        double masterSeconds();   // 当前主时钟（秒，绝对时间线）

        // 时间线快照。
        std::vector<DB::Track> m_tracks;
        std::unordered_map<QString, std::vector<DB::Clip>> m_clipsByTrack; // 供 CompositionEngine
        std::unordered_map<QString, QString> m_pathMap;                    // 供 CompositionEngine
        AudioMixSource m_audioSrc;                                         // 供 mixAudioRange
        bool   m_hasAudio = false;
        qint64 m_durationUs = 0;
        int    m_fps = 30;
        int    m_canvasW = 1920;
        int    m_canvasH = 1080;

        // 运行态。
        AudioOutput *m_audio = nullptr;
        QTimer      *m_tick = nullptr;
        ThreadSafeQueue<VideoFrame> m_frameQueue{8};
        std::thread  m_videoThread;
        std::thread  m_audioThread;
        std::atomic<bool> m_abort{false};
        std::atomic<bool> m_videoDone{false};

        qint64 m_startUs = 0;
        bool   m_playing = false;
        bool   m_paused = false;
        bool   m_prerolled = false;   // 队列攒够帧后才开始出帧，避免起播抖动

        // 主时钟锚点（音频时钟停滞时用墙钟平滑兜底，静音工程也能推进）。
        QElapsedTimer m_anchorTimer;
        double m_clockAnchorPts = 0.0;
        double m_lastAudioClock = -1.0;
        qreal  m_volume = 1.0;
    };

} // namespace Mixed::Player

#endif // MIXEDEDITING_PLAYBACKCONTROLLER_H
