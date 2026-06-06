//
// Created by Anlk on 2026/6/6.
// Database 实现。
//

#include "../include/Database.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>
#include <QDebug>

namespace Mixed {
namespace DB {

Database& Database::instance() {
    static Database s_instance;
    return s_instance;
}

bool Database::open(const QString& dbPath) {
    if (m_open) {
        return true;
    }

    QString path = dbPath;
    if (path.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        path = dir + QStringLiteral("/mixedediting.db");
    }

    if (QSqlDatabase::contains(kConnectionName)) {
        m_db = QSqlDatabase::database(kConnectionName);
    } else {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConnectionName);
    }
    m_db.setDatabaseName(path);

    if (!m_db.open()) {
        qWarning() << "[DB] 打开失败:" << m_db.lastError().text() << "path=" << path;
        return false;
    }

    // WAL：读写并发，剪辑预览与写入互不阻塞；外键：保证引用完整性。
    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));

    if (!migrate()) {
        qWarning() << "[DB] 迁移失败";
        m_db.close();
        return false;
    }

    m_open = true;
    qInfo() << "[DB] 已打开:" << path << "schema v" << schemaVersion();
    return true;
}

bool Database::isOpen() const {
    return m_open && m_db.isOpen();
}

QSqlDatabase Database::db() const {
    return m_db;
}

bool Database::transaction(const std::function<bool()>& fn) {
    if (!m_db.transaction()) {
        qWarning() << "[DB] 无法开启事务:" << m_db.lastError().text();
        return false;
    }
    bool commit = false;
    try {
        commit = fn();
    } catch (const std::exception& e) {
        qWarning() << "[DB] 事务异常:" << e.what();
        commit = false;
    } catch (...) {
        qWarning() << "[DB] 事务未知异常";
        commit = false;
    }

    if (commit) {
        if (!m_db.commit()) {
            qWarning() << "[DB] 提交失败:" << m_db.lastError().text();
            m_db.rollback();
            return false;
        }
        return true;
    }
    m_db.rollback();
    return false;
}

QString Database::newId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

qint64 Database::nowMs() {
    return QDateTime::currentMSecsSinceEpoch();
}

int Database::schemaVersion() const {
    return const_cast<Database*>(this)->readVersion();
}

int Database::readVersion() {
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT name FROM sqlite_master WHERE type='table' AND name='schema_version'"))) {
        return 0;
    }
    if (!q.next()) {
        return 0; // 表不存在
    }
    QSqlQuery v(m_db);
    if (v.exec(QStringLiteral("SELECT COALESCE(MAX(version),0) FROM schema_version")) && v.next()) {
        return v.value(0).toInt();
    }
    return 0;
}

bool Database::migrate() {
    QSqlQuery q(m_db);
    // 版本表独立于 schema.sql 管理，避免迁移逻辑自我依赖。
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS schema_version ("
            "version INTEGER PRIMARY KEY, applied_at INTEGER NOT NULL)"))) {
        qWarning() << "[DB] 建 schema_version 失败:" << q.lastError().text();
        return false;
    }

    const int current = readVersion();
    if (current >= kTargetVersion) {
        return true; // 已是最新
    }

    // v1：加载内嵌 schema.sql 建全部业务表。
    QFile f(QStringLiteral(":/db/schema.sql"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[DB] 无法读取内嵌 schema.sql";
        return false;
    }
    const QString sql = QString::fromUtf8(f.readAll());
    f.close();

    return transaction([&]() -> bool {
        // 按分号切分逐条执行（schema.sql 中无分号字面量，安全）。
        const QStringList statements = sql.split(QChar(';'), Qt::SkipEmptyParts);
        for (const QString& raw : statements) {
            const QString stmt = raw.trimmed();
            if (stmt.isEmpty() || stmt.startsWith(QStringLiteral("--"))) {
                continue;
            }
            QSqlQuery exec(m_db);
            if (!exec.exec(stmt)) {
                qWarning() << "[DB] 执行 DDL 失败:" << exec.lastError().text()
                           << "\n  SQL:" << stmt.left(80);
                return false;
            }
        }
        QSqlQuery ver(m_db);
        ver.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO schema_version(version, applied_at) VALUES(?, ?)"));
        ver.addBindValue(kTargetVersion);
        ver.addBindValue(nowMs());
        if (!ver.exec()) {
            qWarning() << "[DB] 写版本号失败:" << ver.lastError().text();
            return false;
        }
        return true;
    });
}

} // namespace DB
} // namespace Mixed
