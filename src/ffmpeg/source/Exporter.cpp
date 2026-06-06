//
// Created by Anlk on 2026/6/6.
// Exporter 实现。
//

#include "../include/Exporter.h"
#include "../include/CompositionEngine.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <thread>
#include <unordered_map>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace Mixed::Player {

    namespace {
        constexpr int    kAudioRate = 48000;   // 导出音频采样率
        constexpr int    kAudioCh   = 2;       // 立体声
        constexpr qint64 kUsPerSec  = 1'000'000;

        // 把时间线音频混成一条整序列的交错 float 立体声缓冲。
        // 返回总帧数（每帧含 kAudioCh 个 sample）。失败/无音频返回空。
        std::vector<float> mixAudio(const Exporter::Request &req,
                                    std::atomic<bool> &cancelled) {
            const qint64 totalSamples =
                static_cast<qint64>(std::ceil(req.durationUs / 1'000'000.0 * kAudioRate));
            if (totalSamples <= 0) {
                return {};
            }
            std::vector<float> mix(static_cast<size_t>(totalSamples) * kAudioCh, 0.0f);
            bool any = false;

            for (const DB::Track &track : req.tracks) {
                if (track.trackType != QStringLiteral("audio")) {
                    continue;
                }
                auto clipsIt = req.clipsByTrack.constFind(track.id);
                if (clipsIt == req.clipsByTrack.constEnd()) continue;

                for (const DB::Clip &clip : clipsIt.value()) {
                    if (cancelled.load()) return {};
                    auto pathIt = req.assetPathByClip.constFind(clip.id);
                    if (pathIt == req.assetPathByClip.constEnd() || pathIt.value().isEmpty()) {
                        continue;
                    }
                    const QByteArray path = pathIt.value().toUtf8();

                    AVFormatContext *fmt = nullptr;
                    if (avformat_open_input(&fmt, path.constData(), nullptr, nullptr) != 0) continue;
                    if (avformat_find_stream_info(fmt, nullptr) < 0) { avformat_close_input(&fmt); continue; }

                    int aIdx = -1;
                    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
                        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) { aIdx = (int)i; break; }
                    }
                    if (aIdx < 0) { avformat_close_input(&fmt); continue; }

                    AVStream *st = fmt->streams[aIdx];
                    const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
                    AVCodecContext *ctx = dec ? avcodec_alloc_context3(dec) : nullptr;
                    if (!ctx || avcodec_parameters_to_context(ctx, st->codecpar) < 0 ||
                        avcodec_open2(ctx, dec, nullptr) < 0) {
                        if (ctx) avcodec_free_context(&ctx);
                        avformat_close_input(&fmt);
                        continue;
                    }

                    // 重采样到 48k 立体声 float（交错）。
                    SwrContext *swr = nullptr;
                    AVChannelLayout outLayout;
                    av_channel_layout_default(&outLayout, kAudioCh);
                    swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, kAudioRate,
                                        &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate, 0, nullptr);
                    if (!swr || swr_init(swr) < 0) {
                        if (swr) swr_free(&swr);
                        av_channel_layout_uninit(&outLayout);
                        avcodec_free_context(&ctx);
                        avformat_close_input(&fmt);
                        continue;
                    }

                    // seek 到片段源起点。
                    const int64_t seekTs = av_rescale_q(clip.sourceInUs, AVRational{1, 1000000}, st->time_base);
                    av_seek_frame(fmt, aIdx, seekTs, AVSEEK_FLAG_BACKWARD);
                    avcodec_flush_buffers(ctx);

                    AVPacket *pkt = av_packet_alloc();
                    AVFrame *frame = av_frame_alloc();
                    std::vector<float> tmp;
                    bool done = false;

                    while (!done && av_read_frame(fmt, pkt) >= 0) {
                        if (pkt->stream_index == aIdx && avcodec_send_packet(ctx, pkt) == 0) {
                            while (avcodec_receive_frame(ctx, frame) == 0) {
                                if (cancelled.load()) { done = true; break; }
                                const int64_t bestPts = (frame->best_effort_timestamp != AV_NOPTS_VALUE)
                                    ? frame->best_effort_timestamp : frame->pts;
                                const qint64 framePtsUs = (bestPts == AV_NOPTS_VALUE) ? clip.sourceInUs
                                    : av_rescale_q(bestPts, st->time_base, AVRational{1, 1000000});
                                if (framePtsUs >= clip.sourceOutUs) { done = true; break; }

                                const int maxOut = swr_get_out_samples(swr, frame->nb_samples);
                                tmp.resize(static_cast<size_t>(std::max(0, maxOut)) * kAudioCh);
                                uint8_t *outPtr = reinterpret_cast<uint8_t *>(tmp.data());
                                const int got = swr_convert(swr, &outPtr, maxOut,
                                    const_cast<const uint8_t **>(frame->data), frame->nb_samples);

                                // 该帧首样本在时间线上的位置（源内偏移 + 片段时间线起点）。
                                const qint64 srcOffUs = framePtsUs - clip.sourceInUs;
                                const qint64 tlUs = clip.timelineStartUs + srcOffUs;
                                qint64 startSample = static_cast<qint64>(
                                    std::llround(tlUs / 1'000'000.0 * kAudioRate));

                                for (int s = 0; s < got; ++s) {
                                    const qint64 dst = startSample + s;
                                    if (dst < 0 || dst >= totalSamples) continue;
                                    // 叠加混音（多轨/重叠相加，最后统一限幅）。
                                    mix[static_cast<size_t>(dst) * kAudioCh + 0] += tmp[s * kAudioCh + 0];
                                    mix[static_cast<size_t>(dst) * kAudioCh + 1] += tmp[s * kAudioCh + 1];
                                }
                                any = true;
                            }
                        }
                        av_packet_unref(pkt);
                    }

                    av_frame_free(&frame);
                    av_packet_free(&pkt);
                    swr_free(&swr);
                    av_channel_layout_uninit(&outLayout);
                    avcodec_free_context(&ctx);
                    avformat_close_input(&fmt);
                }
            }

            if (!any) return {};
            // 软限幅，避免叠加溢出。
            for (float &v : mix) v = std::clamp(v, -1.0f, 1.0f);
            return mix;
        }
    } // namespace

    namespace {
        // 选择 H.264 编码器：优先硬件 VideoToolbox（macOS 媒体引擎），回退 libx264。
        // 用一次试开探测可用性，避免分段时某段才失败。返回编码器名；hwOut 标记是否硬件。
        const char *pickH264Encoder(int W, int H, int FPS, bool &hwOut) {
            const char *vt = "h264_videotoolbox";
            const AVCodec *c = avcodec_find_encoder_by_name(vt);
            if (c) {
                AVCodecContext *probe = avcodec_alloc_context3(c);
                if (probe) {
                    probe->width = W; probe->height = H;
                    probe->time_base = AVRational{1, FPS};
                    probe->pix_fmt = AV_PIX_FMT_YUV420P;
                    probe->bit_rate = 6'000'000;
                    av_opt_set(probe->priv_data, "allow_sw", "1", 0);
                    const bool okOpen = avcodec_open2(probe, c, nullptr) >= 0;
                    avcodec_free_context(&probe);
                    if (okOpen) { hwOut = true; return vt; }
                }
            }
            hwOut = false;
            return "libx264";
        }

        // 配置并打开一个 H.264 编码器上下文。
        AVCodecContext *openH264Encoder(const char *name, bool hardware,
                                        int W, int H, int FPS, int threadCount,
                                        bool globalHeader) {
            const AVCodec *codec = avcodec_find_encoder_by_name(name);
            if (!codec) return nullptr;
            AVCodecContext *ctx = avcodec_alloc_context3(codec);
            if (!ctx) return nullptr;
            ctx->width = W;
            ctx->height = H;
            ctx->time_base = AVRational{1, FPS};
            ctx->framerate = AVRational{FPS, 1};
            ctx->pix_fmt = AV_PIX_FMT_YUV420P;
            ctx->bit_rate = 6'000'000;
            ctx->gop_size = FPS;
            ctx->max_b_frames = 0;   // 无 B 帧：分段 stream-copy 拼接时 pts==dts，顺序简单
            if (globalHeader) ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            if (hardware) {
                av_opt_set(ctx->priv_data, "allow_sw", "1", 0);    // 硬件不可用时允许软回退
                av_opt_set(ctx->priv_data, "realtime", "1", 0);
            } else {
                ctx->thread_count = threadCount;
                ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
                av_opt_set(ctx->priv_data, "preset", "veryfast", 0);
                av_opt_set(ctx->priv_data, "tune", "fastdecode", 0);
            }
            if (avcodec_open2(ctx, codec, nullptr) < 0) {
                avcodec_free_context(&ctx);
                return nullptr;
            }
            return ctx;
        }

        // 把一帧 YUV420P（VideoFrame）拷进编码 AVFrame。
        void fillVideoFrame(AVFrame *vf, const VideoFrame &f, int W, int H, qint64 pts) {
            av_frame_make_writable(vf);
            if (f.valid() && f.width == W && f.height == H) {
                const int cw = W / 2, ch = H / 2;
                for (int y = 0; y < H; ++y)
                    memcpy(vf->data[0] + y * vf->linesize[0], f.y.constData() + y * W, W);
                for (int y = 0; y < ch; ++y) {
                    memcpy(vf->data[1] + y * vf->linesize[1], f.u.constData() + y * cw, cw);
                    memcpy(vf->data[2] + y * vf->linesize[2], f.v.constData() + y * cw, cw);
                }
            } else {
                memset(vf->data[0], 16, vf->linesize[0] * H);
                memset(vf->data[1], 128, vf->linesize[1] * (H / 2));
                memset(vf->data[2], 128, vf->linesize[2] * (H / 2));
            }
            vf->pts = pts;
        }

        // 编码一个连续帧区间 [startFrame, endFrame) 到独立的临时 MP4 片段。
        // 自带 CompositionEngine（顺序解码该区间，仅区间起点一次 seek）。
        // 每完成一帧 framesDone 自增（供主线程汇报进度）。
        bool encodeSegment(const QString &segPath, int W, int H, int FPS,
                           qint64 startFrame, qint64 endFrame,
                           const std::vector<DB::Track> &tracks,
                           const std::unordered_map<QString, std::vector<DB::Clip>> &clipsMap,
                           const std::unordered_map<QString, QString> &pathMap,
                           const char *encName, bool hardware, int threadCount,
                           std::atomic<qint64> &framesDone, std::atomic<bool> &cancelled) {
            const QByteArray outPath = segPath.toUtf8();
            AVFormatContext *oc = nullptr;
            avformat_alloc_output_context2(&oc, nullptr, nullptr, outPath.constData());
            if (!oc) return false;

            AVStream *vst = avformat_new_stream(oc, nullptr);
            const bool globalHeader = (oc->oformat->flags & AVFMT_GLOBALHEADER) != 0;
            AVCodecContext *vctx = openH264Encoder(encName, hardware, W, H, FPS,
                                                   threadCount, globalHeader);
            if (!vctx) { avformat_free_context(oc); return false; }
            avcodec_parameters_from_context(vst->codecpar, vctx);
            vst->time_base = vctx->time_base;

            if (!(oc->oformat->flags & AVFMT_NOFILE)) {
                if (avio_open(&oc->pb, outPath.constData(), AVIO_FLAG_WRITE) < 0) {
                    avcodec_free_context(&vctx); avformat_free_context(oc); return false;
                }
            }
            if (avformat_write_header(oc, nullptr) < 0) {
                if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
                avcodec_free_context(&vctx); avformat_free_context(oc); return false;
            }

            CompositionEngine engine;
            engine.setCanvasSize(W, H);
            engine.setTimeline(tracks, clipsMap, pathMap);

            AVFrame *vframe = av_frame_alloc();
            vframe->format = AV_PIX_FMT_YUV420P;
            vframe->width = W;
            vframe->height = H;
            av_frame_get_buffer(vframe, 32);
            AVPacket *pkt = av_packet_alloc();

            auto drain = [&](AVFrame *fr) -> bool {
                if (avcodec_send_frame(vctx, fr) < 0) return false;
                while (true) {
                    int r = avcodec_receive_packet(vctx, pkt);
                    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
                    if (r < 0) return false;
                    av_packet_rescale_ts(pkt, vctx->time_base, vst->time_base);
                    pkt->stream_index = vst->index;
                    av_interleaved_write_frame(oc, pkt);
                    av_packet_unref(pkt);
                }
                return true;
            };

            bool ok = true;
            for (qint64 i = startFrame; i < endFrame; ++i) {
                if (cancelled.load()) { ok = false; break; }
                const qint64 tUs = static_cast<qint64>(i * kUsPerSec / FPS);
                VideoFrame f = engine.composeAt(tUs);
                fillVideoFrame(vframe, f, W, H, i - startFrame); // 段内本地 pts 从 0 起
                if (!drain(vframe)) { ok = false; break; }
                framesDone.fetch_add(1);
            }
            if (ok) drain(nullptr);          // 冲洗
            if (ok) av_write_trailer(oc);

            av_frame_free(&vframe);
            av_packet_free(&pkt);
            avcodec_free_context(&vctx);
            if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
            avformat_free_context(oc);
            return ok;
        }
    } // namespace

    Exporter::Exporter(QObject *parent) : QObject(parent) {}
    Exporter::~Exporter() = default;

    void Exporter::run() {
        const Request req = m_req;
        auto fail = [&](const QString &msg) { emit finished(false, msg); };

        if (req.outputPath.isEmpty() || req.durationUs <= 0) {
            fail(QStringLiteral("时间线为空，无可导出的内容。"));
            return;
        }
        const int W = req.width > 0 ? req.width : 1920;
        const int H = req.height > 0 ? req.height : 1080;
        const int FPS = req.fps > 0 ? req.fps : 30;
        const qint64 totalFrames =
            std::max<qint64>(1, static_cast<qint64>(std::ceil(req.durationUs / 1'000'000.0 * FPS)));

        // 时间线 map（供各段引擎复用）。
        std::unordered_map<QString, std::vector<DB::Clip>> clipsMap;
        for (auto it = req.clipsByTrack.constBegin(); it != req.clipsByTrack.constEnd(); ++it) {
            clipsMap.emplace(it.key(), it.value());
        }
        std::unordered_map<QString, QString> pathMap;
        for (auto it = req.assetPathByClip.constBegin(); it != req.assetPathByClip.constEnd(); ++it) {
            pathMap.emplace(it.key(), it.value());
        }

        // 选编码器（硬件优先）。
        bool hardware = false;
        const char *encName = pickH264Encoder(W, H, FPS, hardware);

        // 并行度：按核心数；硬件编码会话较省 CPU，可多分几段；软件编码每段内部已多线程，
        // 段数适度避免过度订阅。
        unsigned hw = std::thread::hardware_concurrency();
        const int cores = static_cast<int>(hw == 0 ? 4 : hw);
        int poolSize = hardware ? std::clamp(cores, 1, 8) : std::clamp(cores / 2, 1, 4);
        // 帧数太少不必分太多段。
        poolSize = static_cast<int>(std::min<qint64>(poolSize, std::max<qint64>(1, totalFrames / 30)));
        if (poolSize < 1) poolSize = 1;

        // ---- 音频混音（主流程先做，与视频段并行起跑前完成；通常远快于视频） ----
        std::vector<float> mixed = mixAudio(req, m_cancelled);
        const bool hasAudio = !mixed.empty();
        if (m_cancelled.load()) { fail(QStringLiteral("已取消导出。")); return; }

        // ---- 临时目录，分段并行编码 ----
        QTemporaryDir tmpDir;
        if (!tmpDir.isValid()) { fail(QStringLiteral("无法创建临时目录。")); return; }

        // 切成 poolSize 个连续帧区间。
        struct Seg { qint64 start, end; QString path; };
        std::vector<Seg> segs;
        const qint64 per = (totalFrames + poolSize - 1) / poolSize;
        for (int k = 0; k < poolSize; ++k) {
            const qint64 s = static_cast<qint64>(k) * per;
            if (s >= totalFrames) break;
            const qint64 e = std::min(s + per, totalFrames);
            segs.push_back({s, e, tmpDir.filePath(QStringLiteral("seg_%1.mp4").arg(k))});
        }

        std::atomic<qint64> framesDone{0};
        const int perThreads = hardware ? 1 : std::max(1, cores / static_cast<int>(segs.size()));

        // 起 worker（每段一个），主线程轮询进度。
        std::vector<std::future<bool>> futs;
        futs.reserve(segs.size());
        for (const Seg &sg : segs) {
            futs.push_back(std::async(std::launch::async, [&, sg]() {
                return encodeSegment(sg.path, W, H, FPS, sg.start, sg.end,
                                     req.tracks, clipsMap, pathMap,
                                     encName, hardware, perThreads,
                                     framesDone, m_cancelled);
            }));
        }

        // 轮询：视频阶段占进度 0~85%。
        bool segOk = true;
        {
            using namespace std::chrono_literals;
            bool allDone = false;
            while (!allDone) {
                allDone = true;
                for (auto &f : futs) {
                    if (f.wait_for(80ms) != std::future_status::ready) { allDone = false; break; }
                }
                const qint64 done = framesDone.load();
                emit progress(static_cast<int>(done * 85 / totalFrames));
                if (m_cancelled.load()) break;
            }
        }
        for (auto &f : futs) { if (!f.get()) segOk = false; }

        if (m_cancelled.load() || !segOk) {
            QFile::remove(req.outputPath);
            emit finished(false, m_cancelled.load() ? QStringLiteral("已取消导出。")
                                                    : QStringLiteral("视频编码出错。"));
            return;
        }

        // ---- 最终封装：拼接各段视频（stream-copy）+ 混音 AAC ----
        AVFormatContext *oc = nullptr;
        const QByteArray outPath = req.outputPath.toUtf8();
        avformat_alloc_output_context2(&oc, nullptr, nullptr, outPath.constData());
        if (!oc) { fail(QStringLiteral("无法创建输出容器。")); return; }

        // 视频输出流：从第一段拷贝编码参数（含 SPS/PPS extradata）。
        AVFormatContext *firstIn = nullptr;
        if (avformat_open_input(&firstIn, segs.front().path.toUtf8().constData(), nullptr, nullptr) < 0 ||
            avformat_find_stream_info(firstIn, nullptr) < 0) {
            if (firstIn) avformat_close_input(&firstIn);
            avformat_free_context(oc);
            fail(QStringLiteral("读取临时片段失败。"));
            return;
        }
        int firstVIdx = -1;
        for (unsigned i = 0; i < firstIn->nb_streams; ++i)
            if (firstIn->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { firstVIdx = (int)i; break; }
        AVStream *vstOut = avformat_new_stream(oc, nullptr);
        avcodec_parameters_copy(vstOut->codecpar, firstIn->streams[firstVIdx]->codecpar);
        vstOut->codecpar->codec_tag = 0;
        vstOut->time_base = AVRational{1, FPS};
        avformat_close_input(&firstIn);

        // 音频流 + AAC 编码器。
        AVStream *ast = nullptr;
        AVCodecContext *actx = nullptr;
        int frameSize = 1024;
        if (hasAudio) {
            const AVCodec *acodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
            if (acodec) {
                ast = avformat_new_stream(oc, nullptr);
                actx = avcodec_alloc_context3(acodec);
                actx->sample_fmt = AV_SAMPLE_FMT_FLTP;
                actx->sample_rate = kAudioRate;
                av_channel_layout_default(&actx->ch_layout, kAudioCh);
                actx->bit_rate = 192'000;
                actx->time_base = AVRational{1, kAudioRate};
                if (oc->oformat->flags & AVFMT_GLOBALHEADER)
                    actx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
                if (avcodec_open2(actx, acodec, nullptr) < 0) {
                    avcodec_free_context(&actx); actx = nullptr; ast = nullptr;
                } else {
                    avcodec_parameters_from_context(ast->codecpar, actx);
                    ast->time_base = actx->time_base;
                    if (actx->frame_size > 0) frameSize = actx->frame_size;
                }
            }
        }

        if (!(oc->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&oc->pb, outPath.constData(), AVIO_FLAG_WRITE) < 0) {
                if (actx) avcodec_free_context(&actx);
                avformat_free_context(oc);
                fail(QStringLiteral("无法写入输出文件。"));
                return;
            }
        }
        if (avformat_write_header(oc, nullptr) < 0) {
            if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
            if (actx) avcodec_free_context(&actx);
            avformat_free_context(oc);
            fail(QStringLiteral("写入容器头失败。"));
            return;
        }

        AVPacket *pkt = av_packet_alloc();
        bool ok = true;

        // 预编码全部音频为内存包（AAC 包很小），便于与视频按时间交错写出。
        struct APkt { double tSec; AVPacket *p; };
        std::vector<APkt> audioPkts;
        if (hasAudio && actx) {
            AVFrame *aframe = av_frame_alloc();
            aframe->format = AV_SAMPLE_FMT_FLTP;
            aframe->sample_rate = kAudioRate;
            av_channel_layout_default(&aframe->ch_layout, kAudioCh);
            aframe->nb_samples = frameSize;
            av_frame_get_buffer(aframe, 0);

            const qint64 totalAudioFrames = static_cast<qint64>(mixed.size()) / kAudioCh;
            qint64 pos = 0, ptsA = 0;
            auto drainA = [&]() {
                while (true) {
                    int r = avcodec_receive_packet(actx, pkt);
                    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
                    if (r < 0) return;
                    av_packet_rescale_ts(pkt, actx->time_base, ast->time_base);
                    pkt->stream_index = ast->index;
                    AVPacket *clone = av_packet_clone(pkt);
                    audioPkts.push_back({clone->pts * av_q2d(ast->time_base), clone});
                    av_packet_unref(pkt);
                }
            };
            while (pos < totalAudioFrames) {
                if (m_cancelled.load()) { ok = false; break; }
                av_frame_make_writable(aframe);
                const int n = static_cast<int>(std::min<qint64>(frameSize, totalAudioFrames - pos));
                float *L = reinterpret_cast<float *>(aframe->data[0]);
                float *R = reinterpret_cast<float *>(aframe->data[1]);
                for (int s = 0; s < n; ++s) {
                    L[s] = mixed[static_cast<size_t>(pos + s) * kAudioCh + 0];
                    R[s] = mixed[static_cast<size_t>(pos + s) * kAudioCh + 1];
                }
                for (int s = n; s < frameSize; ++s) { L[s] = 0.0f; R[s] = 0.0f; }
                aframe->nb_samples = frameSize;
                aframe->pts = ptsA;
                ptsA += frameSize;
                pos += n;
                if (avcodec_send_frame(actx, aframe) >= 0) drainA();
            }
            if (ok) { avcodec_send_frame(actx, nullptr); drainA(); }
            av_frame_free(&aframe);
        }

        // 拼接视频：按段顺序读取各段视频包，重写全局递增 pts/dts，并按时间穿插音频包。
        qint64 gf = 0;          // 全局帧序号
        size_t apos = 0;        // 已写出的音频包数
        const double frameDurSec = 1.0 / FPS;
        const AVRational frameTb{1, FPS};

        for (const Seg &sg : segs) {
            if (!ok || m_cancelled.load()) { ok = false; break; }
            AVFormatContext *in = nullptr;
            if (avformat_open_input(&in, sg.path.toUtf8().constData(), nullptr, nullptr) < 0) { ok = false; break; }
            if (avformat_find_stream_info(in, nullptr) < 0) { avformat_close_input(&in); ok = false; break; }
            int vIdx = -1;
            for (unsigned i = 0; i < in->nb_streams; ++i)
                if (in->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { vIdx = (int)i; break; }

            while (av_read_frame(in, pkt) >= 0) {
                if (pkt->stream_index != vIdx) { av_packet_unref(pkt); continue; }
                const double vSec = gf * frameDurSec;
                // 先把时间上不晚于当前视频帧的音频包写出（手动交错，限内存）。
                while (hasAudio && apos < audioPkts.size() && audioPkts[apos].tSec <= vSec) {
                    av_interleaved_write_frame(oc, audioPkts[apos].p);
                    av_packet_free(&audioPkts[apos].p);
                    ++apos;
                }
                // 重写该视频包时间戳为全局帧序（无 B 帧，pts==dts）。
                pkt->stream_index = vstOut->index;
                pkt->pts = pkt->dts = av_rescale_q(gf, frameTb, vstOut->time_base);
                pkt->duration = av_rescale_q(1, frameTb, vstOut->time_base);
                av_interleaved_write_frame(oc, pkt);
                av_packet_unref(pkt);
                ++gf;

                const int pct = 85 + static_cast<int>(gf * 13 / totalFrames);
                emit progress(std::min(98, pct));
            }
            avformat_close_input(&in);
        }
        // 冲洗剩余音频包。
        while (ok && hasAudio && apos < audioPkts.size()) {
            av_interleaved_write_frame(oc, audioPkts[apos].p);
            av_packet_free(&audioPkts[apos].p);
            ++apos;
        }
        // 回收未写出的音频包（出错/取消时）。
        for (; apos < audioPkts.size(); ++apos) av_packet_free(&audioPkts[apos].p);

        if (ok) { av_write_trailer(oc); emit progress(100); }

        av_packet_free(&pkt);
        if (actx) avcodec_free_context(&actx);
        if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
        avformat_free_context(oc);

        if (m_cancelled.load()) {
            QFile::remove(req.outputPath);
            emit finished(false, QStringLiteral("已取消导出。"));
        } else if (ok) {
            emit finished(true, req.outputPath);
        } else {
            QFile::remove(req.outputPath);
            emit finished(false, QStringLiteral("导出过程中出错。"));
        }
    }

} // namespace Mixed::Player
