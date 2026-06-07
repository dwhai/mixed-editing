//
// Created by Anlk on 2026/6/6.
// Exporter 实现（全流水线并行版）。
//
// 架构总览（无临时文件，全部在内存中以 AVPacket 流转）：
//
//   视频：把 [0,totalFrames) 切成 N 个连续帧区间，每段一个 std::async worker，
//         自带 CompositionEngine 顺序合成 + H.264 编码，把 AVPacket 推入「每段一个」
//         的有界队列 m_vQueues[k]（背压控内存）。
//   音频：把整条时间线按样本切成 M 个区间，并行 worker 各自解码/重采样/混音出该区间
//         的 PCM（耗时部分并行）；按序拼成整轨后交给「单一」AAC 编码器串行编码
//         （AAC 极廉价且避免分段 priming 爆音），把 AVPacket 推入 m_aQueue。
//   muxer：单线程消费——视频按段序逐段取包并重写全局递增 pts/dts；与音频包做按时间戳
//         的 2 路归并，喂 av_interleaved_write_frame 直接写最终 MP4。
//
// muxer 与编码并发跑，省掉旧版「先并行编码→再串行拼接」的串行收尾，也去掉了临时文件磁盘往返。
// 自带独立的 CompositionEngine 与解码器，不与主线程预览共享对象，线程安全。
//

#include "../include/Exporter.h"
#include "../include/CompositionEngine.h"
#include "../include/PlayerTypes.h"
#include "../include/AudioMix.h"

#include <QFile>
#include <algorithm>
#include <cmath>
#include <future>
#include <memory>
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
        // 音频混流目标格式常量在 AudioMix.h 中定义（kAudioRate/kAudioCh/kUsPerSec）。

        // 每段视频包队列的内存预算：总预算 / 段数 ≈ 每队列可缓冲的包数上限。
        // 预算内各段编码器全速并行；超出则在 push 处背压，等 muxer 消费腾出空间。
        // 6Mbps 下每帧约 25KB，512MB 预算可缓冲约 2 万帧，足够典型工程全并行。
        constexpr qint64 kVideoQueueBudgetBytes = 512LL * 1024 * 1024;
        constexpr qint64 kApproxBytesPerFrame   = 32 * 1024;  // 粗估，仅用于换算队列深度

        // ---- 编码器选择/配置（与旧版一致） ----

        // 选择 H.264 编码器：优先硬件 VideoToolbox（macOS 媒体引擎），回退 libx264。
        const char *pickH264Encoder(int W, int H, int FPS, int bitrate, bool &hwOut) {
            const char *vt = "h264_videotoolbox";
            const AVCodec *c = avcodec_find_encoder_by_name(vt);
            if (c) {
                AVCodecContext *probe = avcodec_alloc_context3(c);
                if (probe) {
                    probe->width = W; probe->height = H;
                    probe->time_base = AVRational{1, FPS};
                    probe->pix_fmt = AV_PIX_FMT_YUV420P;
                    probe->bit_rate = bitrate;
                    probe->color_range = AVCOL_RANGE_MPEG;  // 与真实编码条件一致
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
                                        int W, int H, int FPS, int bitrate, int threadCount,
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
            ctx->bit_rate = bitrate;
            ctx->gop_size = FPS;
            ctx->max_b_frames = 0;   // 无 B 帧：muxer 重写 pts 时 pts==dts，顺序简单
            // 显式标注色彩元数据：limited(MPEG) range + BT.709（HD 标准）。
            // 否则 videotoolbox 会因 color_range 未设而告警，且成片缺色彩标签。
            ctx->color_range     = AVCOL_RANGE_MPEG;
            ctx->colorspace      = AVCOL_SPC_BT709;
            ctx->color_primaries = AVCOL_PRI_BT709;
            ctx->color_trc       = AVCOL_TRC_BT709;
            if (globalHeader) ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            if (hardware) {
                av_opt_set(ctx->priv_data, "allow_sw", "1", 0);
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

    } // namespace

    Exporter::Exporter(QObject *parent) : QObject(parent) {}
    Exporter::~Exporter() = default;

    // muxer 队列里流转的包：携带「该包应出现的时间（秒）」用于 2 路归并。
    namespace {
        struct QPkt {
            AVPacket *pkt = nullptr;
            double    tSec = 0.0;   // 用于视频/音频按时间归并
            bool      eos = false;  // 段/流结束哨兵
        };
    }

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
        const int bitrate = req.videoBitrate > 0 ? req.videoBitrate : 6'000'000;
        const qint64 totalFrames =
            std::max<qint64>(1, static_cast<qint64>(std::ceil(req.durationUs / 1'000'000.0 * FPS)));

        // 时间线 map（供各段引擎复用）。
        std::unordered_map<QString, std::vector<DB::Clip>> clipsMap;
        for (auto it = req.clipsByTrack.constBegin(); it != req.clipsByTrack.constEnd(); ++it)
            clipsMap.emplace(it.key(), it.value());
        std::unordered_map<QString, QString> pathMap;
        for (auto it = req.assetPathByClip.constBegin(); it != req.assetPathByClip.constEnd(); ++it)
            pathMap.emplace(it.key(), it.value());

        // 音频混流快照（供并行混音 worker 复用，AudioMix 内核接收此结构）。
        AudioMixSource amSrc{req.tracks, req.clipsByTrack, req.assetPathByClip};

        // 是否存在音频片段。
        bool hasAudio = false;
        for (const DB::Track &t : req.tracks) {
            if (t.trackType == QStringLiteral("audio") &&
                clipsMap.find(t.id) != clipsMap.end() && !clipsMap[t.id].empty()) {
                hasAudio = true; break;
            }
        }

        // 编码器选择 + 并行度（与旧版同策略）。
        bool hardware = false;
        const char *encName = pickH264Encoder(W, H, FPS, bitrate, hardware);
        unsigned hw = std::thread::hardware_concurrency();
        const int cores = static_cast<int>(hw == 0 ? 4 : hw);
        int poolSize = hardware ? std::clamp(cores, 1, 8) : std::clamp(cores / 2, 1, 4);
        poolSize = static_cast<int>(std::min<qint64>(poolSize, std::max<qint64>(1, totalFrames / 30)));
        if (poolSize < 1) poolSize = 1;
        const int perThreads = hardware ? 1 : std::max(1, cores / poolSize);

        // ---- 准备最终输出容器与两条流 ----
        AVFormatContext *oc = nullptr;
        const QByteArray outPath = req.outputPath.toUtf8();
        avformat_alloc_output_context2(&oc, nullptr, nullptr, outPath.constData());
        if (!oc) { fail(QStringLiteral("无法创建输出容器。")); return; }
        const bool globalHeader = (oc->oformat->flags & AVFMT_GLOBALHEADER) != 0;

        // 视频流参数：用一个「参数探测编码器」打开一次以拿到 extradata(SPS/PPS)。
        AVCodecContext *vparam = openH264Encoder(encName, hardware, W, H, FPS, bitrate, perThreads, globalHeader);
        if (!vparam) { avformat_free_context(oc); fail(QStringLiteral("无法初始化视频编码器。")); return; }
        AVStream *vst = avformat_new_stream(oc, nullptr);
        avcodec_parameters_from_context(vst->codecpar, vparam);
        vst->time_base = AVRational{1, FPS};
        avcodec_free_context(&vparam);   // 仅用于取参数，真正编码在各段独立开

        // 音频流 + 单一 AAC 编码器（在主线程开，extradata 写头要用）。
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
                if (globalHeader) actx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
                if (avcodec_open2(actx, acodec, nullptr) < 0) {
                    avcodec_free_context(&actx); actx = nullptr; ast = nullptr; hasAudio = false;
                } else {
                    avcodec_parameters_from_context(ast->codecpar, actx);
                    ast->time_base = actx->time_base;
                    if (actx->frame_size > 0) frameSize = actx->frame_size;
                }
            } else {
                hasAudio = false;
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

        // ---- 切分视频段 + 每段一个有界队列 ----
        struct Seg { qint64 start, end; };
        std::vector<Seg> segs;
        const qint64 per = (totalFrames + poolSize - 1) / poolSize;
        for (int k = 0; k < poolSize; ++k) {
            const qint64 s = static_cast<qint64>(k) * per;
            if (s >= totalFrames) break;
            segs.push_back({s, std::min(s + per, totalFrames)});
        }
        const int nSeg = static_cast<int>(segs.size());

        // 每段队列深度：内存预算 / 段数 / 每帧粗估字节。至少留 30 帧缓冲。
        const qint64 budgetFrames = kVideoQueueBudgetBytes / kApproxBytesPerFrame;
        const size_t qCap = static_cast<size_t>(std::max<qint64>(30, budgetFrames / nSeg));

        std::vector<std::unique_ptr<ThreadSafeQueue<QPkt>>> vQueues;
        vQueues.reserve(nSeg);
        for (int k = 0; k < nSeg; ++k)
            vQueues.push_back(std::make_unique<ThreadSafeQueue<QPkt>>(qCap));

        ThreadSafeQueue<QPkt> aQueue(256);  // AAC 包很小，浅队列即可
        std::atomic<qint64> framesEncoded{0};

        // ---- 视频段 worker：合成 + 编码 → 推队列（段内本地 pts，从 0 起） ----
        auto encodeSeg = [&](int k) {
            const Seg sg = segs[k];
            ThreadSafeQueue<QPkt> &q = *vQueues[k];

            AVCodecContext *vctx = openH264Encoder(encName, hardware, W, H, FPS,
                                                   bitrate, perThreads, globalHeader);
            if (!vctx) { q.push(QPkt{nullptr, 0.0, true}); return false; }

            CompositionEngine engine;
            engine.setCanvasSize(W, H);
            engine.setTimeline(req.tracks, clipsMap, pathMap);

            AVFrame *vframe = av_frame_alloc();
            vframe->format = AV_PIX_FMT_YUV420P;
            vframe->width = W; vframe->height = H;
            av_frame_get_buffer(vframe, 32);
            AVPacket *pkt = av_packet_alloc();

            bool ok = true;
            auto drain = [&](AVFrame *fr) -> bool {
                if (avcodec_send_frame(vctx, fr) < 0) return false;
                while (true) {
                    int r = avcodec_receive_packet(vctx, pkt);
                    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
                    if (r < 0) return false;
                    // 段内本地帧号（pts 已是 0..），加上段起点得全局帧号。
                    const qint64 globalFrame = sg.start + pkt->pts;
                    AVPacket *clone = av_packet_clone(pkt);
                    av_packet_unref(pkt);
                    QPkt item{clone, static_cast<double>(globalFrame) / FPS, false};
                    // push 失败说明被 abort（取消），释放并退出。
                    if (!q.push(std::move(item))) { av_packet_free(&clone); return false; }
                }
                return true;
            };

            for (qint64 i = sg.start; i < sg.end; ++i) {
                if (m_cancelled.load()) { ok = false; break; }
                const qint64 tUs = static_cast<qint64>(i * kUsPerSec / FPS);
                VideoFrame f = engine.composeAt(tUs);
                fillVideoFrame(vframe, f, W, H, i - sg.start);  // 段内本地 pts 从 0
                if (!drain(vframe)) { ok = false; break; }
                framesEncoded.fetch_add(1);
            }
            if (ok) ok = drain(nullptr);  // 冲洗

            av_frame_free(&vframe);
            av_packet_free(&pkt);
            avcodec_free_context(&vctx);
            q.push(QPkt{nullptr, 0.0, true});  // 段结束哨兵
            return ok;
        };

        // ---- 音频：并行混音（按样本切 M 段）→ 按序拼接 → 单一 AAC 编码 → 推 aQueue ----
        auto audioMain = [&]() -> bool {
            if (!hasAudio || !actx) { aQueue.push(QPkt{nullptr, 0.0, true}); return true; }
            const qint64 totalSamples =
                static_cast<qint64>(std::ceil(req.durationUs / 1'000'000.0 * kAudioRate));
            if (totalSamples <= 0) { aQueue.push(QPkt{nullptr, 0.0, true}); return true; }

            // 混音并行度：取核心数的一半，避免与视频编码过度争抢。
            const int mixWorkers = std::clamp(cores / 2, 1, 8);
            const qint64 perRange = (totalSamples + mixWorkers - 1) / mixWorkers;
            std::vector<std::future<std::vector<float>>> mixFuts;
            for (int k = 0; k < mixWorkers; ++k) {
                const qint64 s = static_cast<qint64>(k) * perRange;
                if (s >= totalSamples) break;
                const qint64 e = std::min(s + perRange, totalSamples);
                mixFuts.push_back(std::async(std::launch::async, [&, s, e]() {
                    std::vector<float> buf;
                    mixAudioRange(amSrc, s, e, buf, m_cancelled);
                    return buf;
                }));
            }
            // 按序拼接（区间天然有序）。
            std::vector<float> mixed;
            mixed.reserve(static_cast<size_t>(totalSamples) * kAudioCh);
            for (auto &f : mixFuts) {
                std::vector<float> part = f.get();
                mixed.insert(mixed.end(), part.begin(), part.end());
            }
            if (m_cancelled.load()) { aQueue.push(QPkt{nullptr, 0.0, true}); return false; }
            // 软限幅。
            for (float &v : mixed) v = std::clamp(v, -1.0f, 1.0f);

            // 单一 AAC 编码器串行编码（廉价，无分段 priming 问题）。
            AVFrame *aframe = av_frame_alloc();
            aframe->format = AV_SAMPLE_FMT_FLTP;
            aframe->sample_rate = kAudioRate;
            av_channel_layout_default(&aframe->ch_layout, kAudioCh);
            aframe->nb_samples = frameSize;
            av_frame_get_buffer(aframe, 0);
            AVPacket *pkt = av_packet_alloc();

            const qint64 totalAudioFrames = static_cast<qint64>(mixed.size()) / kAudioCh;
            qint64 pos = 0, ptsA = 0;
            bool ok = true;
            auto drainA = [&]() {
                while (true) {
                    int r = avcodec_receive_packet(actx, pkt);
                    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
                    if (r < 0) return;
                    AVPacket *clone = av_packet_clone(pkt);
                    const double tSec = clone->pts * av_q2d(actx->time_base);
                    av_packet_unref(pkt);
                    if (!aQueue.push(QPkt{clone, tSec, false})) { av_packet_free(&clone); return; }
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
            av_packet_free(&pkt);
            aQueue.push(QPkt{nullptr, 0.0, true});  // 音频流结束哨兵
            return ok;
        };

        // ---- 起 worker：N 个视频段 + 1 个音频 ----
        std::vector<std::future<bool>> vFuts;
        vFuts.reserve(nSeg);
        for (int k = 0; k < nSeg; ++k)
            vFuts.push_back(std::async(std::launch::async, [&, k]() { return encodeSeg(k); }));
        std::future<bool> aFut = std::async(std::launch::async, audioMain);

        // ---- muxer 主循环：视频按段序取包，与音频做时间戳 2 路归并写出 ----
        const AVRational frameTb{1, FPS};
        qint64 globalFrame = 0;       // 已写视频帧数（全局递增 pts/dts）
        bool ok = true;

        // 当前段索引 + 该段队首暂存。
        int curSeg = 0;
        QPkt pendingV; bool haveV = false; bool vDone = (nSeg == 0);
        QPkt pendingA; bool haveA = false; bool aDone = !hasAudio;

        // 从视频队列取下一个「真实包」（跳过段哨兵、推进段索引）。
        auto pullVideo = [&]() {
            while (!vDone) {
                if (curSeg >= nSeg) { vDone = true; break; }
                QPkt item;
                if (!vQueues[curSeg]->pop(item)) { vDone = true; break; }  // abort
                if (item.eos) { ++curSeg; continue; }                      // 段结束→下一段
                pendingV = item; haveV = true; return;
            }
        };
        auto pullAudio = [&]() {
            if (aDone) return;
            QPkt item;
            if (!aQueue.pop(item)) { aDone = true; return; }
            if (item.eos) { aDone = true; return; }
            pendingA = item; haveA = true;
        };

        if (!vDone) pullVideo();
        if (!aDone) pullAudio();

        while (ok && (haveV || haveA)) {
            if (m_cancelled.load()) { ok = false; break; }

            // 选时间戳较小者写出（保证送入 muxer 的时间大致有序）。
            const bool writeVideo = haveV && (!haveA || pendingV.tSec <= pendingA.tSec);

            if (writeVideo) {
                AVPacket *p = pendingV.pkt;
                // 重写全局递增 pts/dts（无 B 帧，pts==dts）。
                p->stream_index = vst->index;
                p->pts = p->dts = av_rescale_q(globalFrame, frameTb, vst->time_base);
                p->duration = av_rescale_q(1, frameTb, vst->time_base);
                if (av_interleaved_write_frame(oc, p) < 0) ok = false;
                av_packet_free(&p);
                ++globalFrame;
                haveV = false;
                pullVideo();

                const int pct = static_cast<int>(globalFrame * 98 / totalFrames);
                emit progress(std::min(98, pct));
            } else if (haveA) {
                AVPacket *p = pendingA.pkt;
                p->stream_index = ast->index;
                // 音频包时间戳已是 ast->time_base（编码时 actx 与 ast 同 time_base）。
                if (av_interleaved_write_frame(oc, p) < 0) ok = false;
                av_packet_free(&p);
                haveA = false;
                pullAudio();
            }
        }

        // 取消/出错：abort 所有队列唤醒 worker，并清空残留包。
        if (!ok || m_cancelled.load()) {
            for (auto &q : vQueues) q->abort();
            aQueue.abort();
        }

        // 等所有 worker 收束。
        bool segOk = true;
        for (auto &f : vFuts) { if (!f.get()) segOk = false; }
        const bool audioOk = aFut.get();

        // 回收队列里可能残留的包（取消时）。
        auto drainQueue = [](ThreadSafeQueue<QPkt> &q) {
            QPkt item;
            while (q.tryPop(item)) { if (item.pkt) av_packet_free(&item.pkt); }
        };
        for (auto &q : vQueues) drainQueue(*q);
        drainQueue(aQueue);

        const bool success = ok && segOk && audioOk && !m_cancelled.load();
        if (success) { av_write_trailer(oc); emit progress(100); }

        if (actx) avcodec_free_context(&actx);
        if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
        avformat_free_context(oc);

        if (m_cancelled.load()) {
            QFile::remove(req.outputPath);
            emit finished(false, QStringLiteral("已取消导出。"));
        } else if (success) {
            emit finished(true, req.outputPath);
        } else {
            QFile::remove(req.outputPath);
            emit finished(false, QStringLiteral("导出过程中出错。"));
        }
    }

} // namespace Mixed::Player
