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

    private:
        bool ensureSws(int srcW, int srcH, AVPixelFormat srcFmt);
        QImage convertToImage(AVFrame *frame);
        // seek 到不晚于 targetUs 的关键帧并刷新解码器。
        void seekTo(qint64 targetUs);

        AVFormatContext *m_fmtCtx = nullptr;
        AVCodecContext  *m_codecCtx = nullptr;
        SwsContext      *m_swsCtx = nullptr;
        int              m_videoStreamIndex = -1;
        AVRational       m_timeBase{0, 1};

        int    m_width = 0;
        int    m_height = 0;
        int    m_swsW = 0;            // 当前 sws 上下文匹配的源尺寸/格式
        int    m_swsH = 0;
        AVPixelFormat m_swsFmt = AV_PIX_FMT_NONE;
        qint64 m_durationUs = 0;

        // 最近一次解码出的帧缓存。
        QImage m_cachedImage;
        qint64 m_cachedPtsUs = -1;    // 缓存帧的显示时间戳（微秒）
        qint64 m_cachedDurUs = 0;     // 缓存帧的估计持续时长（微秒）
    };

} // namespace Mixed::Player

#endif // MIXEDEDITING_CLIPSOURCE_H
