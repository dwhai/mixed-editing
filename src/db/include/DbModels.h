//
// Created by Anlk on 2026/6/6.
// 剪辑数据模型：与 docs/数据库设计.md 第 4 章的 PC 端表结构一一对应。
// 时间戳为 unix 毫秒；时长/位置为微秒(_us)。可同步表均带同步四件套
// (createdAt/updatedAt/deletedAt/rev)。
//

#ifndef MIXEDEDITING_DBMODELS_H
#define MIXEDEDITING_DBMODELS_H

#include <QString>
#include <cstdint>

namespace Mixed {
namespace DB {

// 同步元数据：所有可同步实体内嵌此结构。
struct SyncMeta {
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    qint64 deletedAt = 0;   // 0 表示未删除
    int    rev = 1;
};

struct Account {
    QString id;
    QString username;
    QString email;
    QString avatarUrl;
    QString authToken;
    qint64  tokenExpireAt = 0;
    SyncMeta meta;
};

struct Project {
    QString id;
    QString accountId;
    QString title = QStringLiteral("未命名工程");
    QString description;
    QString coverPath;
    int     width = 1920;
    int     height = 1080;
    double  fps = 30.0;
    int     sampleRate = 48000;
    qint64  durationUs = 0;
    QString settings;       // JSON
    SyncMeta meta;
};

struct MediaAsset {
    QString id;
    QString projectId;
    QString mediaType;      // video/audio/image
    QString sourceKind;     // local/kaiyan/cloud
    QString filePath;
    QString remoteUrl;
    qint64  kaiyanVideoId = 0;
    QString displayName;
    qint64  durationUs = 0;
    int     width = 0;
    int     height = 0;
    double  fps = 0.0;
    int     sampleRate = 0;
    int     channels = 0;
    QString codec;
    qint64  fileSize = 0;
    QString checksum;
    QString thumbnailPath;
    QString proxyPath;
    QString waveformPath;
    SyncMeta meta;
};

struct Sequence {
    QString id;
    QString projectId;
    QString name = QStringLiteral("时间线 1");
    bool    isMain = true;
    qint64  durationUs = 0;
    SyncMeta meta;
};

struct Track {
    QString id;
    QString sequenceId;
    QString trackType;      // video/audio/text
    int     trackIndex = 0;
    QString name;
    bool    isMuted = false;
    bool    isLocked = false;
    bool    isHidden = false;
    double  volume = 1.0;
    SyncMeta meta;
};

struct Clip {
    QString id;
    QString trackId;
    QString assetId;        // text 片段可空
    QString kind;           // video/audio/image/text
    qint64  timelineStartUs = 0;
    qint64  durationUs = 0;
    qint64  sourceInUs = 0;
    qint64  sourceOutUs = 0;
    double  speed = 1.0;
    double  opacity = 1.0;
    double  volume = 1.0;
    QString transform;      // JSON
    bool    isEnabled = true;
    SyncMeta meta;
};

struct Effect {
    QString id;
    QString ownerType;      // clip/track
    QString ownerId;
    QString effectType;
    int     orderIndex = 0;
    bool    isEnabled = true;
    QString params;         // JSON
    SyncMeta meta;
};

struct Transition {
    QString id;
    QString trackId;
    QString fromClipId;
    QString toClipId;
    QString transitionType;
    qint64  durationUs = 0;
    QString params;         // JSON
    SyncMeta meta;
};

struct Keyframe {
    QString id;
    QString ownerType;      // clip/effect
    QString ownerId;
    QString propertyName;
    qint64  timeOffsetUs = 0;
    QString value;          // JSON
    QString interpolation = QStringLiteral("linear");
    QString bezierHandles;  // JSON
    SyncMeta meta;
};

// 文字/字幕内容（kind='text' 的 clip 关联，clip_id 作主键）。
// 注意：text_element 表只有 updated_at/rev（无 created_at/deleted_at），
// 故此处不用 SyncMeta，单列 updatedAt/rev。
struct TextElement {
    QString clipId;
    QString content;
    QString fontFamily;
    double  fontSize = 0.0;
    QString color;          // '#RRGGBBAA'
    QString alignment;      // left/center/right
    QString style;          // JSON
    qint64  updatedAt = 0;
    int     rev = 1;
};

// 导出/渲染任务（本地执行，不参与云同步，故无 deleted_at/rev）。
struct ExportJob {
    QString id;
    QString projectId;
    QString sequenceId;
    QString status = QStringLiteral("pending"); // pending/running/done/failed/canceled
    QString outputPath;
    QString format = QStringLiteral("mp4");
    QString videoCodec = QStringLiteral("libx264");
    QString audioCodec = QStringLiteral("aac");
    int     width = 0;
    int     height = 0;
    double  fps = 0.0;
    qint64  bitrate = 0;
    double  progress = 0.0;     // 0~1
    QString errorMessage;
    qint64  createdAt = 0;
    qint64  startedAt = 0;
    qint64  finishedAt = 0;
};

} // namespace DB
} // namespace Mixed

#endif // MIXEDEDITING_DBMODELS_H
