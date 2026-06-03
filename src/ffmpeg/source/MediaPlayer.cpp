//
// Created by Anlk on 2026/6/3.
// MediaPlayer 实现。
//

#include "../include/MediaPlayer.h"
#include "../include/AudioOutput.h"

#include <QTimer>
#include <chrono>
#include <thread>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

namespace Mixed::Player {

    namespace {
        QString avErr(int code) {
            char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(code, buf, sizeof(buf));
            return QString::fromUtf8(buf);
        }
    } // namespace

    MediaPlayer::MediaPlayer(QObject *parent) : QObject(parent) {
        m_audio = new AudioOutput(this);

        m_videoTimer = new QTimer(this);
        m_videoTimer->setTimerType(Qt::PreciseTimer);
        m_videoTimer->setInterval(4); // ~250Hz 轮询，平滑送显
        connect(m_videoTimer, &QTimer::timeout, this, &MediaPlayer::onVideoTick);
    }

    MediaPlayer::~MediaPlayer() {
        stop();
    }

    void MediaPlayer::play(const QString &url) {
        stop();

        m_abort = false;
        m_videoPackets.reset();
        m_audioPackets.reset();
        m_videoFrames.reset();
        m_videoClockStarted = false;
        m_anchorInit = false;
        m_lastAudioClock = -1.0;
        m_playing = true;

        // 解封装与解码线程的创建放在解封装线程内完成（避免网络打开阻塞 GUI）。
        m_demuxThread = std::thread([this, url] { demuxThreadMain(url); });
    }

    void MediaPlayer::stop() {
        if (!m_playing && !m_demuxThread.joinable()) {
            return;
        }
        m_abort = true;

        // 唤醒所有可能阻塞的线程。
        m_videoPackets.abort();
        m_audioPackets.abort();
        m_videoFrames.abort();

        if (m_demuxThread.joinable()) m_demuxThread.join();
        if (m_videoThread.joinable()) m_videoThread.join();
        if (m_audioThread.joinable()) m_audioThread.join();

        if (m_videoTimer) m_videoTimer->stop();
        if (m_audio) {
            m_audio->stop();
            m_audio->clear();
        }

        // 清空并释放残留 packet。
        flushPacketQueue(m_videoPackets);
        flushPacketQueue(m_audioPackets);

        freeContexts();

        emit cleared();

        m_hasAudio = false;
        m_hasVideo = false;
        m_videoStreamIndex = -1;
        m_audioStreamIndex = -1;
        m_playing = false;
    }

    void MediaPlayer::flushPacketQueue(ThreadSafeQueue<AVPacket *> &queue) {
        AVPacket *pkt = nullptr;
        while (queue.tryPop(pkt)) {
            if (pkt) av_packet_free(&pkt);
        }
    }

    void MediaPlayer::freeContexts() {
        if (m_swsCtx) {
            sws_freeContext(m_swsCtx);
            m_swsCtx = nullptr;
        }
        if (m_swrCtx) {
            swr_free(&m_swrCtx);
            m_swrCtx = nullptr;
        }
        if (m_videoCodecCtx) avcodec_free_context(&m_videoCodecCtx);
        if (m_audioCodecCtx) avcodec_free_context(&m_audioCodecCtx);
        if (m_fmtCtx) avformat_close_input(&m_fmtCtx);
    }

    bool MediaPlayer::openInput(const QString &url) {
        AVDictionary *opts = nullptr;
        // 网络流的连接/读取超时与重连设置，避免长时间卡死。
        av_dict_set(&opts, "timeout", "5000000", 0); // 5s（微秒）
        av_dict_set(&opts, "user_agent", "MixedEditing/1.0", 0);
        av_dict_set(&opts, "reconnect", "1", 0);
        av_dict_set(&opts, "reconnect_streamed", "1", 0);

        int ret = avformat_open_input(&m_fmtCtx, url.toUtf8().constData(), nullptr, &opts);
        av_dict_free(&opts);
        if (ret < 0) {
            emit errorOccurred(QStringLiteral("打开媒体失败: ") + avErr(ret));
            return false;
        }

        ret = avformat_find_stream_info(m_fmtCtx, nullptr);
        if (ret < 0) {
            emit errorOccurred(QStringLiteral("解析流信息失败: ") + avErr(ret));
            return false;
        }

        m_videoStreamIndex = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        m_audioStreamIndex = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        return m_videoStreamIndex >= 0 || m_audioStreamIndex >= 0;
    }

    bool MediaPlayer::openVideoCodec() {
        if (m_videoStreamIndex < 0) return false;
        AVStream *stream = m_fmtCtx->streams[m_videoStreamIndex];
        const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) return false;

        m_videoCodecCtx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(m_videoCodecCtx, stream->codecpar);
        m_videoCodecCtx->thread_count = 0; // 自动多线程解码
        if (avcodec_open2(m_videoCodecCtx, codec, nullptr) < 0) {
            avcodec_free_context(&m_videoCodecCtx);
            return false;
        }
        m_videoTimeBase = av_q2d(stream->time_base);
        return true;
    }

    bool MediaPlayer::openAudioCodec() {
        if (m_audioStreamIndex < 0) return false;
        AVStream *stream = m_fmtCtx->streams[m_audioStreamIndex];
        const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) return false;

        m_audioCodecCtx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(m_audioCodecCtx, stream->codecpar);
        if (avcodec_open2(m_audioCodecCtx, codec, nullptr) < 0) {
            avcodec_free_context(&m_audioCodecCtx);
            return false;
        }
        m_audioTimeBase = av_q2d(stream->time_base);

        // 初始化重采样：源格式 -> S16 交错 / 44100 / 立体声。
        AVChannelLayout outLayout;
        av_channel_layout_default(&outLayout, kOutChannels);
        int ret = swr_alloc_set_opts2(&m_swrCtx,
                                       &outLayout, AV_SAMPLE_FMT_S16, kOutSampleRate,
                                       &m_audioCodecCtx->ch_layout,
                                       m_audioCodecCtx->sample_fmt,
                                       m_audioCodecCtx->sample_rate,
                                       0, nullptr);
        av_channel_layout_uninit(&outLayout);
        if (ret < 0 || swr_init(m_swrCtx) < 0) {
            emit errorOccurred(QStringLiteral("初始化音频重采样失败"));
            return false;
        }
        return true;
    }

    void MediaPlayer::demuxThreadMain(QString url) {
        if (!openInput(url)) {
            m_playing = false;
            return;
        }

        m_hasVideo = openVideoCodec();
        m_hasAudio = openAudioCodec();

        if (!m_hasVideo && !m_hasAudio) {
            emit errorOccurred(QStringLiteral("没有可解码的音视频流"));
            m_playing = false;
            return;
        }

        // 启动解码线程。
        if (m_hasVideo) m_videoThread = std::thread([this] { videoThreadMain(); });
        if (m_hasAudio) m_audioThread = std::thread([this] { audioThreadMain(); });

        // 通知 GUI 线程启动声卡与送显定时器。
        QMetaObject::invokeMethod(this, "onStreamsReady",
                                  Qt::QueuedConnection,
                                  Q_ARG(bool, m_hasAudio),
                                  Q_ARG(bool, m_hasVideo));
        emit started();

        // 读取并分流 packet。
        while (!m_abort) {
            AVPacket *pkt = av_packet_alloc();
            int ret = av_read_frame(m_fmtCtx, pkt);
            if (ret < 0) {
                av_packet_free(&pkt);
                break; // EOF 或出错
            }
            if (pkt->stream_index == m_videoStreamIndex) {
                if (!m_videoPackets.push(pkt)) { av_packet_free(&pkt); break; }
            } else if (pkt->stream_index == m_audioStreamIndex) {
                if (!m_audioPackets.push(pkt)) { av_packet_free(&pkt); break; }
            } else {
                av_packet_free(&pkt);
            }
        }

        // 推入 EOF 哨兵（nullptr），让解码线程冲刷并退出。
        if (m_hasVideo) m_videoPackets.push(nullptr);
        if (m_hasAudio) m_audioPackets.push(nullptr);
    }

    void MediaPlayer::videoThreadMain() {
        AVFrame *frame = av_frame_alloc();

        auto decodeAvailable = [&]() {
            while (true) {
                int ret = avcodec_receive_frame(m_videoCodecCtx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) break;

                const int w = m_videoCodecCtx->width;
                const int h = m_videoCodecCtx->height;
                m_swsCtx = sws_getCachedContext(m_swsCtx, w, h,
                                                static_cast<AVPixelFormat>(frame->format),
                                                w, h, AV_PIX_FMT_YUV420P,
                                                SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (!m_swsCtx) { av_frame_unref(frame); continue; }

                const int cw = w / 2;
                const int ch = h / 2;
                VideoFrame vf;
                vf.width = w;
                vf.height = h;
                vf.y.resize(w * h);
                vf.u.resize(cw * ch);
                vf.v.resize(cw * ch);

                uint8_t *dst[4] = {
                    reinterpret_cast<uint8_t *>(vf.y.data()),
                    reinterpret_cast<uint8_t *>(vf.u.data()),
                    reinterpret_cast<uint8_t *>(vf.v.data()),
                    nullptr};
                int dstLinesize[4] = {w, cw, cw, 0};
                sws_scale(m_swsCtx, frame->data, frame->linesize, 0, h, dst, dstLinesize);

                int64_t ts = frame->best_effort_timestamp;
                if (ts == AV_NOPTS_VALUE) ts = frame->pts;
                vf.pts = (ts == AV_NOPTS_VALUE) ? 0.0 : ts * m_videoTimeBase;

                av_frame_unref(frame);
                if (!m_videoFrames.push(vf)) return; // 被 abort
            }
        };

        while (!m_abort) {
            AVPacket *pkt = nullptr;
            if (!m_videoPackets.pop(pkt)) break;

            if (pkt == nullptr) {
                avcodec_send_packet(m_videoCodecCtx, nullptr); // 冲刷
                decodeAvailable();
                break;
            }
            if (avcodec_send_packet(m_videoCodecCtx, pkt) >= 0) {
                decodeAvailable();
            }
            av_packet_free(&pkt);
        }

        av_frame_free(&frame);
    }

    void MediaPlayer::audioThreadMain() {
        AVFrame *frame = av_frame_alloc();
        const int bytesPerSample = 2 * kOutChannels; // S16 * 声道数

        auto decodeAvailable = [&]() {
            while (true) {
                int ret = avcodec_receive_frame(m_audioCodecCtx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) break;

                // 计算输出样本数上限。
                int64_t delay = swr_get_delay(m_swrCtx, m_audioCodecCtx->sample_rate);
                int outSamples = static_cast<int>(av_rescale_rnd(
                    delay + frame->nb_samples, kOutSampleRate,
                    m_audioCodecCtx->sample_rate, AV_ROUND_UP));

                QByteArray pcm(outSamples * bytesPerSample, Qt::Uninitialized);
                uint8_t *outBuf = reinterpret_cast<uint8_t *>(pcm.data());
                int converted = swr_convert(m_swrCtx, &outBuf, outSamples,
                                            const_cast<const uint8_t **>(frame->extended_data),
                                            frame->nb_samples);
                if (converted > 0) {
                    pcm.resize(converted * bytesPerSample);
                    AudioFrame af;
                    af.pcm = pcm;
                    int64_t ts = frame->best_effort_timestamp;
                    if (ts == AV_NOPTS_VALUE) ts = frame->pts;
                    af.pts = (ts == AV_NOPTS_VALUE) ? 0.0 : ts * m_audioTimeBase;
                    m_audio->enqueue(af);
                }
                av_frame_unref(frame);

                // 节流：缓冲超过 0.5s 时稍作等待，避免无限堆积内存。
                while (!m_abort && m_audio->bufferedSeconds() > 0.5) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        };

        while (!m_abort) {
            AVPacket *pkt = nullptr;
            if (!m_audioPackets.pop(pkt)) break;

            if (pkt == nullptr) {
                avcodec_send_packet(m_audioCodecCtx, nullptr);
                decodeAvailable();
                break;
            }
            if (avcodec_send_packet(m_audioCodecCtx, pkt) >= 0) {
                decodeAvailable();
            }
            av_packet_free(&pkt);
        }

        av_frame_free(&frame);
    }

    void MediaPlayer::onStreamsReady(bool hasAudio, bool hasVideo) {
        if (m_abort) return;
        if (hasAudio) {
            m_audio->start(kOutSampleRate, kOutChannels);
        }
        if (hasVideo) {
            m_videoTimer->start();
        }
    }

    double MediaPlayer::currentMasterClock() {
        // 同步策略：音频为主时钟。
        if (m_hasAudio && m_audio->isRunning()) {
            const double a = m_audio->masterClock();
            // 音频时钟前进时重新锚定（音频即主钟）；停滞时用墙钟外推，
            // 既能平滑显示，又能避免音频短暂停滞造成整个管线死锁。
            if (!m_anchorInit || a > m_lastAudioClock + 1e-4) {
                m_lastAudioClock = a;
                m_clockAnchorPts = a;
                m_anchorTimer.restart();
                m_anchorInit = true;
            }
            return m_clockAnchorPts + m_anchorTimer.elapsed() / 1000.0;
        }
        // 无音频时退化为基于系统时间的视频时钟。
        if (!m_videoClockStarted) {
            return -1.0; // 尚未起钟
        }
        return m_videoClockBasePts + m_videoClock.elapsed() / 1000.0;
    }

    void MediaPlayer::onVideoTick() {
        if (!m_hasVideo) return;

        // 无音频：用首帧的 PTS 起钟。
        if (!m_hasAudio && !m_videoClockStarted) {
            VideoFrame f;
            if (m_videoFrames.peek(f)) {
                m_videoClockStarted = true;
                m_videoClockBasePts = f.pts;
                m_videoClock.start();
            } else {
                return;
            }
        }

        const double master = currentMasterClock();

        VideoFrame toShow;
        bool show = false;
        VideoFrame f;
        while (m_videoFrames.peek(f)) {
            if (f.pts > master + 0.005) {
                break; // 还没到显示时间
            }
            // 到点（或已迟到）：取出，保留最新的一帧用于显示，丢弃更早的。
            m_videoFrames.tryPop(toShow);
            show = true;
        }

        if (show) {
            emit frameReady(toShow);
        } else if (m_videoFrames.empty() && !m_playing) {
            emit finished();
        }
    }

} // namespace Mixed::Player
