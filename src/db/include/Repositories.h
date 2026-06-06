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

} // namespace DB
} // namespace Mixed

#endif // MIXEDEDITING_REPOSITORIES_H
