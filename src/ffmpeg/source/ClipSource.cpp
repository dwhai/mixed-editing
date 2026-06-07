//
// Created by Anlk on 2026/6/6.
// ClipSource 实现。
//

#include "../include/ClipSource.h"

#include <QDebug>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

namespace Mixed::Player {

    namespace {
        // 解码器协商像素格式回调：若候选里含本 ClipSource 选定的硬件格式则用硬解，
        // 否则退到第一个软件格式。硬件格式通过 codecCtx->opaque 传入。
        AVPixelFormat hwGetFormat(AVCodecContext *ctx, const AVPixelFormat *fmts) {
            const AVPixelFormat want = *reinterpret_cast<AVPixelFormat *>(ctx->opaque);
            for (const AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
                if (*p == want) return *p;
            }
            return fmts[0];  // 回退（通常即软件格式）
        }
    }

    ClipSource::~ClipSource() {
        close();
    }

    // 尝试为 VideoToolbox 硬解建立设备上下文并挂到解码器。
    bool ClipSource::initHwDecode(const AVCodec *codec, AVStream * /*stream*/) {
        const AVHWDeviceType type = AV_HWDEVICE_TYPE_VIDEOTOOLBOX;

        // 确认该解码器支持 VideoToolbox，并取硬件像素格式。
        AVPixelFormat hwFmt = AV_PIX_FMT_NONE;
        for (int i = 0;; ++i) {
            const AVCodecHWConfig *cfg = avcodec_get_hw_config(codec, i);
            if (!cfg) break;
            if ((cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
                cfg->device_type == type) {
                hwFmt = cfg->pix_fmt;
                break;
            }
        }
        if (hwFmt == AV_PIX_FMT_NONE) return false;

        if (av_hwdevice_ctx_create(&m_hwDeviceCtx, type, nullptr, nullptr, 0) < 0) {
            m_hwDeviceCtx = nullptr;
            return false;
        }
        m_hwPixFmt = hwFmt;
        m_codecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
        m_codecCtx->opaque = &m_hwPixFmt;
        m_codecCtx->get_format = hwGetFormat;
        return true;
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
        if (avcodec_parameters_to_context(m_codecCtx, stream->codecpar) < 0) {
            close();
            return false;
        }

        // GPU 优先：尝试 VideoToolbox 硬解（解码几乎不占 CPU）。
        const bool hwReady = initHwDecode(codec, stream);
        if (!hwReady) {
            // 回退：CPU 多线程软解（自动取核心数，帧级 + 片级并行）。
            m_codecCtx->thread_count = 0;  // 0 = 自动
            m_codecCtx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
        }

        if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
            // 硬解打开失败：拆掉硬件上下文，改多线程软解重试一次。
            if (m_hwDeviceCtx) {
                if (m_codecCtx->hw_device_ctx) av_buffer_unref(&m_codecCtx->hw_device_ctx);
                av_buffer_unref(&m_hwDeviceCtx);
                m_hwDeviceCtx = nullptr;
                m_hwPixFmt = AV_PIX_FMT_NONE;
                m_codecCtx->opaque = nullptr;
                m_codecCtx->get_format = nullptr;
                m_codecCtx->thread_count = 0;
                m_codecCtx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
            }
            if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
                close();
                return false;
            }
        }
        if (m_hwDeviceCtx) {
            m_swFrame = av_frame_alloc();  // 硬解帧回传系统内存用
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
        if (m_yuvSwsCtx) {
            sws_freeContext(m_yuvSwsCtx);
            m_yuvSwsCtx = nullptr;
        }
        if (m_cachedFrame) {
            av_frame_free(&m_cachedFrame);
        }
        if (m_swFrame) {
            av_frame_free(&m_swFrame);
        }
        if (m_codecCtx) {
            if (m_codecCtx->hw_device_ctx) {
                av_buffer_unref(&m_codecCtx->hw_device_ctx);
            }
            avcodec_free_context(&m_codecCtx);
        }
        if (m_hwDeviceCtx) {
            av_buffer_unref(&m_hwDeviceCtx);
        }
        if (m_fmtCtx) {
            avformat_close_input(&m_fmtCtx);
        }
        m_videoStreamIndex = -1;
        m_hwPixFmt = AV_PIX_FMT_NONE;
        m_width = m_height = 0;
        m_swsW = m_swsH = 0;
        m_swsFmt = AV_PIX_FMT_NONE;
        m_yuvSrcW = m_yuvSrcH = m_yuvDstW = m_yuvDstH = 0;
        m_yuvSrcFmt = AV_PIX_FMT_NONE;
        m_durationUs = 0;
        m_cachedImage = QImage();
        m_cachedImageValid = false;
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

    bool ClipSource::ensureYuvSws(int srcW, int srcH, AVPixelFormat srcFmt,
                                  int dstW, int dstH) {
        if (m_yuvSwsCtx && srcW == m_yuvSrcW && srcH == m_yuvSrcH &&
            srcFmt == m_yuvSrcFmt && dstW == m_yuvDstW && dstH == m_yuvDstH) {
            return true;
        }
        if (m_yuvSwsCtx) {
            sws_freeContext(m_yuvSwsCtx);
            m_yuvSwsCtx = nullptr;
        }
        m_yuvSwsCtx = sws_getContext(srcW, srcH, srcFmt,
                                     dstW, dstH, AV_PIX_FMT_YUV420P,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_yuvSwsCtx) {
            return false;
        }
        m_yuvSrcW = srcW; m_yuvSrcH = srcH; m_yuvSrcFmt = srcFmt;
        m_yuvDstW = dstW; m_yuvDstH = dstH;
        return true;
    }

    void ClipSource::seekTo(qint64 targetUs) {
        if (!m_fmtCtx || m_videoStreamIndex < 0) {
            return;
        }
        const int64_t ts = av_rescale_q(targetUs, AVRational{1, 1000000}, m_timeBase);
        av_seek_frame(m_fmtCtx, m_videoStreamIndex, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(m_codecCtx);
    }

    // 解码到首个覆盖 sourceUs 的帧并缓存原始 AVFrame（不做任何色彩转换）。
    // RGBA(frameAt) 与 YUV(frameYuvAt) 两条路径都从这里取缓存帧再各自转换。
    AVFrame *ClipSource::decodeFrameAt(qint64 sourceUs) {
        if (!isOpen()) {
            return nullptr;
        }
        if (sourceUs < 0) sourceUs = 0;

        // 快路径：命中缓存帧的展示区间，直接复用缓存的原始帧。
        if (m_cachedFrame && m_cachedPtsUs >= 0 && sourceUs >= m_cachedPtsUs &&
            sourceUs < m_cachedPtsUs + m_cachedDurUs) {
            return m_cachedFrame;
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
        bool done = false;

        // 默认帧时长：按平均帧率估计，未知时给 40ms。
        qint64 defaultDurUs = 40'000;
        AVStream *stream = m_fmtCtx->streams[m_videoStreamIndex];
        if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
            defaultDurUs = static_cast<qint64>(
                1'000'000.0 * stream->avg_frame_rate.den / stream->avg_frame_rate.num);
        }

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

                        // 解码到首个 pts+dur > 目标的帧即采用，缓存原始帧。
                        if (framePtsUs + defaultDurUs > sourceUs) {
                            // 选择要缓存的帧：硬解帧在 GPU，先回传系统内存。
                            AVFrame *toCache = frame;
                            if (m_hwDeviceCtx && frame->format == m_hwPixFmt) {
                                av_frame_unref(m_swFrame);
                                if (av_hwframe_transfer_data(m_swFrame, frame, 0) < 0) {
                                    av_packet_unref(pkt);
                                    continue;  // 回传失败，跳过此帧继续
                                }
                                m_swFrame->pts = frame->pts;
                                m_swFrame->duration = frame->duration;
                                toCache = m_swFrame;
                            }
                            if (m_cachedFrame) av_frame_free(&m_cachedFrame);
                            m_cachedFrame = av_frame_alloc();
                            av_frame_ref(m_cachedFrame, toCache);  // 引用计数，零拷贝
                            m_cachedImageValid = false;            // RGBA 缓存失效
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

        av_frame_free(&frame);
        av_packet_free(&pkt);
        // EOF 前未命中：退而用最后一次缓存帧（若有）。
        return m_cachedFrame;
    }

    QImage ClipSource::frameAt(qint64 sourceUs) {
        AVFrame *f = decodeFrameAt(sourceUs);
        if (!f) {
            return m_cachedImageValid ? m_cachedImage : QImage();
        }
        // 懒转换：同一缓存帧的多次 RGBA 请求只转一次。
        if (!m_cachedImageValid) {
            m_cachedImage = convertToImage(f);
            m_cachedImageValid = true;
        }
        return m_cachedImage;
    }

    bool ClipSource::frameYuvAt(qint64 sourceUs, int dstW, int dstH,
                                QByteArray &outY, QByteArray &outU, QByteArray &outV) {
        if (dstW <= 0 || dstH <= 0) return false;
        AVFrame *f = decodeFrameAt(sourceUs);
        if (!f) return false;

        if (!ensureYuvSws(f->width, f->height,
                          static_cast<AVPixelFormat>(f->format), dstW, dstH)) {
            return false;
        }

        const int cw = dstW / 2;
        const int ch = dstH / 2;
        outY.resize(dstW * dstH);
        outU.resize(cw * ch);
        outV.resize(cw * ch);

        uint8_t *dstData[4] = {
            reinterpret_cast<uint8_t *>(outY.data()),
            reinterpret_cast<uint8_t *>(outU.data()),
            reinterpret_cast<uint8_t *>(outV.data()),
            nullptr};
        int dstLinesize[4] = {dstW, cw, cw, 0};

        sws_scale(m_yuvSwsCtx, f->data, f->linesize, 0, f->height, dstData, dstLinesize);
        return true;
    }

} // namespace Mixed::Player
