//
// Created by Anlk on 2026/6/3.
// MediaPlayer 实现。
//

#include "../include/MediaPlayer.h"
#include "../include/AudioOutput.h"

#include <QStringList>
#include <QTimer>
#include <chrono>
#include <thread>

extern "C" {
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
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
        m_lastEmittedPos = -1.0;
        m_duration = 0.0;
        m_playing = true;

        {
            std::lock_guard<std::mutex> lock(m_subMutex);
            m_subCues.clear();
        }
        m_currentSubText.clear();
        emit subtitleChanged(QString());

        // 应用当前音量与倍速设置（跨视频保持）。
        m_audioSpeed.store(m_speed);
        m_rebuildFilter.store(false);
        if (m_audio) {
            m_audio->setVolume(m_volume);
            m_audio->setSpeed(m_speed);
        }

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
        m_subtitleStreamIndex = -1;
        m_playing = false;
        m_paused = false;
    }

    void MediaPlayer::pause() {
        if (!m_playing || m_paused) return;
        m_paused = true;
        if (m_videoTimer) m_videoTimer->stop();
        if (m_audio) m_audio->pause();
    }

    void MediaPlayer::resume() {
        if (!m_playing || !m_paused) return;
        m_paused = false;
        if (m_videoTimer) m_videoTimer->start();
        if (m_audio) m_audio->resume();
    }

    void MediaPlayer::setVolume(qreal volume) {
        m_volume = volume;
        if (m_audio) m_audio->setVolume(volume);
    }

    void MediaPlayer::setSpeed(double speed) {
        if (speed <= 0.0) return;

        // 无音频时，先按旧倍速结算视频时钟，保证变速点连续。
        if (!m_hasAudio && m_videoClockStarted) {
            m_videoClockBasePts += m_videoClock.elapsed() / 1000.0 * m_speed;
            m_videoClock.restart();
        }

        m_speed = speed;
        m_audioSpeed.store(speed);
        m_rebuildFilter.store(true); // 通知音频线程重建 atempo 滤镜
        if (m_audio) m_audio->setSpeed(speed);
    }

    void MediaPlayer::flushPacketQueue(ThreadSafeQueue<AVPacket *> &queue) {
        AVPacket *pkt = nullptr;
        while (queue.tryPop(pkt)) {
            if (pkt) av_packet_free(&pkt);
        }
    }

    void MediaPlayer::freeContexts() {
        freeAudioFilterGraph();
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
        if (m_subtitleCodecCtx) avcodec_free_context(&m_subtitleCodecCtx);
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
        m_subtitleStreamIndex = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_SUBTITLE, -1, -1, nullptr, 0);

        if (m_fmtCtx->duration > 0) {
            m_duration = static_cast<double>(m_fmtCtx->duration) / AV_TIME_BASE;
        }
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

    bool MediaPlayer::openSubtitleCodec() {
        if (m_subtitleStreamIndex < 0) return false;
        AVStream *stream = m_fmtCtx->streams[m_subtitleStreamIndex];
        const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) return false;

        m_subtitleCodecCtx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(m_subtitleCodecCtx, stream->codecpar);
        if (avcodec_open2(m_subtitleCodecCtx, codec, nullptr) < 0) {
            avcodec_free_context(&m_subtitleCodecCtx);
            return false;
        }
        m_subtitleTimeBase = av_q2d(stream->time_base);
        return true;
    }

    void MediaPlayer::freeAudioFilterGraph() {
        if (m_filterGraph) {
            avfilter_graph_free(&m_filterGraph);
            m_filterGraph = nullptr;
            m_filterSrc = nullptr;
            m_filterSink = nullptr;
        }
    }

    bool MediaPlayer::buildAudioFilterGraph(double speed) {
        freeAudioFilterGraph();
        if (!m_audioCodecCtx) return false;

        m_filterGraph = avfilter_graph_alloc();
        if (!m_filterGraph) return false;

        char chLayout[64] = {0};
        av_channel_layout_describe(&m_audioCodecCtx->ch_layout, chLayout, sizeof(chLayout));

        char args[256];
        snprintf(args, sizeof(args),
                 "time_base=1/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
                 m_audioCodecCtx->sample_rate, m_audioCodecCtx->sample_rate,
                 av_get_sample_fmt_name(m_audioCodecCtx->sample_fmt), chLayout);

        const AVFilter *abuffersrc = avfilter_get_by_name("abuffer");
        const AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
        const AVFilter *atempo = avfilter_get_by_name("atempo");
        if (!abuffersrc || !abuffersink || !atempo) {
            freeAudioFilterGraph();
            return false;
        }

        if (avfilter_graph_create_filter(&m_filterSrc, abuffersrc, "in",
                                         args, nullptr, m_filterGraph) < 0 ||
            avfilter_graph_create_filter(&m_filterSink, abuffersink, "out",
                                         nullptr, nullptr, m_filterGraph) < 0) {
            freeAudioFilterGraph();
            return false;
        }

        // 关键：atempo 只接受 packed float，会自动把解码帧(常为 fltp)转为 flt。
        // 这里强制 sink 输出回解码端的采样格式/采样率，确保与后续 swr 上下文一致，
        // 否则 swr 会按 planar 访问到空指针导致崩溃。
        const enum AVSampleFormat outSampleFmts[] = {m_audioCodecCtx->sample_fmt,
                                                     AV_SAMPLE_FMT_NONE};
        const int outSampleRates[] = {m_audioCodecCtx->sample_rate, -1};
        av_opt_set_int_list(m_filterSink, "sample_fmts", outSampleFmts,
                            AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
        av_opt_set_int_list(m_filterSink, "sample_rates", outSampleRates, -1,
                            AV_OPT_SEARCH_CHILDREN);

        char atempoArgs[64];
        snprintf(atempoArgs, sizeof(atempoArgs), "tempo=%.4f", speed);
        AVFilterContext *atempoCtx = nullptr;
        if (avfilter_graph_create_filter(&atempoCtx, atempo, "atempo",
                                         atempoArgs, nullptr, m_filterGraph) < 0) {
            freeAudioFilterGraph();
            return false;
        }

        if (avfilter_link(m_filterSrc, 0, atempoCtx, 0) < 0 ||
            avfilter_link(atempoCtx, 0, m_filterSink, 0) < 0 ||
            avfilter_graph_config(m_filterGraph, nullptr) < 0) {
            freeAudioFilterGraph();
            return false;
        }
        return true;
    }

    namespace {
        // 从 ASS Dialogue 行中提取纯文本：取第 9 个逗号之后的内容，
        // 去除 {\...} 覆盖标签，并把 \N / \n 还原为换行。
        QString plainTextFromAss(const QString &ass) {
            int comma = 0;
            int idx = 0;
            for (; idx < ass.size() && comma < 9; ++idx) {
                if (ass[idx] == QLatin1Char(',')) ++comma;
            }
            QString text = (comma == 9) ? ass.mid(idx) : ass;
            // 去除 {...} 覆盖标签。
            QString out;
            out.reserve(text.size());
            bool inBrace = false;
            for (int i = 0; i < text.size(); ++i) {
                const QChar c = text[i];
                if (c == QLatin1Char('{')) { inBrace = true; continue; }
                if (c == QLatin1Char('}')) { inBrace = false; continue; }
                if (inBrace) continue;
                if (c == QLatin1Char('\\') && i + 1 < text.size() &&
                    (text[i + 1] == QLatin1Char('N') || text[i + 1] == QLatin1Char('n'))) {
                    out.append(QLatin1Char('\n'));
                    ++i;
                    continue;
                }
                out.append(c);
            }
            return out.trimmed();
        }
    } // namespace

    void MediaPlayer::decodeSubtitlePacket(AVPacket *pkt) {
        if (!m_subtitleCodecCtx || !pkt) return;

        AVSubtitle sub;
        int got = 0;
        if (avcodec_decode_subtitle2(m_subtitleCodecCtx, &sub, &got, pkt) < 0 || !got) {
            return;
        }

        const double pktPts = (pkt->pts == AV_NOPTS_VALUE)
                                  ? 0.0
                                  : pkt->pts * m_subtitleTimeBase;
        SubtitleCue cue;
        cue.start = pktPts + sub.start_display_time / 1000.0;
        cue.end = pktPts + sub.end_display_time / 1000.0;
        if (cue.end <= cue.start) cue.end = cue.start + 5.0; // 兜底显示时长

        QStringList lines;
        for (unsigned i = 0; i < sub.num_rects; ++i) {
            const AVSubtitleRect *rect = sub.rects[i];
            if (!rect) continue;
            if (rect->ass) {
                lines << plainTextFromAss(QString::fromUtf8(rect->ass));
            } else if (rect->text) {
                lines << QString::fromUtf8(rect->text).trimmed();
            }
        }
        cue.text = lines.join(QLatin1Char('\n')).trimmed();
        avsubtitle_free(&sub);

        if (!cue.text.isEmpty()) {
            std::lock_guard<std::mutex> lock(m_subMutex);
            m_subCues.push_back(cue);
        }
    }

    void MediaPlayer::updateSubtitle(double master) {
        QString text;
        {
            std::lock_guard<std::mutex> lock(m_subMutex);
            for (const auto &cue : m_subCues) {
                if (master >= cue.start && master <= cue.end) {
                    text = cue.text;
                    break;
                }
            }
        }
        if (text != m_currentSubText) {
            m_currentSubText = text;
            emit subtitleChanged(text);
        }
    }

    void MediaPlayer::demuxThreadMain(QString url) {
        if (!openInput(url)) {
            m_playing = false;
            return;
        }

        m_hasVideo = openVideoCodec();
        m_hasAudio = openAudioCodec();
        openSubtitleCodec(); // 字幕可有可无，失败不影响播放

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
            } else if (pkt->stream_index == m_subtitleStreamIndex) {
                // 字幕包稀疏，直接在本线程解码后释放。
                decodeSubtitlePacket(pkt);
                av_packet_free(&pkt);
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
        AVFrame *frame = av_frame_alloc();   // 解码输出
        AVFrame *filt = av_frame_alloc();    // 变速滤镜输出
        const int bytesPerSample = 2 * kOutChannels; // S16 * 声道数

        // 解码线程独享滤镜图：在此构建，按倍速变化重建。
        buildAudioFilterGraph(m_audioSpeed.load());
        double sinkTb = m_filterSink ? av_q2d(av_buffersink_get_time_base(m_filterSink))
                                     : m_audioTimeBase;

        // swr 重采样 + 入队（节流避免内存堆积）。
        auto convertEnqueue = [&](AVFrame *f) {
            int64_t delay = swr_get_delay(m_swrCtx, m_audioCodecCtx->sample_rate);
            int outSamples = static_cast<int>(av_rescale_rnd(
                delay + f->nb_samples, kOutSampleRate,
                m_audioCodecCtx->sample_rate, AV_ROUND_UP));

            QByteArray pcm(outSamples * bytesPerSample, Qt::Uninitialized);
            uint8_t *outBuf = reinterpret_cast<uint8_t *>(pcm.data());
            int converted = swr_convert(m_swrCtx, &outBuf, outSamples,
                                        const_cast<const uint8_t **>(f->extended_data),
                                        f->nb_samples);
            if (converted > 0) {
                pcm.resize(converted * bytesPerSample);
                AudioFrame af;
                af.pcm = pcm;
                int64_t ts = f->best_effort_timestamp;
                if (ts == AV_NOPTS_VALUE) ts = f->pts;
                af.pts = (ts == AV_NOPTS_VALUE) ? 0.0 : ts * sinkTb;
                m_audio->enqueue(af);
            }
            while (!m_abort && m_audio->bufferedSeconds() > 0.5) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        };

        // 从 atempo 滤镜拉取所有就绪帧并送去重采样。
        auto drainFilter = [&]() {
            if (!m_filterSink) return;
            while (true) {
                int r = av_buffersink_get_frame(m_filterSink, filt);
                if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
                if (r < 0) break;
                convertEnqueue(filt);
                av_frame_unref(filt);
            }
        };

        auto decodeAvailable = [&]() {
            while (true) {
                int ret = avcodec_receive_frame(m_audioCodecCtx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) break;

                // 倍速变化：冲刷旧滤镜后重建（音调保持）。
                if (m_rebuildFilter.exchange(false)) {
                    if (m_filterSrc) (void) av_buffersrc_add_frame(m_filterSrc, nullptr);
                    drainFilter();
                    buildAudioFilterGraph(m_audioSpeed.load());
                    sinkTb = m_filterSink
                                 ? av_q2d(av_buffersink_get_time_base(m_filterSink))
                                 : m_audioTimeBase;
                }

                if (m_filterSrc &&
                    av_buffersrc_add_frame_flags(m_filterSrc, frame,
                                                 AV_BUFFERSRC_FLAG_KEEP_REF) >= 0) {
                    drainFilter();
                } else {
                    convertEnqueue(frame); // 滤镜不可用时直通
                }
                av_frame_unref(frame);
            }
        };

        while (!m_abort) {
            AVPacket *pkt = nullptr;
            if (!m_audioPackets.pop(pkt)) break;

            if (pkt == nullptr) {
                avcodec_send_packet(m_audioCodecCtx, nullptr);
                decodeAvailable();
                if (m_filterSrc) { (void) av_buffersrc_add_frame(m_filterSrc, nullptr); drainFilter(); }
                break;
            }
            if (avcodec_send_packet(m_audioCodecCtx, pkt) >= 0) {
                decodeAvailable();
            }
            av_packet_free(&pkt);
        }

        av_frame_free(&frame);
        av_frame_free(&filt);
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
            // 停滞外推也按倍速推进，保持与音频变速一致。
            return m_clockAnchorPts + m_anchorTimer.elapsed() / 1000.0 * m_speed;
        }
        // 无音频时退化为基于系统时间的视频时钟（按倍速推进）。
        if (!m_videoClockStarted) {
            return -1.0; // 尚未起钟
        }
        return m_videoClockBasePts + m_videoClock.elapsed() / 1000.0 * m_speed;
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

        // 进度上报（节流到约 4 次/秒）与字幕刷新。
        if (master >= 0.0) {
            if (m_lastEmittedPos < 0.0 || master - m_lastEmittedPos >= 0.25 ||
                master < m_lastEmittedPos) {
                m_lastEmittedPos = master;
                emit positionChanged(master, m_duration);
            }
            updateSubtitle(master);
        }

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
