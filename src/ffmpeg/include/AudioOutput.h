//
// Created by Anlk on 2026/6/3.
// 音频播放：基于 Qt6 的 QAudioSink（Qt6 中用于裸 PCM 输出的推荐类，
// 取代了 Qt5 的 QAudioOutput）。采用 push 模式（主动写入声卡），并以
// 已播放时长充当音画同步的“主时钟”。
//

#ifndef MIXEDEDITING_AUDIOOUTPUT_H
#define MIXEDEDITING_AUDIOOUTPUT_H

#include "PlayerTypes.h"

#include <QObject>
#include <atomic>
#include <mutex>

class QAudioSink;
class QIODevice;
class QTimer;

namespace Mixed::Player {

    class AudioOutput : public QObject {
        Q_OBJECT

    public:
        explicit AudioOutput(QObject *parent = nullptr);
        ~AudioOutput() override;

        // 以固定格式（S16 交错）启动声卡。解码端需重采样到该格式。
        bool start(int sampleRate = 44100, int channels = 2);
        void stop();
        void pause();
        void resume();

        // 设置音量（线性 0.0~1.0）。可在声卡启动前调用，启动时自动套用。
        void setVolume(qreal volume);
        qreal volume() const { return m_volume; }

        // 设置倍速（用于主时钟换算）。实际的音频变速由解码端 atempo 滤镜完成，
        // 这里仅保证主时钟在变速点连续、不跳变。
        void setSpeed(double speed);

        // 由音频解码线程调用，提交一段已重采样的 PCM。
        void enqueue(const AudioFrame &frame);

        // 清空缓冲（停止/切换源时）。
        void clear();

        bool isRunning() const { return m_running; }
        int sampleRate() const { return m_sampleRate; }
        int channels() const { return m_channels; }
        int bytesPerSecond() const { return m_sampleRate * m_channels * 2; }

        // 主时钟：声卡已实际播放到的音频时间戳（秒）。
        // = 首个样本 PTS + 已处理时长（QAudioSink::processedUSecs）。
        double masterClock() const;

        // 当前已缓冲（本地队列 + 声卡内部）音频时长（秒），用于解码端节流。
        double bufferedSeconds() const;

    private slots:
        // 定时把本地队列中的 PCM 写入声卡空闲缓冲（push 模式）。
        void feed();

    private:
        QAudioSink *m_sink = nullptr;
        QIODevice *m_io = nullptr;   // 由 QAudioSink::start() 返回，归声卡所有
        QTimer *m_feedTimer = nullptr;

        std::deque<AudioFrame> m_queue;
        mutable std::mutex m_mutex;
        std::atomic<size_t> m_queuedBytes{0};

        AudioFrame m_current;        // 正在写入的帧
        qint64 m_offset = 0;         // 当前帧已写入字节数
        double m_startPts = 0.0;     // 首个写入样本的 PTS
        std::atomic<bool> m_started{false};

        // 主时钟换算锚点：源位置 = m_clockBasePts + (processed - m_clockBaseUSecs)*speed。
        // 变速时在此锚点处结算已播放部分，保证时钟连续。
        double m_clockBasePts = 0.0;
        qint64 m_clockBaseUSecs = 0;
        double m_speed = 1.0;

        int m_sampleRate = 44100;
        int m_channels = 2;
        bool m_running = false;
        qreal m_volume = 1.0;
    };

} // namespace Mixed::Player

#endif //MIXEDEDITING_AUDIOOUTPUT_H
