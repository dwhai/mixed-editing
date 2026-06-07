//
// Created by Anlk on 2026/6/7.
// AudioMix 实现：从 Exporter.cpp 抽出的时间线音频混流内核（逻辑保持一致）。
//

#include "../include/AudioMix.h"

#include <algorithm>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace Mixed::Player {

    void mixAudioRange(const AudioMixSource &src,
                       qint64 startSample, qint64 endSample,
                       std::vector<float> &out,
                       std::atomic<bool> &cancelled) {
        const qint64 rangeSamples = endSample - startSample;
        if (rangeSamples <= 0) return;
        out.assign(static_cast<size_t>(rangeSamples) * kAudioCh, 0.0f);

        // 区间在时间线上的微秒范围（用于裁剪片段）。
        const qint64 rangeStartUs = startSample * kUsPerSec / kAudioRate;
        const qint64 rangeEndUs   = endSample   * kUsPerSec / kAudioRate;

        for (const DB::Track &track : src.tracks) {
            if (track.trackType != QStringLiteral("audio")) continue;
            auto clipsIt = src.clipsByTrack.constFind(track.id);
            if (clipsIt == src.clipsByTrack.constEnd()) continue;

            for (const DB::Clip &clip : clipsIt.value()) {
                if (cancelled.load()) return;
                // 片段在时间线上的覆盖范围；与本区间无交集则跳过。
                const qint64 clipTlStart = clip.timelineStartUs;
                const qint64 clipTlEnd   = clip.timelineStartUs +
                                           (clip.sourceOutUs - clip.sourceInUs);
                if (clipTlEnd <= rangeStartUs || clipTlStart >= rangeEndUs) continue;

                auto pathIt = src.assetPathByClip.constFind(clip.id);
                if (pathIt == src.assetPathByClip.constEnd() || pathIt.value().isEmpty()) continue;
                const QByteArray path = pathIt.value().toUtf8();

                AVFormatContext *fmt = nullptr;
                if (avformat_open_input(&fmt, path.constData(), nullptr, nullptr) != 0) continue;
                if (avformat_find_stream_info(fmt, nullptr) < 0) { avformat_close_input(&fmt); continue; }

                int aIdx = -1;
                for (unsigned i = 0; i < fmt->nb_streams; ++i)
                    if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) { aIdx = (int)i; break; }
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

                // seek 到「本区间起点对应的源内位置」与片段源起点的较大者，
                // 避免每段都从片段头解码（区间靠后时浪费）。
                const qint64 clipSrcForRange = clip.sourceInUs +
                    std::max<qint64>(0, rangeStartUs - clipTlStart);
                const int64_t seekTs = av_rescale_q(clipSrcForRange,
                    AVRational{1, 1000000}, st->time_base);
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

                            // 该帧首样本在时间线上的绝对样本号。
                            const qint64 srcOffUs = framePtsUs - clip.sourceInUs;
                            const qint64 tlUs = clip.timelineStartUs + srcOffUs;
                            const qint64 absStart = static_cast<qint64>(
                                std::llround(tlUs / 1'000'000.0 * kAudioRate));

                            for (int s = 0; s < got; ++s) {
                                const qint64 abs = absStart + s;
                                if (abs < startSample || abs >= endSample) continue;
                                const qint64 local = abs - startSample;  // 写进本区间偏移
                                out[static_cast<size_t>(local) * kAudioCh + 0] += tmp[s * kAudioCh + 0];
                                out[static_cast<size_t>(local) * kAudioCh + 1] += tmp[s * kAudioCh + 1];
                            }
                            // 已越过本区间右界则可提前结束该片段解码。
                            if (absStart > endSample) { done = true; break; }
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
    }

} // namespace Mixed::Player
