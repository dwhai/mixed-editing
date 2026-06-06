//
// Created by Anlk on 2026/6/6.
// 媒体探测：用 FFmpeg 读取文件元信息（时长/分辨率/帧率/编码/音频参数），
// 供导入素材时回填 MediaAsset。只读不解码，开销很小。
//

#ifndef MIXEDEDITING_MEDIAPROBE_H
#define MIXEDEDITING_MEDIAPROBE_H

#include <QString>

namespace Mixed::Player {

    // 探测结果。durationUs 为微秒，与 DB 层时间单位一致。
    struct MediaInfo {
        bool    ok = false;
        bool    hasVideo = false;
        bool    hasAudio = false;
        qint64  durationUs = 0;
        int     width = 0;
        int     height = 0;
        double  fps = 0.0;
        int     sampleRate = 0;
        int     channels = 0;
        QString videoCodec;   // 首个视频流编码名（无则取音频）
    };

    class MediaProbe {
    public:
        // 探测本地文件或网络 URL。失败返回 ok=false。
        static MediaInfo probe(const QString &url);
    };

} // namespace Mixed::Player

#endif // MIXEDEDITING_MEDIAPROBE_H
