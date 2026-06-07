//
// Created by Anlk on 2026/6/6.
// ClipSource：单个媒体文件的可 seek 视频解码器。
// 与流式播放的 MediaPlayer 不同，它面向「合成」场景：按需 seek 到任意时刻取一帧，
// 解码结果转为 RGBA(QImage) 以便上层用 QPainter 做图层叠加。
// 内部缓存上一帧，相邻时间点的请求走快路径，避免重复 seek/解码。
//

#ifndef MIXEDEDITING_CLIPSOURCE_H
#define MIXEDEDITING_CLIPSOURCE_H

#include <QImage>
#include <QString>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}

namespace Mixed::Player {

    class ClipSource {
    public:
        ClipSource() = default;
        ~ClipSource();

        ClipSource(const ClipSource &) = delete;
        ClipSource &operator=(const ClipSource &) = delete;

        // 打开文件并初始化视频解码器。无视频流或失败返回 false。
        bool open(const QString &filePath);
        void close();

        bool isOpen() const { return m_fmtCtx != nullptr && m_videoStreamIndex >= 0; }

        int width() const { return m_width; }
        int height() const { return m_height; }
        // 源时长（微秒），未知为 0。
        qint64 durationUs() const { return m_durationUs; }

        // 取源内 sourceUs（微秒）时刻的帧，返回 RGBA8888 的 QImage。
        // 失败或越界返回 null QImage。
        QImage frameAt(qint64 sourceUs);

        // 取源内 sourceUs 时刻的帧，直接缩放为 dstW×dstH 的 YUV420P，写入三平面。
        // 用于「单片段铺满画布」的导出快路径：跳过 RGBA + QPainter + 二次转换。
        // 成功返回 true 并填好 outY/outU/outV（调用方按 dstW*dstH / (dstW/2)*(dstH/2) 预留）。
        bool frameYuvAt(qint64 sourceUs, int dstW, int dstH,
                        QByteArray &outY, QByteArray &outU, QByteArray &outV);

    private:
        bool ensureSws(int srcW, int srcH, AVPixelFormat srcFmt);
        bool ensureYuvSws(int srcW, int srcH, AVPixelFormat srcFmt, int dstW, int dstH);
        QImage convertToImage(AVFrame *frame);
        // 解码到首个覆盖 sourceUs 的帧并缓存原始 AVFrame（RGBA/YUV 路径共用）。
        // 返回缓存帧指针；失败返回 nullptr。
        AVFrame *decodeFrameAt(qint64 sourceUs);
        // 尝试初始化 VideoToolbox 硬件解码；不可用返回 false（调用方回退软解）。
        bool initHwDecode(const AVCodec *codec, AVStream *stream);
        // seek 到不晚于 targetUs 的关键帧并刷新解码器。
        void seekTo(qint64 targetUs);

        AVFormatContext *m_fmtCtx = nullptr;
        AVCodecContext  *m_codecCtx = nullptr;
        SwsContext      *m_swsCtx = nullptr;     // 源 → RGBA
        SwsContext      *m_yuvSwsCtx = nullptr;  // 源 → YUV420P@dst
        int              m_videoStreamIndex = -1;
        AVRational       m_timeBase{0, 1};

        // 硬件解码（VideoToolbox）。m_hwDeviceCtx 非空表示走硬解；
        // 硬解帧在 GPU，需 av_hwframe_transfer_data 取回 m_swFrame（系统内存）。
        AVBufferRef     *m_hwDeviceCtx = nullptr;
        AVPixelFormat    m_hwPixFmt = AV_PIX_FMT_NONE;  // 硬件帧格式（VIDEOTOOLBOX）
        AVFrame         *m_swFrame = nullptr;           // 硬解回传的系统内存帧（复用）

        int    m_width = 0;
        int    m_height = 0;
        int    m_swsW = 0;            // 当前 RGBA sws 上下文匹配的源尺寸/格式
        int    m_swsH = 0;
        AVPixelFormat m_swsFmt = AV_PIX_FMT_NONE;
        // YUV sws 上下文匹配的源尺寸/格式 + 目标尺寸。
        int    m_yuvSrcW = 0, m_yuvSrcH = 0, m_yuvDstW = 0, m_yuvDstH = 0;
        AVPixelFormat m_yuvSrcFmt = AV_PIX_FMT_NONE;
        qint64 m_durationUs = 0;

        // 最近一次解码出的原始帧（YUV/源格式），RGBA 与 YUV 转换均从此出发。
        AVFrame *m_cachedFrame = nullptr;
        QImage m_cachedImage;         // RGBA 路径的缓存（懒转换）
        bool   m_cachedImageValid = false;
        qint64 m_cachedPtsUs = -1;    // 缓存帧的显示时间戳（微秒）
        qint64 m_cachedDurUs = 0;     // 缓存帧的估计持续时长（微秒）
    };

} // namespace Mixed::Player

#endif // MIXEDEDITING_CLIPSOURCE_H
