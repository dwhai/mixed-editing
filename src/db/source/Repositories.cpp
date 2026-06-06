//
// Created by Anlk on 2026/6/6.
// Repository 实现。约定：写操作前由调用方填好业务字段，Repository 负责补全
// id 与同步元数据。所有读取仅返回未删除(deleted_at IS NULL)的行。
//

#include "../include/Repositories.h"
#include "../include/Database.h"

#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>
#include <QDebug>

namespace Mixed {
namespace DB {

namespace {

// deleted_at：0 视为未删除，存 NULL；非 0 存实际时间戳。
QVariant delVar(qint64 deletedAt) {
    return deletedAt == 0 ? QVariant() : QVariant(deletedAt);
}
qint64 delFromVar(const QVariant& v) {
    return v.isNull() ? 0 : v.toLongLong();
}

// 把任意行快照序列化为紧凑 JSON 字符串，写进 oplog.payload。
QString toPayload(const QJsonObject& obj) {
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

} // namespace

// ============ RepositoryBase ============
void RepositoryBase::writeOplog(const QString& table, const QString& rowId,
                                const QString& op, int rev, const QString& payloadJson) {
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral(
        "INSERT INTO sync_oplog(table_name, row_id, op, payload, rev, created_at, synced) "
        "VALUES(?,?,?,?,?,?,0)"));
    q.addBindValue(table);
    q.addBindValue(rowId);
    q.addBindValue(op);
    q.addBindValue(payloadJson.isEmpty() ? QVariant() : payloadJson);
    q.addBindValue(rev);
    q.addBindValue(Database::nowMs());
    if (!q.exec()) {
        qWarning() << "[DB] 写 oplog 失败:" << q.lastError().text();
    }
}

// ============ ProjectRepository ============
namespace {
QJsonObject projectJson(const Project& p) {
    QJsonObject o;
    o["id"] = p.id;
    o["account_id"] = p.accountId;
    o["title"] = p.title;
    o["description"] = p.description;
    o["cover_path"] = p.coverPath;
    o["width"] = p.width;
    o["height"] = p.height;
    o["fps"] = p.fps;
    o["sample_rate"] = p.sampleRate;
    o["duration_us"] = static_cast<double>(p.durationUs);
    o["settings"] = p.settings;
    o["created_at"] = static_cast<double>(p.meta.createdAt);
    o["updated_at"] = static_cast<double>(p.meta.updatedAt);
    o["rev"] = p.meta.rev;
    return o;
}
Project readProject(const QSqlQuery& q) {
    Project p;
    p.id = q.value("id").toString();
    p.accountId = q.value("account_id").toString();
    p.title = q.value("title").toString();
    p.description = q.value("description").toString();
    p.coverPath = q.value("cover_path").toString();
    p.width = q.value("width").toInt();
    p.height = q.value("height").toInt();
    p.fps = q.value("fps").toDouble();
    p.sampleRate = q.value("sample_rate").toInt();
    p.durationUs = q.value("duration_us").toLongLong();
    p.settings = q.value("settings").toString();
    p.meta.createdAt = q.value("created_at").toLongLong();
    p.meta.updatedAt = q.value("updated_at").toLongLong();
    p.meta.deletedAt = delFromVar(q.value("deleted_at"));
    p.meta.rev = q.value("rev").toInt();
    return p;
}
} // namespace

bool ProjectRepository::insert(Project& p) {
    if (p.id.isEmpty()) p.id = Database::newId();
    const qint64 now = Database::nowMs();
    p.meta.createdAt = now;
    p.meta.updatedAt = now;
    p.meta.rev = 1;

    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "INSERT INTO project(id,account_id,title,description,cover_path,width,height,fps,"
            "sample_rate,duration_us,settings,created_at,updated_at,deleted_at,rev) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
        q.addBindValue(p.id);
        q.addBindValue(p.accountId);
        q.addBindValue(p.title);
        q.addBindValue(p.description);
        q.addBindValue(p.coverPath);
        q.addBindValue(p.width);
        q.addBindValue(p.height);
        q.addBindValue(p.fps);
        q.addBindValue(p.sampleRate);
        q.addBindValue(static_cast<qlonglong>(p.durationUs));
        q.addBindValue(p.settings);
        q.addBindValue(p.meta.createdAt);
        q.addBindValue(p.meta.updatedAt);
        q.addBindValue(delVar(p.meta.deletedAt));
        q.addBindValue(p.meta.rev);
        if (!q.exec()) {
            qWarning() << "[DB] project insert 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("project"), p.id, QStringLiteral("insert"),
                   p.meta.rev, toPayload(projectJson(p)));
        return true;
    });
}

bool ProjectRepository::update(Project& p) {
    p.meta.updatedAt = Database::nowMs();
    p.meta.rev += 1;

    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "UPDATE project SET account_id=?,title=?,description=?,cover_path=?,width=?,height=?,"
            "fps=?,sample_rate=?,duration_us=?,settings=?,updated_at=?,rev=? WHERE id=?"));
        q.addBindValue(p.accountId);
        q.addBindValue(p.title);
        q.addBindValue(p.description);
        q.addBindValue(p.coverPath);
        q.addBindValue(p.width);
        q.addBindValue(p.height);
        q.addBindValue(p.fps);
        q.addBindValue(p.sampleRate);
        q.addBindValue(static_cast<qlonglong>(p.durationUs));
        q.addBindValue(p.settings);
        q.addBindValue(p.meta.updatedAt);
        q.addBindValue(p.meta.rev);
        q.addBindValue(p.id);
        if (!q.exec()) {
            qWarning() << "[DB] project update 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("project"), p.id, QStringLiteral("update"),
                   p.meta.rev, toPayload(projectJson(p)));
        return true;
    });
}

bool ProjectRepository::remove(const QString& id) {
    const qint64 now = Database::nowMs();
    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "UPDATE project SET deleted_at=?, updated_at=?, rev=rev+1 WHERE id=?"));
        q.addBindValue(now);
        q.addBindValue(now);
        q.addBindValue(id);
        if (!q.exec()) {
            qWarning() << "[DB] project remove 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("project"), id, QStringLiteral("delete"), 0, QString());
        return true;
    });
}

std::optional<Project> ProjectRepository::findById(const QString& id) {
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("SELECT * FROM project WHERE id=? AND deleted_at IS NULL"));
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        return readProject(q);
    }
    return std::nullopt;
}

std::vector<Project> ProjectRepository::listByAccount(const QString& accountId) {
    std::vector<Project> out;
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral(
        "SELECT * FROM project WHERE account_id=? AND deleted_at IS NULL ORDER BY updated_at DESC"));
    q.addBindValue(accountId);
    if (q.exec()) {
        while (q.next()) out.push_back(readProject(q));
    }
    return out;
}

// ============ MediaAssetRepository ============
namespace {
QJsonObject assetJson(const MediaAsset& a) {
    QJsonObject o;
    o["id"] = a.id;
    o["project_id"] = a.projectId;
    o["media_type"] = a.mediaType;
    o["source_kind"] = a.sourceKind;
    o["file_path"] = a.filePath;
    o["remote_url"] = a.remoteUrl;
    o["kaiyan_video_id"] = static_cast<double>(a.kaiyanVideoId);
    o["display_name"] = a.displayName;
    o["duration_us"] = static_cast<double>(a.durationUs);
    o["width"] = a.width;
    o["height"] = a.height;
    o["fps"] = a.fps;
    o["sample_rate"] = a.sampleRate;
    o["channels"] = a.channels;
    o["codec"] = a.codec;
    o["file_size"] = static_cast<double>(a.fileSize);
    o["checksum"] = a.checksum;
    o["thumbnail_path"] = a.thumbnailPath;
    o["proxy_path"] = a.proxyPath;
    o["waveform_path"] = a.waveformPath;
    o["created_at"] = static_cast<double>(a.meta.createdAt);
    o["updated_at"] = static_cast<double>(a.meta.updatedAt);
    o["rev"] = a.meta.rev;
    return o;
}
MediaAsset readAsset(const QSqlQuery& q) {
    MediaAsset a;
    a.id = q.value("id").toString();
    a.projectId = q.value("project_id").toString();
    a.mediaType = q.value("media_type").toString();
    a.sourceKind = q.value("source_kind").toString();
    a.filePath = q.value("file_path").toString();
    a.remoteUrl = q.value("remote_url").toString();
    a.kaiyanVideoId = q.value("kaiyan_video_id").toLongLong();
    a.displayName = q.value("display_name").toString();
    a.durationUs = q.value("duration_us").toLongLong();
    a.width = q.value("width").toInt();
    a.height = q.value("height").toInt();
    a.fps = q.value("fps").toDouble();
    a.sampleRate = q.value("sample_rate").toInt();
    a.channels = q.value("channels").toInt();
    a.codec = q.value("codec").toString();
    a.fileSize = q.value("file_size").toLongLong();
    a.checksum = q.value("checksum").toString();
    a.thumbnailPath = q.value("thumbnail_path").toString();
    a.proxyPath = q.value("proxy_path").toString();
    a.waveformPath = q.value("waveform_path").toString();
    a.meta.createdAt = q.value("created_at").toLongLong();
    a.meta.updatedAt = q.value("updated_at").toLongLong();
    a.meta.deletedAt = delFromVar(q.value("deleted_at"));
    a.meta.rev = q.value("rev").toInt();
    return a;
}
void bindAssetBody(QSqlQuery& q, const MediaAsset& a) {
    q.addBindValue(a.projectId);
    q.addBindValue(a.mediaType);
    q.addBindValue(a.sourceKind);
    q.addBindValue(a.filePath);
    q.addBindValue(a.remoteUrl);
    q.addBindValue(static_cast<qlonglong>(a.kaiyanVideoId));
    q.addBindValue(a.displayName);
    q.addBindValue(static_cast<qlonglong>(a.durationUs));
    q.addBindValue(a.width);
    q.addBindValue(a.height);
    q.addBindValue(a.fps);
    q.addBindValue(a.sampleRate);
    q.addBindValue(a.channels);
    q.addBindValue(a.codec);
    q.addBindValue(static_cast<qlonglong>(a.fileSize));
    q.addBindValue(a.checksum);
    q.addBindValue(a.thumbnailPath);
    q.addBindValue(a.proxyPath);
    q.addBindValue(a.waveformPath);
}
} // namespace

bool MediaAssetRepository::insert(MediaAsset& a) {
    if (a.id.isEmpty()) a.id = Database::newId();
    const qint64 now = Database::nowMs();
    a.meta.createdAt = now;
    a.meta.updatedAt = now;
    a.meta.rev = 1;

    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "INSERT INTO media_asset(project_id,media_type,source_kind,file_path,remote_url,"
            "kaiyan_video_id,display_name,duration_us,width,height,fps,sample_rate,channels,codec,"
            "file_size,checksum,thumbnail_path,proxy_path,waveform_path,"
            "id,created_at,updated_at,deleted_at,rev) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
        bindAssetBody(q, a);
        q.addBindValue(a.id);
        q.addBindValue(a.meta.createdAt);
        q.addBindValue(a.meta.updatedAt);
        q.addBindValue(delVar(a.meta.deletedAt));
        q.addBindValue(a.meta.rev);
        if (!q.exec()) {
            qWarning() << "[DB] asset insert 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("media_asset"), a.id, QStringLiteral("insert"),
                   a.meta.rev, toPayload(assetJson(a)));
        return true;
    });
}

bool MediaAssetRepository::update(MediaAsset& a) {
    a.meta.updatedAt = Database::nowMs();
    a.meta.rev += 1;

    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "UPDATE media_asset SET project_id=?,media_type=?,source_kind=?,file_path=?,remote_url=?,"
            "kaiyan_video_id=?,display_name=?,duration_us=?,width=?,height=?,fps=?,sample_rate=?,"
            "channels=?,codec=?,file_size=?,checksum=?,thumbnail_path=?,proxy_path=?,waveform_path=?,"
            "updated_at=?,rev=? WHERE id=?"));
        bindAssetBody(q, a);
        q.addBindValue(a.meta.updatedAt);
        q.addBindValue(a.meta.rev);
        q.addBindValue(a.id);
        if (!q.exec()) {
            qWarning() << "[DB] asset update 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("media_asset"), a.id, QStringLiteral("update"),
                   a.meta.rev, toPayload(assetJson(a)));
        return true;
    });
}

bool MediaAssetRepository::remove(const QString& id) {
    const qint64 now = Database::nowMs();
    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "UPDATE media_asset SET deleted_at=?, updated_at=?, rev=rev+1 WHERE id=?"));
        q.addBindValue(now);
        q.addBindValue(now);
        q.addBindValue(id);
        if (!q.exec()) {
            qWarning() << "[DB] asset remove 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("media_asset"), id, QStringLiteral("delete"), 0, QString());
        return true;
    });
}

std::optional<MediaAsset> MediaAssetRepository::findById(const QString& id) {
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("SELECT * FROM media_asset WHERE id=? AND deleted_at IS NULL"));
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        return readAsset(q);
    }
    return std::nullopt;
}

std::vector<MediaAsset> MediaAssetRepository::listByProject(const QString& projectId) {
    std::vector<MediaAsset> out;
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral(
        "SELECT * FROM media_asset WHERE project_id=? AND deleted_at IS NULL ORDER BY created_at"));
    q.addBindValue(projectId);
    if (q.exec()) {
        while (q.next()) out.push_back(readAsset(q));
    }
    return out;
}

// ============ SequenceRepository ============
namespace {
QJsonObject sequenceJson(const Sequence& s) {
    QJsonObject o;
    o["id"] = s.id;
    o["project_id"] = s.projectId;
    o["name"] = s.name;
    o["is_main"] = s.isMain ? 1 : 0;
    o["duration_us"] = static_cast<double>(s.durationUs);
    o["created_at"] = static_cast<double>(s.meta.createdAt);
    o["updated_at"] = static_cast<double>(s.meta.updatedAt);
    o["rev"] = s.meta.rev;
    return o;
}
Sequence readSequence(const QSqlQuery& q) {
    Sequence s;
    s.id = q.value("id").toString();
    s.projectId = q.value("project_id").toString();
    s.name = q.value("name").toString();
    s.isMain = q.value("is_main").toInt() != 0;
    s.durationUs = q.value("duration_us").toLongLong();
    s.meta.createdAt = q.value("created_at").toLongLong();
    s.meta.updatedAt = q.value("updated_at").toLongLong();
    s.meta.deletedAt = delFromVar(q.value("deleted_at"));
    s.meta.rev = q.value("rev").toInt();
    return s;
}
} // namespace

bool SequenceRepository::insert(Sequence& s) {
    if (s.id.isEmpty()) s.id = Database::newId();
    const qint64 now = Database::nowMs();
    s.meta.createdAt = now;
    s.meta.updatedAt = now;
    s.meta.rev = 1;

    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "INSERT INTO sequence(id,project_id,name,is_main,duration_us,"
            "created_at,updated_at,deleted_at,rev) VALUES(?,?,?,?,?,?,?,?,?)"));
        q.addBindValue(s.id);
        q.addBindValue(s.projectId);
        q.addBindValue(s.name);
        q.addBindValue(s.isMain ? 1 : 0);
        q.addBindValue(static_cast<qlonglong>(s.durationUs));
        q.addBindValue(s.meta.createdAt);
        q.addBindValue(s.meta.updatedAt);
        q.addBindValue(delVar(s.meta.deletedAt));
        q.addBindValue(s.meta.rev);
        if (!q.exec()) {
            qWarning() << "[DB] sequence insert 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("sequence"), s.id, QStringLiteral("insert"),
                   s.meta.rev, toPayload(sequenceJson(s)));
        return true;
    });
}

bool SequenceRepository::update(Sequence& s) {
    s.meta.updatedAt = Database::nowMs();
    s.meta.rev += 1;

    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "UPDATE sequence SET project_id=?,name=?,is_main=?,duration_us=?,"
            "updated_at=?,rev=? WHERE id=?"));
        q.addBindValue(s.projectId);
        q.addBindValue(s.name);
        q.addBindValue(s.isMain ? 1 : 0);
        q.addBindValue(static_cast<qlonglong>(s.durationUs));
        q.addBindValue(s.meta.updatedAt);
        q.addBindValue(s.meta.rev);
        q.addBindValue(s.id);
        if (!q.exec()) {
            qWarning() << "[DB] sequence update 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("sequence"), s.id, QStringLiteral("update"),
                   s.meta.rev, toPayload(sequenceJson(s)));
        return true;
    });
}

bool SequenceRepository::remove(const QString& id) {
    const qint64 now = Database::nowMs();
    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "UPDATE sequence SET deleted_at=?, updated_at=?, rev=rev+1 WHERE id=?"));
        q.addBindValue(now);
        q.addBindValue(now);
        q.addBindValue(id);
        if (!q.exec()) {
            qWarning() << "[DB] sequence remove 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("sequence"), id, QStringLiteral("delete"), 0, QString());
        return true;
    });
}

std::optional<Sequence> SequenceRepository::findById(const QString& id) {
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("SELECT * FROM sequence WHERE id=? AND deleted_at IS NULL"));
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        return readSequence(q);
    }
    return std::nullopt;
}

std::vector<Sequence> SequenceRepository::listByProject(const QString& projectId) {
    std::vector<Sequence> out;
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral(
        "SELECT * FROM sequence WHERE project_id=? AND deleted_at IS NULL "
        "ORDER BY is_main DESC, created_at"));
    q.addBindValue(projectId);
    if (q.exec()) {
        while (q.next()) out.push_back(readSequence(q));
    }
    return out;
}

// ============ TrackRepository ============
namespace {
QJsonObject trackJson(const Track& t) {
    QJsonObject o;
    o["id"] = t.id;
    o["sequence_id"] = t.sequenceId;
    o["track_type"] = t.trackType;
    o["track_index"] = t.trackIndex;
    o["name"] = t.name;
    o["is_muted"] = t.isMuted ? 1 : 0;
    o["is_locked"] = t.isLocked ? 1 : 0;
    o["is_hidden"] = t.isHidden ? 1 : 0;
    o["volume"] = t.volume;
    o["created_at"] = static_cast<double>(t.meta.createdAt);
    o["updated_at"] = static_cast<double>(t.meta.updatedAt);
    o["rev"] = t.meta.rev;
    return o;
}
Track readTrack(const QSqlQuery& q) {
    Track t;
    t.id = q.value("id").toString();
    t.sequenceId = q.value("sequence_id").toString();
    t.trackType = q.value("track_type").toString();
    t.trackIndex = q.value("track_index").toInt();
    t.name = q.value("name").toString();
    t.isMuted = q.value("is_muted").toInt() != 0;
    t.isLocked = q.value("is_locked").toInt() != 0;
    t.isHidden = q.value("is_hidden").toInt() != 0;
    t.volume = q.value("volume").toDouble();
    t.meta.createdAt = q.value("created_at").toLongLong();
    t.meta.updatedAt = q.value("updated_at").toLongLong();
    t.meta.deletedAt = delFromVar(q.value("deleted_at"));
    t.meta.rev = q.value("rev").toInt();
    return t;
}
} // namespace

bool TrackRepository::insert(Track& t) {
    if (t.id.isEmpty()) t.id = Database::newId();
    const qint64 now = Database::nowMs();
    t.meta.createdAt = now;
    t.meta.updatedAt = now;
    t.meta.rev = 1;

    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "INSERT INTO track(id,sequence_id,track_type,track_index,name,is_muted,is_locked,"
            "is_hidden,volume,created_at,updated_at,deleted_at,rev) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)"));
        q.addBindValue(t.id);
        q.addBindValue(t.sequenceId);
        q.addBindValue(t.trackType);
        q.addBindValue(t.trackIndex);
        q.addBindValue(t.name);
        q.addBindValue(t.isMuted ? 1 : 0);
        q.addBindValue(t.isLocked ? 1 : 0);
        q.addBindValue(t.isHidden ? 1 : 0);
        q.addBindValue(t.volume);
        q.addBindValue(t.meta.createdAt);
        q.addBindValue(t.meta.updatedAt);
        q.addBindValue(delVar(t.meta.deletedAt));
        q.addBindValue(t.meta.rev);
        if (!q.exec()) {
            qWarning() << "[DB] track insert 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("track"), t.id, QStringLiteral("insert"),
                   t.meta.rev, toPayload(trackJson(t)));
        return true;
    });
}

bool TrackRepository::update(Track& t) {
    t.meta.updatedAt = Database::nowMs();
    t.meta.rev += 1;

    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "UPDATE track SET sequence_id=?,track_type=?,track_index=?,name=?,is_muted=?,"
            "is_locked=?,is_hidden=?,volume=?,updated_at=?,rev=? WHERE id=?"));
        q.addBindValue(t.sequenceId);
        q.addBindValue(t.trackType);
        q.addBindValue(t.trackIndex);
        q.addBindValue(t.name);
        q.addBindValue(t.isMuted ? 1 : 0);
        q.addBindValue(t.isLocked ? 1 : 0);
        q.addBindValue(t.isHidden ? 1 : 0);
        q.addBindValue(t.volume);
        q.addBindValue(t.meta.updatedAt);
        q.addBindValue(t.meta.rev);
        q.addBindValue(t.id);
        if (!q.exec()) {
            qWarning() << "[DB] track update 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("track"), t.id, QStringLiteral("update"),
                   t.meta.rev, toPayload(trackJson(t)));
        return true;
    });
}

bool TrackRepository::remove(const QString& id) {
    const qint64 now = Database::nowMs();
    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "UPDATE track SET deleted_at=?, updated_at=?, rev=rev+1 WHERE id=?"));
        q.addBindValue(now);
        q.addBindValue(now);
        q.addBindValue(id);
        if (!q.exec()) {
            qWarning() << "[DB] track remove 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("track"), id, QStringLiteral("delete"), 0, QString());
        return true;
    });
}

std::vector<Track> TrackRepository::listBySequence(const QString& sequenceId) {
    std::vector<Track> out;
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral(
        "SELECT * FROM track WHERE sequence_id=? AND deleted_at IS NULL ORDER BY track_index"));
    q.addBindValue(sequenceId);
    if (q.exec()) {
        while (q.next()) out.push_back(readTrack(q));
    }
    return out;
}

// ============ ClipRepository ============
namespace {
QJsonObject clipJson(const Clip& c) {
    QJsonObject o;
    o["id"] = c.id;
    o["track_id"] = c.trackId;
    o["asset_id"] = c.assetId;
    o["kind"] = c.kind;
    o["timeline_start_us"] = static_cast<double>(c.timelineStartUs);
    o["duration_us"] = static_cast<double>(c.durationUs);
    o["source_in_us"] = static_cast<double>(c.sourceInUs);
    o["source_out_us"] = static_cast<double>(c.sourceOutUs);
    o["speed"] = c.speed;
    o["opacity"] = c.opacity;
    o["volume"] = c.volume;
    o["transform"] = c.transform;
    o["is_enabled"] = c.isEnabled ? 1 : 0;
    o["created_at"] = static_cast<double>(c.meta.createdAt);
    o["updated_at"] = static_cast<double>(c.meta.updatedAt);
    o["rev"] = c.meta.rev;
    return o;
}
Clip readClip(const QSqlQuery& q) {
    Clip c;
    c.id = q.value("id").toString();
    c.trackId = q.value("track_id").toString();
    c.assetId = q.value("asset_id").toString();
    c.kind = q.value("kind").toString();
    c.timelineStartUs = q.value("timeline_start_us").toLongLong();
    c.durationUs = q.value("duration_us").toLongLong();
    c.sourceInUs = q.value("source_in_us").toLongLong();
    c.sourceOutUs = q.value("source_out_us").toLongLong();
    c.speed = q.value("speed").toDouble();
    c.opacity = q.value("opacity").toDouble();
    c.volume = q.value("volume").toDouble();
    c.transform = q.value("transform").toString();
    c.isEnabled = q.value("is_enabled").toInt() != 0;
    c.meta.createdAt = q.value("created_at").toLongLong();
    c.meta.updatedAt = q.value("updated_at").toLongLong();
    c.meta.deletedAt = delFromVar(q.value("deleted_at"));
    c.meta.rev = q.value("rev").toInt();
    return c;
}
// asset_id 为空时存 NULL（外键允许 text 片段无素材）。
QVariant assetIdVar(const QString& assetId) {
    return assetId.isEmpty() ? QVariant() : QVariant(assetId);
}
} // namespace

bool ClipRepository::insert(Clip& c) {
    if (c.id.isEmpty()) c.id = Database::newId();
    const qint64 now = Database::nowMs();
    c.meta.createdAt = now;
    c.meta.updatedAt = now;
    c.meta.rev = 1;

    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "INSERT INTO clip(id,track_id,asset_id,kind,timeline_start_us,duration_us,"
            "source_in_us,source_out_us,speed,opacity,volume,transform,is_enabled,"
            "created_at,updated_at,deleted_at,rev) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
        q.addBindValue(c.id);
        q.addBindValue(c.trackId);
        q.addBindValue(assetIdVar(c.assetId));
        q.addBindValue(c.kind);
        q.addBindValue(static_cast<qlonglong>(c.timelineStartUs));
        q.addBindValue(static_cast<qlonglong>(c.durationUs));
        q.addBindValue(static_cast<qlonglong>(c.sourceInUs));
        q.addBindValue(static_cast<qlonglong>(c.sourceOutUs));
        q.addBindValue(c.speed);
        q.addBindValue(c.opacity);
        q.addBindValue(c.volume);
        q.addBindValue(c.transform);
        q.addBindValue(c.isEnabled ? 1 : 0);
        q.addBindValue(c.meta.createdAt);
        q.addBindValue(c.meta.updatedAt);
        q.addBindValue(delVar(c.meta.deletedAt));
        q.addBindValue(c.meta.rev);
        if (!q.exec()) {
            qWarning() << "[DB] clip insert 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("clip"), c.id, QStringLiteral("insert"),
                   c.meta.rev, toPayload(clipJson(c)));
        return true;
    });
}

bool ClipRepository::update(Clip& c) {
    c.meta.updatedAt = Database::nowMs();
    c.meta.rev += 1;

    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "UPDATE clip SET track_id=?,asset_id=?,kind=?,timeline_start_us=?,duration_us=?,"
            "source_in_us=?,source_out_us=?,speed=?,opacity=?,volume=?,transform=?,is_enabled=?,"
            "updated_at=?,rev=? WHERE id=?"));
        q.addBindValue(c.trackId);
        q.addBindValue(assetIdVar(c.assetId));
        q.addBindValue(c.kind);
        q.addBindValue(static_cast<qlonglong>(c.timelineStartUs));
        q.addBindValue(static_cast<qlonglong>(c.durationUs));
        q.addBindValue(static_cast<qlonglong>(c.sourceInUs));
        q.addBindValue(static_cast<qlonglong>(c.sourceOutUs));
        q.addBindValue(c.speed);
        q.addBindValue(c.opacity);
        q.addBindValue(c.volume);
        q.addBindValue(c.transform);
        q.addBindValue(c.isEnabled ? 1 : 0);
        q.addBindValue(c.meta.updatedAt);
        q.addBindValue(c.meta.rev);
        q.addBindValue(c.id);
        if (!q.exec()) {
            qWarning() << "[DB] clip update 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("clip"), c.id, QStringLiteral("update"),
                   c.meta.rev, toPayload(clipJson(c)));
        return true;
    });
}

bool ClipRepository::remove(const QString& id) {
    const qint64 now = Database::nowMs();
    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "UPDATE clip SET deleted_at=?, updated_at=?, rev=rev+1 WHERE id=?"));
        q.addBindValue(now);
        q.addBindValue(now);
        q.addBindValue(id);
        if (!q.exec()) {
            qWarning() << "[DB] clip remove 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("clip"), id, QStringLiteral("delete"), 0, QString());
        return true;
    });
}

std::optional<Clip> ClipRepository::findById(const QString& id) {
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("SELECT * FROM clip WHERE id=? AND deleted_at IS NULL"));
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        return readClip(q);
    }
    return std::nullopt;
}

std::vector<Clip> ClipRepository::listByTrack(const QString& trackId) {
    std::vector<Clip> out;
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral(
        "SELECT * FROM clip WHERE track_id=? AND deleted_at IS NULL ORDER BY timeline_start_us"));
    q.addBindValue(trackId);
    if (q.exec()) {
        while (q.next()) out.push_back(readClip(q));
    }
    return out;
}

// ============ EffectRepository ============
namespace {
QJsonObject effectJson(const Effect& e) {
    QJsonObject o;
    o["id"] = e.id;
    o["owner_type"] = e.ownerType;
    o["owner_id"] = e.ownerId;
    o["effect_type"] = e.effectType;
    o["order_index"] = e.orderIndex;
    o["is_enabled"] = e.isEnabled ? 1 : 0;
    o["params"] = e.params;
    o["created_at"] = static_cast<double>(e.meta.createdAt);
    o["updated_at"] = static_cast<double>(e.meta.updatedAt);
    o["rev"] = e.meta.rev;
    return o;
}
Effect readEffect(const QSqlQuery& q) {
    Effect e;
    e.id = q.value("id").toString();
    e.ownerType = q.value("owner_type").toString();
    e.ownerId = q.value("owner_id").toString();
    e.effectType = q.value("effect_type").toString();
    e.orderIndex = q.value("order_index").toInt();
    e.isEnabled = q.value("is_enabled").toInt() != 0;
    e.params = q.value("params").toString();
    e.meta.createdAt = q.value("created_at").toLongLong();
    e.meta.updatedAt = q.value("updated_at").toLongLong();
    e.meta.deletedAt = delFromVar(q.value("deleted_at"));
    e.meta.rev = q.value("rev").toInt();
    return e;
}
} // namespace

bool EffectRepository::insert(Effect& e) {
    if (e.id.isEmpty()) e.id = Database::newId();
    const qint64 now = Database::nowMs();
    e.meta.createdAt = now;
    e.meta.updatedAt = now;
    e.meta.rev = 1;

    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "INSERT INTO effect(id,owner_type,owner_id,effect_type,order_index,is_enabled,params,"
            "created_at,updated_at,deleted_at,rev) VALUES(?,?,?,?,?,?,?,?,?,?,?)"));
        q.addBindValue(e.id);
        q.addBindValue(e.ownerType);
        q.addBindValue(e.ownerId);
        q.addBindValue(e.effectType);
        q.addBindValue(e.orderIndex);
        q.addBindValue(e.isEnabled ? 1 : 0);
        q.addBindValue(e.params);
        q.addBindValue(e.meta.createdAt);
        q.addBindValue(e.meta.updatedAt);
        q.addBindValue(delVar(e.meta.deletedAt));
        q.addBindValue(e.meta.rev);
        if (!q.exec()) {
            qWarning() << "[DB] effect insert 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("effect"), e.id, QStringLiteral("insert"),
                   e.meta.rev, toPayload(effectJson(e)));
        return true;
    });
}

bool EffectRepository::update(Effect& e) {
    e.meta.updatedAt = Database::nowMs();
    e.meta.rev += 1;

    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "UPDATE effect SET owner_type=?,owner_id=?,effect_type=?,order_index=?,is_enabled=?,"
            "params=?,updated_at=?,rev=? WHERE id=?"));
        q.addBindValue(e.ownerType);
        q.addBindValue(e.ownerId);
        q.addBindValue(e.effectType);
        q.addBindValue(e.orderIndex);
        q.addBindValue(e.isEnabled ? 1 : 0);
        q.addBindValue(e.params);
        q.addBindValue(e.meta.updatedAt);
        q.addBindValue(e.meta.rev);
        q.addBindValue(e.id);
        if (!q.exec()) {
            qWarning() << "[DB] effect update 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("effect"), e.id, QStringLiteral("update"),
                   e.meta.rev, toPayload(effectJson(e)));
        return true;
    });
}

bool EffectRepository::remove(const QString& id) {
    const qint64 now = Database::nowMs();
    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "UPDATE effect SET deleted_at=?, updated_at=?, rev=rev+1 WHERE id=?"));
        q.addBindValue(now);
        q.addBindValue(now);
        q.addBindValue(id);
        if (!q.exec()) {
            qWarning() << "[DB] effect remove 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("effect"), id, QStringLiteral("delete"), 0, QString());
        return true;
    });
}

std::vector<Effect> EffectRepository::listByOwner(const QString& ownerType, const QString& ownerId) {
    std::vector<Effect> out;
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral(
        "SELECT * FROM effect WHERE owner_type=? AND owner_id=? AND deleted_at IS NULL "
        "ORDER BY order_index"));
    q.addBindValue(ownerType);
    q.addBindValue(ownerId);
    if (q.exec()) {
        while (q.next()) out.push_back(readEffect(q));
    }
    return out;
}

// ============ TransitionRepository ============
namespace {
QJsonObject transitionJson(const Transition& t) {
    QJsonObject o;
    o["id"] = t.id;
    o["track_id"] = t.trackId;
    o["from_clip_id"] = t.fromClipId;
    o["to_clip_id"] = t.toClipId;
    o["transition_type"] = t.transitionType;
    o["duration_us"] = static_cast<double>(t.durationUs);
    o["params"] = t.params;
    o["created_at"] = static_cast<double>(t.meta.createdAt);
    o["updated_at"] = static_cast<double>(t.meta.updatedAt);
    o["rev"] = t.meta.rev;
    return o;
}
Transition readTransition(const QSqlQuery& q) {
    Transition t;
    t.id = q.value("id").toString();
    t.trackId = q.value("track_id").toString();
    t.fromClipId = q.value("from_clip_id").toString();
    t.toClipId = q.value("to_clip_id").toString();
    t.transitionType = q.value("transition_type").toString();
    t.durationUs = q.value("duration_us").toLongLong();
    t.params = q.value("params").toString();
    t.meta.createdAt = q.value("created_at").toLongLong();
    t.meta.updatedAt = q.value("updated_at").toLongLong();
    t.meta.deletedAt = delFromVar(q.value("deleted_at"));
    t.meta.rev = q.value("rev").toInt();
    return t;
}
// from/to clip 为空时存 NULL（片段开头/结尾的转场允许缺一端）。
QVariant clipIdVar(const QString& clipId) {
    return clipId.isEmpty() ? QVariant() : QVariant(clipId);
}
} // namespace

bool TransitionRepository::insert(Transition& t) {
    if (t.id.isEmpty()) t.id = Database::newId();
    const qint64 now = Database::nowMs();
    t.meta.createdAt = now;
    t.meta.updatedAt = now;
    t.meta.rev = 1;

    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "INSERT INTO transition(id,track_id,from_clip_id,to_clip_id,transition_type,"
            "duration_us,params,created_at,updated_at,deleted_at,rev) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?)"));
        q.addBindValue(t.id);
        q.addBindValue(t.trackId);
        q.addBindValue(clipIdVar(t.fromClipId));
        q.addBindValue(clipIdVar(t.toClipId));
        q.addBindValue(t.transitionType);
        q.addBindValue(static_cast<qlonglong>(t.durationUs));
        q.addBindValue(t.params);
        q.addBindValue(t.meta.createdAt);
        q.addBindValue(t.meta.updatedAt);
        q.addBindValue(delVar(t.meta.deletedAt));
        q.addBindValue(t.meta.rev);
        if (!q.exec()) {
            qWarning() << "[DB] transition insert 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("transition"), t.id, QStringLiteral("insert"),
                   t.meta.rev, toPayload(transitionJson(t)));
        return true;
    });
}

bool TransitionRepository::update(Transition& t) {
    t.meta.updatedAt = Database::nowMs();
    t.meta.rev += 1;

    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "UPDATE transition SET track_id=?,from_clip_id=?,to_clip_id=?,transition_type=?,"
            "duration_us=?,params=?,updated_at=?,rev=? WHERE id=?"));
        q.addBindValue(t.trackId);
        q.addBindValue(clipIdVar(t.fromClipId));
        q.addBindValue(clipIdVar(t.toClipId));
        q.addBindValue(t.transitionType);
        q.addBindValue(static_cast<qlonglong>(t.durationUs));
        q.addBindValue(t.params);
        q.addBindValue(t.meta.updatedAt);
        q.addBindValue(t.meta.rev);
        q.addBindValue(t.id);
        if (!q.exec()) {
            qWarning() << "[DB] transition update 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("transition"), t.id, QStringLiteral("update"),
                   t.meta.rev, toPayload(transitionJson(t)));
        return true;
    });
}

bool TransitionRepository::remove(const QString& id) {
    const qint64 now = Database::nowMs();
    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "UPDATE transition SET deleted_at=?, updated_at=?, rev=rev+1 WHERE id=?"));
        q.addBindValue(now);
        q.addBindValue(now);
        q.addBindValue(id);
        if (!q.exec()) {
            qWarning() << "[DB] transition remove 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("transition"), id, QStringLiteral("delete"), 0, QString());
        return true;
    });
}

std::vector<Transition> TransitionRepository::listByTrack(const QString& trackId) {
    std::vector<Transition> out;
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral(
        "SELECT * FROM transition WHERE track_id=? AND deleted_at IS NULL ORDER BY created_at"));
    q.addBindValue(trackId);
    if (q.exec()) {
        while (q.next()) out.push_back(readTransition(q));
    }
    return out;
}

// ============ KeyframeRepository ============
namespace {
QJsonObject keyframeJson(const Keyframe& k) {
    QJsonObject o;
    o["id"] = k.id;
    o["owner_type"] = k.ownerType;
    o["owner_id"] = k.ownerId;
    o["property_name"] = k.propertyName;
    o["time_offset_us"] = static_cast<double>(k.timeOffsetUs);
    o["value"] = k.value;
    o["interpolation"] = k.interpolation;
    o["bezier_handles"] = k.bezierHandles;
    o["created_at"] = static_cast<double>(k.meta.createdAt);
    o["updated_at"] = static_cast<double>(k.meta.updatedAt);
    o["rev"] = k.meta.rev;
    return o;
}
Keyframe readKeyframe(const QSqlQuery& q) {
    Keyframe k;
    k.id = q.value("id").toString();
    k.ownerType = q.value("owner_type").toString();
    k.ownerId = q.value("owner_id").toString();
    k.propertyName = q.value("property_name").toString();
    k.timeOffsetUs = q.value("time_offset_us").toLongLong();
    k.value = q.value("value").toString();
    k.interpolation = q.value("interpolation").toString();
    k.bezierHandles = q.value("bezier_handles").toString();
    k.meta.createdAt = q.value("created_at").toLongLong();
    k.meta.updatedAt = q.value("updated_at").toLongLong();
    k.meta.deletedAt = delFromVar(q.value("deleted_at"));
    k.meta.rev = q.value("rev").toInt();
    return k;
}
} // namespace

bool KeyframeRepository::insert(Keyframe& k) {
    if (k.id.isEmpty()) k.id = Database::newId();
    const qint64 now = Database::nowMs();
    k.meta.createdAt = now;
    k.meta.updatedAt = now;
    k.meta.rev = 1;

    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "INSERT INTO keyframe(id,owner_type,owner_id,property_name,time_offset_us,value,"
            "interpolation,bezier_handles,created_at,updated_at,deleted_at,rev) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"));
        q.addBindValue(k.id);
        q.addBindValue(k.ownerType);
        q.addBindValue(k.ownerId);
        q.addBindValue(k.propertyName);
        q.addBindValue(static_cast<qlonglong>(k.timeOffsetUs));
        q.addBindValue(k.value);
        q.addBindValue(k.interpolation);
        q.addBindValue(k.bezierHandles);
        q.addBindValue(k.meta.createdAt);
        q.addBindValue(k.meta.updatedAt);
        q.addBindValue(delVar(k.meta.deletedAt));
        q.addBindValue(k.meta.rev);
        if (!q.exec()) {
            qWarning() << "[DB] keyframe insert 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("keyframe"), k.id, QStringLiteral("insert"),
                   k.meta.rev, toPayload(keyframeJson(k)));
        return true;
    });
}

bool KeyframeRepository::update(Keyframe& k) {
    k.meta.updatedAt = Database::nowMs();
    k.meta.rev += 1;

    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "UPDATE keyframe SET owner_type=?,owner_id=?,property_name=?,time_offset_us=?,value=?,"
            "interpolation=?,bezier_handles=?,updated_at=?,rev=? WHERE id=?"));
        q.addBindValue(k.ownerType);
        q.addBindValue(k.ownerId);
        q.addBindValue(k.propertyName);
        q.addBindValue(static_cast<qlonglong>(k.timeOffsetUs));
        q.addBindValue(k.value);
        q.addBindValue(k.interpolation);
        q.addBindValue(k.bezierHandles);
        q.addBindValue(k.meta.updatedAt);
        q.addBindValue(k.meta.rev);
        q.addBindValue(k.id);
        if (!q.exec()) {
            qWarning() << "[DB] keyframe update 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("keyframe"), k.id, QStringLiteral("update"),
                   k.meta.rev, toPayload(keyframeJson(k)));
        return true;
    });
}

bool KeyframeRepository::remove(const QString& id) {
    const qint64 now = Database::nowMs();
    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "UPDATE keyframe SET deleted_at=?, updated_at=?, rev=rev+1 WHERE id=?"));
        q.addBindValue(now);
        q.addBindValue(now);
        q.addBindValue(id);
        if (!q.exec()) {
            qWarning() << "[DB] keyframe remove 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("keyframe"), id, QStringLiteral("delete"), 0, QString());
        return true;
    });
}

std::vector<Keyframe> KeyframeRepository::listByOwner(const QString& ownerType,
                                                      const QString& ownerId,
                                                      const QString& propertyName) {
    std::vector<Keyframe> out;
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral(
        "SELECT * FROM keyframe WHERE owner_type=? AND owner_id=? AND property_name=? "
        "AND deleted_at IS NULL ORDER BY time_offset_us"));
    q.addBindValue(ownerType);
    q.addBindValue(ownerId);
    q.addBindValue(propertyName);
    if (q.exec()) {
        while (q.next()) out.push_back(readKeyframe(q));
    }
    return out;
}

// ============ TextElementRepository ============
namespace {
QJsonObject textJson(const TextElement& t) {
    QJsonObject o;
    o["clip_id"] = t.clipId;
    o["content"] = t.content;
    o["font_family"] = t.fontFamily;
    o["font_size"] = t.fontSize;
    o["color"] = t.color;
    o["alignment"] = t.alignment;
    o["style"] = t.style;
    o["updated_at"] = static_cast<double>(t.updatedAt);
    o["rev"] = t.rev;
    return o;
}
TextElement readText(const QSqlQuery& q) {
    TextElement t;
    t.clipId = q.value("clip_id").toString();
    t.content = q.value("content").toString();
    t.fontFamily = q.value("font_family").toString();
    t.fontSize = q.value("font_size").toDouble();
    t.color = q.value("color").toString();
    t.alignment = q.value("alignment").toString();
    t.style = q.value("style").toString();
    t.updatedAt = q.value("updated_at").toLongLong();
    t.rev = q.value("rev").toInt();
    return t;
}
} // namespace

bool TextElementRepository::save(TextElement& t) {
    t.updatedAt = Database::nowMs();
    // 已存在则 rev+1，否则从 1 起。
    bool exists = false;
    {
        QSqlQuery probe(Database::instance().db());
        probe.prepare(QStringLiteral("SELECT rev FROM text_element WHERE clip_id=?"));
        probe.addBindValue(t.clipId);
        if (probe.exec() && probe.next()) {
            exists = true;
            t.rev = probe.value(0).toInt() + 1;
        } else {
            t.rev = 1;
        }
    }

    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral(
            "INSERT INTO text_element(clip_id,content,font_family,font_size,color,alignment,style,"
            "updated_at,rev) VALUES(?,?,?,?,?,?,?,?,?) "
            "ON CONFLICT(clip_id) DO UPDATE SET content=excluded.content,"
            "font_family=excluded.font_family,font_size=excluded.font_size,color=excluded.color,"
            "alignment=excluded.alignment,style=excluded.style,updated_at=excluded.updated_at,"
            "rev=excluded.rev"));
        q.addBindValue(t.clipId);
        q.addBindValue(t.content);
        q.addBindValue(t.fontFamily);
        q.addBindValue(t.fontSize);
        q.addBindValue(t.color);
        q.addBindValue(t.alignment);
        q.addBindValue(t.style);
        q.addBindValue(t.updatedAt);
        q.addBindValue(t.rev);
        if (!q.exec()) {
            qWarning() << "[DB] text_element save 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("text_element"), t.clipId,
                   exists ? QStringLiteral("update") : QStringLiteral("insert"),
                   t.rev, toPayload(textJson(t)));
        return true;
    });
}

bool TextElementRepository::remove(const QString& clipId) {
    // text_element 无 deleted_at，物理删除并记 delete oplog。
    return Database::instance().transaction([&]() -> bool {
        QSqlQuery q(Database::instance().db());
        q.prepare(QStringLiteral("DELETE FROM text_element WHERE clip_id=?"));
        q.addBindValue(clipId);
        if (!q.exec()) {
            qWarning() << "[DB] text_element remove 失败:" << q.lastError().text();
            return false;
        }
        writeOplog(QStringLiteral("text_element"), clipId, QStringLiteral("delete"), 0, QString());
        return true;
    });
}

std::optional<TextElement> TextElementRepository::findByClip(const QString& clipId) {
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("SELECT * FROM text_element WHERE clip_id=?"));
    q.addBindValue(clipId);
    if (q.exec() && q.next()) {
        return readText(q);
    }
    return std::nullopt;
}

// ============ ExportJobRepository ============
namespace {
ExportJob readJob(const QSqlQuery& q) {
    ExportJob j;
    j.id = q.value("id").toString();
    j.projectId = q.value("project_id").toString();
    j.sequenceId = q.value("sequence_id").toString();
    j.status = q.value("status").toString();
    j.outputPath = q.value("output_path").toString();
    j.format = q.value("format").toString();
    j.videoCodec = q.value("video_codec").toString();
    j.audioCodec = q.value("audio_codec").toString();
    j.width = q.value("width").toInt();
    j.height = q.value("height").toInt();
    j.fps = q.value("fps").toDouble();
    j.bitrate = q.value("bitrate").toLongLong();
    j.progress = q.value("progress").toDouble();
    j.errorMessage = q.value("error_message").toString();
    j.createdAt = q.value("created_at").toLongLong();
    j.startedAt = q.value("started_at").toLongLong();
    j.finishedAt = q.value("finished_at").toLongLong();
    return j;
}
QVariant tsVar(qint64 ts) { return ts == 0 ? QVariant() : QVariant(ts); }
} // namespace

bool ExportJobRepository::insert(ExportJob& j) {
    if (j.id.isEmpty()) j.id = Database::newId();
    j.createdAt = Database::nowMs();

    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral(
        "INSERT INTO export_job(id,project_id,sequence_id,status,output_path,format,video_codec,"
        "audio_codec,width,height,fps,bitrate,progress,error_message,created_at,started_at,"
        "finished_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(j.id);
    q.addBindValue(j.projectId);
    q.addBindValue(j.sequenceId);
    q.addBindValue(j.status);
    q.addBindValue(j.outputPath);
    q.addBindValue(j.format);
    q.addBindValue(j.videoCodec);
    q.addBindValue(j.audioCodec);
    q.addBindValue(j.width);
    q.addBindValue(j.height);
    q.addBindValue(j.fps);
    q.addBindValue(static_cast<qlonglong>(j.bitrate));
    q.addBindValue(j.progress);
    q.addBindValue(j.errorMessage);
    q.addBindValue(j.createdAt);
    q.addBindValue(tsVar(j.startedAt));
    q.addBindValue(tsVar(j.finishedAt));
    if (!q.exec()) {
        qWarning() << "[DB] export_job insert 失败:" << q.lastError().text();
        return false;
    }
    return true;
}

bool ExportJobRepository::update(ExportJob& j) {
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral(
        "UPDATE export_job SET status=?,output_path=?,format=?,video_codec=?,audio_codec=?,"
        "width=?,height=?,fps=?,bitrate=?,progress=?,error_message=?,started_at=?,finished_at=? "
        "WHERE id=?"));
    q.addBindValue(j.status);
    q.addBindValue(j.outputPath);
    q.addBindValue(j.format);
    q.addBindValue(j.videoCodec);
    q.addBindValue(j.audioCodec);
    q.addBindValue(j.width);
    q.addBindValue(j.height);
    q.addBindValue(j.fps);
    q.addBindValue(static_cast<qlonglong>(j.bitrate));
    q.addBindValue(j.progress);
    q.addBindValue(j.errorMessage);
    q.addBindValue(tsVar(j.startedAt));
    q.addBindValue(tsVar(j.finishedAt));
    q.addBindValue(j.id);
    if (!q.exec()) {
        qWarning() << "[DB] export_job update 失败:" << q.lastError().text();
        return false;
    }
    return true;
}

std::optional<ExportJob> ExportJobRepository::findById(const QString& id) {
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral("SELECT * FROM export_job WHERE id=?"));
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        return readJob(q);
    }
    return std::nullopt;
}

std::vector<ExportJob> ExportJobRepository::listByProject(const QString& projectId) {
    std::vector<ExportJob> out;
    QSqlQuery q(Database::instance().db());
    q.prepare(QStringLiteral(
        "SELECT * FROM export_job WHERE project_id=? ORDER BY created_at DESC"));
    q.addBindValue(projectId);
    if (q.exec()) {
        while (q.next()) out.push_back(readJob(q));
    }
    return out;
}

} // namespace DB
} // namespace Mixed
