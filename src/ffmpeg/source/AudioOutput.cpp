//
// Created by Anlk on 2026/6/3.
// AudioOutput / AudioDevice 实现。
//

#include "../include/AudioOutput.h"

#include <QAudioSink>
#include <QMediaDevices>
#include <cstring>

namespace Mixed::Player {

    // ============================================
    // AudioDevice
    // ============================================

    AudioDevice::AudioDevice(int bytesPerSecond, QObject *parent)
        : QIODevice(parent), m_bytesPerSecond(bytesPerSecond) {}

    void AudioDevice::enqueue(const AudioFrame &frame) {
        if (frame.pcm.isEmpty()) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queuedBytes += static_cast<size_t>(frame.pcm.size());
        m_queue.push_back(frame);
    }

    void AudioDevice::clearBuffer() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.clear();
        m_current = AudioFrame{};
        m_offset = 0;
        m_queuedBytes = 0;
        m_deliveredPts = 0.0;
    }

    size_t AudioDevice::queuedBytes() const {
        return m_queuedBytes.load();
    }

    qint64 AudioDevice::writeData(const char *, qint64) {
        return 0; // 只读设备
    }

    qint64 AudioDevice::readData(char *data, qint64 maxlen) {
        qint64 written = 0;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            while (written < maxlen) {
                if (m_offset >= m_current.pcm.size()) {
                    if (m_queue.empty()) {
                        break; // 欠载
                    }
                    m_current = m_queue.front();
                    m_queue.pop_front();
                    m_offset = 0;
                }
                const qint64 avail = m_current.pcm.size() - m_offset;
                const qint64 n = qMin<qint64>(avail, maxlen - written);
                std::memcpy(data + written,
                            m_current.pcm.constData() + m_offset,
                            static_cast<size_t>(n));
                m_offset += n;
                written += n;
                m_queuedBytes -= static_cast<size_t>(n);
            }

            if (written > 0 && m_bytesPerSecond > 0) {
                // 已交付声卡的位置 = 当前帧起始 PTS + 帧内已消费时长。
                m_deliveredPts = m_current.pts +
                                 static_cast<double>(m_offset) / m_bytesPerSecond;
            }
        }

        // 欠载时用静音补齐，保持声卡持续拉取（主时钟不因静音前进）。
        if (written < maxlen) {
            std::memset(data + written, 0, static_cast<size_t>(maxlen - written));
        }
        return maxlen;
    }

    // ============================================
    // AudioOutput
    // ============================================

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

        const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
        if (dev.isNull()) {
            qWarning("AudioOutput: 没有可用的音频输出设备");
            return false;
        }
        if (!dev.isFormatSupported(format)) {
            qWarning("AudioOutput: 设备不支持 %d Hz / %d ch / S16，尝试使用首选格式",
                     sampleRate, channels);
        }

        m_sink = new QAudioSink(dev, format, this);
        // 适中的缓冲，兼顾延迟与稳定性（约 200ms）。
        m_sink->setBufferSize(bytesPerSecond() / 5);

        m_device = new AudioDevice(bytesPerSecond(), this);
        m_device->open(QIODevice::ReadOnly);
        m_sink->start(m_device);

        m_running = true;
        return true;
    }

    void AudioOutput::stop() {
        m_running = false;
        if (m_sink) {
            m_sink->stop();
            m_sink->deleteLater();
            m_sink = nullptr;
        }
        if (m_device) {
            m_device->close();
            m_device->deleteLater();
            m_device = nullptr;
        }
    }

    void AudioOutput::enqueue(const AudioFrame &frame) {
        if (m_device) m_device->enqueue(frame);
    }

    void AudioOutput::clear() {
        if (m_device) m_device->clearBuffer();
    }

    double AudioOutput::masterClock() const {
        if (!m_device || !m_sink) return 0.0;
        const double delivered = m_device->deliveredPts();
        // 声卡内部尚未播放的字节对应的延迟。
        const qint64 buffered = m_sink->bufferSize() - m_sink->bytesFree();
        const double latency = buffered > 0
                               ? static_cast<double>(buffered) / bytesPerSecond()
                               : 0.0;
        return delivered - latency;
    }

    double AudioOutput::bufferedSeconds() const {
        if (!m_device) return 0.0;
        return static_cast<double>(m_device->queuedBytes()) / bytesPerSecond();
    }

} // namespace Mixed::Player
