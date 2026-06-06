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

} // namespace DB
} // namespace Mixed
