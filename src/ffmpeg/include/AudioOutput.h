//
// Created by Anlk on 2026/6/3.
// 音频播放：基于 Qt6 的 QAudioSink（Qt6 中用于裸 PCM 输出的推荐类，
// 取代了 Qt5 的 QAudioOutput）。同时充当音画同步的“主时钟”。
//

#ifndef MIXEDEDITING_AUDIOOUTPUT_H
#define MIXEDEDITING_AUDIOOUTPUT_H

#include "PlayerTypes.h"

#include <QAudioFormat>
#include <QIODevice>
#include <QObject>
#include <atomic>
#include <mutex>

class QAudioSink;

namespace Mixed::Player {

    // QAudioSink 在拉模式下通过该 QIODevice 主动拉取已解码的 PCM 数据，
    // 并据此维护“已播放位置”作为主时钟。
    class AudioDevice : public QIODevice {
        Q_OBJECT

    public:
        explicit AudioDevice(int bytesPerSecond, QObject *parent = nullptr);

        void enqueue(const AudioFrame &frame);
        void clearBuffer();
        size_t queuedBytes() const;

        // 已交付给声卡的音频位置（秒）。
        double deliveredPts() const { return m_deliveredPts.load(); }

        // 拉模式为顺序设备。
        bool isSequential() const override { return true; }

    protected:
        qint64 readData(char *data, qint64 maxlen) override;
        qint64 writeData(const char *data, qint64 len) override;

    private:
        int m_bytesPerSecond;
        std::deque<AudioFrame> m_queue;
        mutable std::mutex m_mutex;
        std::atomic<size_t> m_queuedBytes{0};

        AudioFrame m_current;          // 正在消费的帧
        qint64 m_offset = 0;           // 当前帧已消费字节数
        std::atomic<double> m_deliveredPts{0.0};
    };

    class AudioOutput : public QObject {
        Q_OBJECT

    public:
        explicit AudioOutput(QObject *parent = nullptr);
        ~AudioOutput() override;

        // 以固定格式（S16 交错）启动声卡。解码端需重采样到该格式。
        bool start(int sampleRate = 44100, int channels = 2);
        void stop();

        // 由音频解码线程调用，提交一段已重采样的 PCM。
        void enqueue(const AudioFrame &frame);

        // 清空缓冲（停止/切换源时）。
        void clear();

        bool isRunning() const { return m_running; }
        int sampleRate() const { return m_sampleRate; }
        int channels() const { return m_channels; }
        int bytesPerSecond() const { return m_sampleRate * m_channels * 2; }

        // 主时钟：当前实际播放到的音频时间戳（秒）。
        // = 已交付声卡的 PTS - 声卡内部尚未播放的缓冲延迟。
        double masterClock() const;

        // 当前已缓冲（队列中尚未交付）音频时长（秒），用于解码端节流。
        double bufferedSeconds() const;

    private:
        QAudioSink *m_sink = nullptr;
        AudioDevice *m_device = nullptr;
        int m_sampleRate = 44100;
        int m_channels = 2;
        bool m_running = false;
    };

} // namespace Mixed::Player

#endif //MIXEDEDITING_AUDIOOUTPUT_H
