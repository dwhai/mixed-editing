//
// Created by Anlk on 2026/6/3.
// 媒体播放器：FFmpeg 解封装/解码（多线程） + 音画同步（音频为主时钟）。
//
// 线程模型：
//   - 解封装线程：读取 packet，按音/视频分流到两个队列；
//   - 视频解码线程：解码 + sws 转 YUV420P，输出到帧队列；
//   - 音频解码线程：解码 + swr 重采样为 S16，喂给 QAudioSink；
//   - GUI 定时器：以音频主时钟为基准，从帧队列取帧送显（丢/等帧）。
//

#ifndef MIXEDEDITING_MEDIAPLAYER_H
#define MIXEDEDITING_MEDIAPLAYER_H

#include "PlayerTypes.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <atomic>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

class QTimer;

namespace Mixed::Player {

    class AudioOutput;

    class MediaPlayer : public QObject {
        Q_OBJECT

    public:
        explicit MediaPlayer(QObject *parent = nullptr);
        ~MediaPlayer() override;

        // 打开并开始播放（url 可以是本地文件或 http/https 网络地址）。
        void play(const QString &url);

        // 停止播放并释放资源。
        void stop();

        bool isPlaying() const { return m_playing; }

    signals:
        // 解码引擎不直接依赖 UI：按主时钟到点的帧通过信号发出，由显示控件接收。
        void frameReady(const VideoFrame &frame);
        void cleared();
        void errorOccurred(const QString &message);
        void started();
        void finished();

    private slots:
        void onStreamsReady(bool hasAudio, bool hasVideo);
        void onVideoTick();

    private:
        // 三个工作线程入口。
        void demuxThreadMain(QString url);
        void videoThreadMain();
        void audioThreadMain();

        bool openInput(const QString &url);
        bool openVideoCodec();
        bool openAudioCodec();
        void freeContexts();
        void flushPacketQueue(ThreadSafeQueue<AVPacket *> &queue);

        double currentMasterClock();

        // FFmpeg 上下文
        AVFormatContext *m_fmtCtx = nullptr;
        AVCodecContext *m_videoCodecCtx = nullptr;
        AVCodecContext *m_audioCodecCtx = nullptr;
        SwsContext *m_swsCtx = nullptr;
        SwrContext *m_swrCtx = nullptr;
        int m_videoStreamIndex = -1;
        int m_audioStreamIndex = -1;
        double m_videoTimeBase = 0.0;
        double m_audioTimeBase = 0.0;

        // 队列（packet 较大缓冲，避免一路队满阻塞解封装而饿死另一路）
        ThreadSafeQueue<AVPacket *> m_videoPackets{256};
        ThreadSafeQueue<AVPacket *> m_audioPackets{256};
        ThreadSafeQueue<VideoFrame> m_videoFrames{10};

        // 线程
        std::thread m_demuxThread;
        std::thread m_videoThread;
        std::thread m_audioThread;
        std::atomic<bool> m_abort{false};

        // 输出
        AudioOutput *m_audio = nullptr;
        QTimer *m_videoTimer = nullptr;

        // 同步状态
        bool m_hasAudio = false;
        bool m_hasVideo = false;
        bool m_playing = false;

        // 主时钟锚定：以音频时钟为锚，两次音频更新之间用墙钟平滑/兜底推进，
        // 避免音频时钟短暂停滞导致整个管线死锁。
        QElapsedTimer m_anchorTimer;
        double m_clockAnchorPts = 0.0;
        double m_lastAudioClock = -1.0;
        bool m_anchorInit = false;

        // 无音频时的视频时钟
        QElapsedTimer m_videoClock;
        bool m_videoClockStarted = false;
        double m_videoClockBasePts = 0.0;

        static constexpr int kOutSampleRate = 44100;
        static constexpr int kOutChannels = 2;
    };

} // namespace Mixed::Player

#endif //MIXEDEDITING_MEDIAPLAYER_H
