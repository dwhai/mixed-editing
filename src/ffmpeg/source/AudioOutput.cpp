//
// Created by Anlk on 2026/6/3.
// AudioOutput 实现（QAudioSink push 模式）。
//

#include "../include/AudioOutput.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QTimer>

namespace Mixed::Player {

    AudioOutput::AudioOutput(QObject *parent) : QObject(parent) {}

    AudioOutput::~AudioOutput() {
        stop();
    }

    bool AudioOutput::start(int sampleRate, int channels) {
        stop();

        m_sampleRate = sampleRate;
        m_channels = channels;

        QAudioFormat format;
        format.setSampleRate(sampleRate);
        format.setChannelCount(channels);
        format.setSampleFormat(QAudioFormat::Int16);

        QAudioDevice dev = QMediaDevices::defaultAudioOutput();
        if (dev.isNull()) {
            qWarning("AudioOutput: 没有可用的音频输出设备");
            return false;
        }
        if (!dev.isFormatSupported(format)) {
            QAudioFormat preferred = dev.preferredFormat();
            qWarning("AudioOutput: 设备不支持 %d Hz/%d ch/S16，回退到首选格式 %d Hz/%d ch",
                     sampleRate, channels, preferred.sampleRate(), preferred.channelCount());
        }

        m_sink = new QAudioSink(dev, format, this);
        m_sink->setBufferSize(bytesPerSecond() / 5); // ~200ms
        m_sink->setVolume(m_volume);

        m_started = false;
        m_current = AudioFrame{};
        m_offset = 0;
        m_queuedBytes = 0;

        // push 模式：start() 返回可写入的 QIODevice。
        m_io = m_sink->start();
        if (!m_io) {
            qWarning("AudioOutput: QAudioSink 启动失败");
            delete m_sink;
            m_sink = nullptr;
            return false;
        }

        m_feedTimer = new QTimer(this);
        m_feedTimer->setTimerType(Qt::PreciseTimer);
        m_feedTimer->setInterval(10);
        connect(m_feedTimer, &QTimer::timeout, this, &AudioOutput::feed);
        m_feedTimer->start();

        m_running = true;
        return true;
    }

    void AudioOutput::stop() {
        m_running = false;
        if (m_feedTimer) {
            m_feedTimer->stop();
            m_feedTimer->deleteLater();
            m_feedTimer = nullptr;
        }
        if (m_sink) {
            m_sink->stop();
            m_sink->deleteLater();
            m_sink = nullptr;
        }
        m_io = nullptr; // 归声卡所有，随声卡销毁
        clear();
        m_started = false;
    }

    void AudioOutput::setVolume(qreal volume) {
        m_volume = qBound(0.0, volume, 1.0);
        if (m_sink) m_sink->setVolume(m_volume);
    }

    void AudioOutput::setSpeed(double speed) {
        if (speed <= 0.0) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        // 在变速点把已播放部分按旧倍速结算进 base，之后用新倍速推进。
        if (m_sink && m_started.load()) {
            const qint64 p = m_sink->processedUSecs();
            m_clockBasePts += static_cast<double>(p - m_clockBaseUSecs) / 1e6 * m_speed;
            m_clockBaseUSecs = p;
        }
        m_speed = speed;
    }

    void AudioOutput::pause() {
        if (m_sink) m_sink->suspend();
        if (m_feedTimer) m_feedTimer->stop();
    }

    void AudioOutput::resume() {
        if (m_sink) m_sink->resume();
        if (m_feedTimer) m_feedTimer->start();
    }

    void AudioOutput::enqueue(const AudioFrame &frame) {
        if (frame.pcm.isEmpty()) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queuedBytes += static_cast<size_t>(frame.pcm.size());
        m_queue.push_back(frame);
    }

    void AudioOutput::clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.clear();
        m_current = AudioFrame{};
        m_offset = 0;
        m_queuedBytes = 0;
    }

    void AudioOutput::feed() {
        if (!m_sink || !m_io) return;

        int free = m_sink->bytesFree();
        if (free <= 0) return;

        QByteArray chunk;
        chunk.reserve(free);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            while (chunk.size() < free) {
                if (m_offset >= m_current.pcm.size()) {
                    if (m_queue.empty()) break;
                    m_current = m_queue.front();
                    m_queue.pop_front();
                    m_offset = 0;
                    if (!m_started) {
                        m_startPts = m_current.pts;
                        m_clockBasePts = m_startPts;
                        m_clockBaseUSecs = m_sink ? m_sink->processedUSecs() : 0;
                        m_started = true;
                    }
                }
                const int avail = static_cast<int>(m_current.pcm.size() - m_offset);
                const int n = qMin(avail, static_cast<int>(free - chunk.size()));
                chunk.append(m_current.pcm.constData() + m_offset, n);
                m_offset += n;
                m_queuedBytes -= static_cast<size_t>(n);
            }
        }

        if (!chunk.isEmpty()) {
            m_io->write(chunk);
        }
    }

    double AudioOutput::masterClock() const {
        if (!m_sink || !m_started.load()) return 0.0;
        // 源位置 = base + 自上次锚点以来已播放真实时长 × 倍速。
        const qint64 p = m_sink->processedUSecs();
        return m_clockBasePts + static_cast<double>(p - m_clockBaseUSecs) / 1e6 * m_speed;
    }

    double AudioOutput::bufferedSeconds() const {
        double secs = static_cast<double>(m_queuedBytes.load()) / bytesPerSecond();
        if (m_sink) {
            const qint64 inSink = m_sink->bufferSize() - m_sink->bytesFree();
            if (inSink > 0) secs += static_cast<double>(inSink) / bytesPerSecond();
        }
        return secs;
    }

} // namespace Mixed::Player
