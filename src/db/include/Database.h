//
// Created by Anlk on 2026/6/6.
// 数据库连接与迁移管理：打开 SQLite（WAL + 外键），按版本执行 schema 迁移，
// 提供事务封装与 UUID / 时间戳工具。单例，整个进程共享一条连接。
//

#ifndef MIXEDEDITING_DATABASE_H
#define MIXEDEDITING_DATABASE_H

#include <QSqlDatabase>
#include <QString>
#include <functional>

namespace Mixed {
namespace DB {

class Database {
public:
    // 进程内单例。
    static Database& instance();

    // 打开数据库并执行迁移。dbPath 为空时使用默认应用数据目录下的 mixedediting.db。
    // 可重复调用（幂等）；返回 false 表示打开或迁移失败。
    bool open(const QString& dbPath = QString());

    // 是否已成功打开。
    bool isOpen() const;

    // 底层连接（供 Repository 构造 QSqlQuery）。
    QSqlDatabase db() const;

    // 在事务中执行 fn：fn 返回 true 则提交，false 或抛异常则回滚。
    // 返回最终是否提交成功。
    bool transaction(const std::function<bool()>& fn);

    // ---- 工具 ----
    // 生成去除花括号的 UUID 字符串，作为各表主键。
    static QString newId();
    // 当前 unix 毫秒时间戳。
    static qint64 nowMs();

    // 当前 schema 版本（迁移后）。
    int schemaVersion() const;

private:
    Database() = default;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // 应用所有未应用的迁移，维护 schema_version 表。
    bool migrate();
    // 读取当前 schema_version；表不存在视为 0。
    int readVersion();

    QSqlDatabase m_db;
    bool m_open = false;

    static constexpr int kTargetVersion = 1;
    static constexpr const char* kConnectionName = "mixed_clip_db";
};

} // namespace DB
} // namespace Mixed

#endif // MIXEDEDITING_DATABASE_H
