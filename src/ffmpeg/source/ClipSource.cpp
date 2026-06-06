//
// Created by Anlk on 2026/6/6.
// ClipSource 实现。
//

#include "../include/ClipSource.h"

#include <QDebug>

extern "C" {
#include <libavutil/imgutils.h>
}

namespace Mixed::Player {

    ClipSource::~ClipSource() {
        close();
    }

    bool ClipSource::open(const QString &filePath) {
        close();

        const QByteArray path = filePath.toUtf8();
        if (avformat_open_input(&m_fmtCtx, path.constData(), nullptr, nullptr) != 0) {
            qWarning() << "[ClipSource] 打开失败:" << filePath;
            m_fmtCtx = nullptr;
            return false;
        }
        if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
            close();
            return false;
        }

        // 找首个视频流。
        for (unsigned i = 0; i < m_fmtCtx->nb_streams; ++i) {
            if (m_fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                m_videoStreamIndex = static_cast<int>(i);
                break;
            }
        }
        if (m_videoStreamIndex < 0) {
            close();
            return false;
        }

        AVStream *stream = m_fmtCtx->streams[m_videoStreamIndex];
        m_timeBase = stream->time_base;

        const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) {
            close();
            return false;
        }
        m_codecCtx = avcodec_alloc_context3(codec);
        if (!m_codecCtx) {
            close();
            return false;
        }
        if (avcodec_parameters_to_context(m_codecCtx, stream->codecpar) < 0 ||
            avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
            close();
            return false;
        }

        m_width = m_codecCtx->width;
        m_height = m_codecCtx->height;
        if (m_fmtCtx->duration != AV_NOPTS_VALUE && m_fmtCtx->duration > 0) {
            m_durationUs = static_cast<qint64>(m_fmtCtx->duration); // AV_TIME_BASE 即微秒
        }
        return true;
    }

    void ClipSource::close() {
        if (m_swsCtx) {
            sws_freeContext(m_swsCtx);
            m_swsCtx = nullptr;
        }
        if (m_codecCtx) {
            avcodec_free_context(&m_codecCtx);
        }
        if (m_fmtCtx) {
            avformat_close_input(&m_fmtCtx);
        }
        m_videoStreamIndex = -1;
        m_width = m_height = 0;
        m_swsW = m_swsH = 0;
        m_swsFmt = AV_PIX_FMT_NONE;
        m_durationUs = 0;
        m_cachedImage = QImage();
        m_cachedPtsUs = -1;
        m_cachedDurUs = 0;
    }

    bool ClipSource::ensureSws(int srcW, int srcH, AVPixelFormat srcFmt) {
        if (m_swsCtx && srcW == m_swsW && srcH == m_swsH && srcFmt == m_swsFmt) {
            return true;
        }
        if (m_swsCtx) {
            sws_freeContext(m_swsCtx);
            m_swsCtx = nullptr;
        }
        m_swsCtx = sws_getContext(srcW, srcH, srcFmt,
                                  srcW, srcH, AV_PIX_FMT_RGBA,
                                  SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_swsCtx) {
            return false;
        }
        m_swsW = srcW;
        m_swsH = srcH;
        m_swsFmt = srcFmt;
        return true;
    }

    QImage ClipSource::convertToImage(AVFrame *frame) {
        if (!ensureSws(frame->width, frame->height,
                       static_cast<AVPixelFormat>(frame->format))) {
            return QImage();
        }
        // QImage 自带 4 字节对齐的 RGBA 缓冲，直接作为 sws 目标。
        QImage img(frame->width, frame->height, QImage::Format_RGBA8888);
        uint8_t *dst[4] = {img.bits(), nullptr, nullptr, nullptr};
        int dstLinesize[4] = {static_cast<int>(img.bytesPerLine()), 0, 0, 0};
        sws_scale(m_swsCtx, frame->data, frame->linesize, 0, frame->height,
                  dst, dstLinesize);
        return img;
    }

    void ClipSource::seekTo(qint64 targetUs) {
        if (!m_fmtCtx || m_videoStreamIndex < 0) {
            return;
        }
        const int64_t ts = av_rescale_q(targetUs, AVRational{1, 1000000}, m_timeBase);
        av_seek_frame(m_fmtCtx, m_videoStreamIndex, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(m_codecCtx);
    }

    QImage ClipSource::frameAt(qint64 sourceUs) {
        if (!isOpen()) {
            return QImage();
        }
        if (sourceUs < 0) sourceUs = 0;

        // 快路径：命中缓存帧的展示区间。
        if (m_cachedPtsUs >= 0 && sourceUs >= m_cachedPtsUs &&
            sourceUs < m_cachedPtsUs + m_cachedDurUs) {
            return m_cachedImage;
        }

        // 若目标在缓存帧之前，或跨度过大，先 seek。否则向前顺序解码即可。
        const bool needSeek = (m_cachedPtsUs < 0) || (sourceUs < m_cachedPtsUs) ||
                              (sourceUs - m_cachedPtsUs > 2'000'000);
        if (needSeek) {
            seekTo(sourceUs);
            m_cachedPtsUs = -1;
        }

        AVPacket *pkt = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        QImage result;

        // 默认帧时长：按平均帧率估计，未知时给 40ms。
        qint64 defaultDurUs = 40'000;
        AVStream *stream = m_fmtCtx->streams[m_videoStreamIndex];
        if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
            defaultDurUs = static_cast<qint64>(
                1'000'000.0 * stream->avg_frame_rate.den / stream->avg_frame_rate.num);
        }

        bool done = false;
        while (!done && av_read_frame(m_fmtCtx, pkt) >= 0) {
            if (pkt->stream_index == m_videoStreamIndex) {
                if (avcodec_send_packet(m_codecCtx, pkt) == 0) {
                    while (avcodec_receive_frame(m_codecCtx, frame) == 0) {
                        const int64_t bestPts =
                            (frame->best_effort_timestamp != AV_NOPTS_VALUE)
                                ? frame->best_effort_timestamp
                                : frame->pts;
                        const qint64 framePtsUs =
                            (bestPts == AV_NOPTS_VALUE)
                                ? 0
                                : av_rescale_q(bestPts, m_timeBase, AVRational{1, 1000000});

                        // 解码到首个 pts >= 目标的帧（或紧邻其前的帧）即采用。
                        if (framePtsUs + defaultDurUs > sourceUs) {
                            result = convertToImage(frame);
                            m_cachedImage = result;
                            m_cachedPtsUs = framePtsUs;
                            m_cachedDurUs = (frame->duration > 0)
                                ? av_rescale_q(frame->duration, m_timeBase, AVRational{1, 1000000})
                                : defaultDurUs;
                            done = true;
                            break;
                        }
                    }
                }
            }
            av_packet_unref(pkt);
        }

        // EOF 前未命中：退而用最后一次缓存（若有）。
        if (!done && !m_cachedImage.isNull()) {
            result = m_cachedImage;
        }

        av_frame_free(&frame);
        av_packet_free(&pkt);
        return result;
    }

} // namespace Mixed::Player
