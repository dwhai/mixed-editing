//
// Created by Anlk on 2026/6/6.
// MediaProbe 实现。
//

#include "../include/MediaProbe.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace Mixed::Player {

    MediaInfo MediaProbe::probe(const QString &url) {
        MediaInfo info;

        AVFormatContext *fmt = nullptr;
        const QByteArray urlBytes = url.toUtf8();
        if (avformat_open_input(&fmt, urlBytes.constData(), nullptr, nullptr) != 0) {
            return info; // ok 仍为 false
        }
        if (avformat_find_stream_info(fmt, nullptr) < 0) {
            avformat_close_input(&fmt);
            return info;
        }

        // 总时长：AV_TIME_BASE 单位（微秒），未知为 AV_NOPTS_VALUE。
        if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0) {
            info.durationUs = static_cast<qint64>(fmt->duration);
        }

        for (unsigned i = 0; i < fmt->nb_streams; ++i) {
            const AVStream *stream = fmt->streams[i];
            const AVCodecParameters *par = stream->codecpar;

            if (par->codec_type == AVMEDIA_TYPE_VIDEO && !info.hasVideo) {
                info.hasVideo = true;
                info.width = par->width;
                info.height = par->height;
                // 优先用平均帧率，回退到 r_frame_rate。
                AVRational rate = stream->avg_frame_rate;
                if (rate.num == 0 || rate.den == 0) {
                    rate = stream->r_frame_rate;
                }
                if (rate.den != 0) {
                    info.fps = av_q2d(rate);
                }
                if (const char *name = avcodec_get_name(par->codec_id)) {
                    info.videoCodec = QString::fromUtf8(name);
                }
            } else if (par->codec_type == AVMEDIA_TYPE_AUDIO && !info.hasAudio) {
                info.hasAudio = true;
                info.sampleRate = par->sample_rate;
                info.channels = par->ch_layout.nb_channels;
                if (info.videoCodec.isEmpty()) {
                    if (const char *name = avcodec_get_name(par->codec_id)) {
                        info.videoCodec = QString::fromUtf8(name);
                    }
                }
            }
        }

        avformat_close_input(&fmt);
        info.ok = info.hasVideo || info.hasAudio;
        return info;
    }

} // namespace Mixed::Player
