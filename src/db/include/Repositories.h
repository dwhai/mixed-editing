//
// Created by Anlk on 2026/6/6.
// 数据访问层(DAO)：每个实体一个 Repository。所有写操作经 RepositoryBase 的
// trackInsert/trackUpdate/trackDelete 统一维护同步四件套并写入 sync_oplog，
// 供后续 SyncService 增量推送。删除一律软删除。
//

#ifndef MIXEDEDITING_REPOSITORIES_H
#define MIXEDEDITING_REPOSITORIES_H

#include "DbModels.h"
#include <QString>
#include <QSqlQuery>
#include <optional>
#include <vector>

namespace Mixed {
namespace DB {

// 写入 sync_oplog 的公共能力。
class RepositoryBase {
protected:
    // 记一条变更日志。payloadJson 为变更后行快照（delete 时可为空）。
    void writeOplog(const QString& table, const QString& rowId,
                    const QString& op, int rev, const QString& payloadJson);
};

class ProjectRepository : public RepositoryBase {
public:
    // 插入：自动分配 id（若空）、created_at/updated_at/rev，并写 oplog。
    bool insert(Project& p);
    // 全字段更新：自动 bump updated_at/rev，并写 oplog。
    bool update(Project& p);
    // 软删除：置 deleted_at、bump rev，并写 delete oplog。
    bool remove(const QString& id);

    std::optional<Project> findById(const QString& id);
    // 列出某账户下未删除的工程，按更新时间倒序。
    std::vector<Project> listByAccount(const QString& accountId);
};

class MediaAssetRepository : public RepositoryBase {
public:
    bool insert(MediaAsset& a);
    bool update(MediaAsset& a);
    bool remove(const QString& id);

    std::optional<MediaAsset> findById(const QString& id);
    std::vector<MediaAsset> listByProject(const QString& projectId);
};

class SequenceRepository : public RepositoryBase {
public:
    bool insert(Sequence& s);
    bool update(Sequence& s);
    bool remove(const QString& id);

    std::optional<Sequence> findById(const QString& id);
    std::vector<Sequence> listByProject(const QString& projectId);
};

class TrackRepository : public RepositoryBase {
public:
    bool insert(Track& t);
    bool update(Track& t);
    bool remove(const QString& id);

    // 按 track_index 升序返回某序列的未删除轨道。
    std::vector<Track> listBySequence(const QString& sequenceId);
};

class ClipRepository : public RepositoryBase {
public:
    bool insert(Clip& c);
    bool update(Clip& c);
    bool remove(const QString& id);

    std::optional<Clip> findById(const QString& id);
    // 按时间线起点升序返回某轨道的未删除片段。
    std::vector<Clip> listByTrack(const QString& trackId);
};

// 特效：可挂在 clip 或 track 上，按 order_index 组成特效链。
class EffectRepository : public RepositoryBase {
public:
    bool insert(Effect& e);
    bool update(Effect& e);
    bool remove(const QString& id);

    // 按 order_index 升序返回某 owner（clip/track）的未删除特效。
    std::vector<Effect> listByOwner(const QString& ownerType, const QString& ownerId);
};

// 转场：同一轨道相邻两片段之间。
class TransitionRepository : public RepositoryBase {
public:
    bool insert(Transition& t);
    bool update(Transition& t);
    bool remove(const QString& id);

    std::vector<Transition> listByTrack(const QString& trackId);
};

// 关键帧：对 clip 或 effect 的某属性做动画。
class KeyframeRepository : public RepositoryBase {
public:
    bool insert(Keyframe& k);
    bool update(Keyframe& k);
    bool remove(const QString& id);

    // 按 time_offset_us 升序返回某 owner 某属性的未删除关键帧。
    std::vector<Keyframe> listByOwner(const QString& ownerType, const QString& ownerId,
                                      const QString& propertyName);
};

// 文字元素：与 kind='text' 的 clip 一一对应（clip_id 为主键）。
class TextElementRepository : public RepositoryBase {
public:
    // upsert：存在则更新，否则插入。
    bool save(TextElement& t);
    bool remove(const QString& clipId);
    std::optional<TextElement> findByClip(const QString& clipId);
};

// 导出任务：本地执行，不参与云同步（无 oplog）。
class ExportJobRepository {
public:
    bool insert(ExportJob& j);
    bool update(ExportJob& j);

    std::optional<ExportJob> findById(const QString& id);
    std::vector<ExportJob> listByProject(const QString& projectId);
};

} // namespace DB
} // namespace Mixed

#endif // MIXEDEDITING_REPOSITORIES_H
