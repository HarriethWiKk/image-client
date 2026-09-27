// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/store/database.h"

#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QVariant>

namespace oic::store {
namespace {

constexpr const char *kStatusColumn = "status";

qint64 nowSeconds()
{
    return QDateTime::currentSecsSinceEpoch();
}

const char *kSchemaV1[] = {
    "CREATE TABLE IF NOT EXISTS profiles ("
    " name TEXT PRIMARY KEY,"
    " base_url TEXT NOT NULL,"
    " protocol TEXT NOT NULL,"
    " image_model TEXT NOT NULL DEFAULT '',"
    " allowed_models TEXT NOT NULL DEFAULT '',"
    " timeout_s INTEGER NOT NULL DEFAULT 0,"
    " credential_target TEXT NOT NULL,"
    " created_at INTEGER NOT NULL)",

    "CREATE TABLE IF NOT EXISTS jobs ("
    " id TEXT PRIMARY KEY,"
    " created_at INTEGER NOT NULL,"
    " updated_at INTEGER NOT NULL,"
    " mode TEXT NOT NULL,"
    " protocol TEXT NOT NULL,"
    " profile TEXT NOT NULL,"
    " model TEXT NOT NULL DEFAULT '',"
    " prompt TEXT NOT NULL DEFAULT '',"
    " size TEXT NOT NULL DEFAULT '',"
    " n INTEGER NOT NULL DEFAULT 1,"
    " status TEXT NOT NULL,"
    " endpoint TEXT NOT NULL DEFAULT '',"
    " client_request_id TEXT NOT NULL DEFAULT '',"
    " error TEXT NOT NULL DEFAULT '',"
    " duration_ms INTEGER NOT NULL DEFAULT 0,"
    " request_json TEXT NOT NULL DEFAULT '',"
    " result_json TEXT NOT NULL DEFAULT '',"
    " pinned INTEGER NOT NULL DEFAULT 0,"
    " deleted_at INTEGER NOT NULL DEFAULT 0)",

    "CREATE TABLE IF NOT EXISTS assets ("
    " id TEXT PRIMARY KEY,"
    " job_id TEXT NOT NULL REFERENCES jobs(id) ON DELETE CASCADE,"
    " ordinal INTEGER NOT NULL,"
    " filename TEXT NOT NULL,"
    " rel_path TEXT NOT NULL,"
    " bytes INTEGER NOT NULL,"
    " mime TEXT NOT NULL DEFAULT '',"
    " width INTEGER NOT NULL DEFAULT 0,"
    " height INTEGER NOT NULL DEFAULT 0,"
    " sha256 TEXT NOT NULL DEFAULT '',"
    " created_at INTEGER NOT NULL)",

    "CREATE INDEX IF NOT EXISTS idx_jobs_created ON jobs(created_at DESC)",
    "CREATE INDEX IF NOT EXISTS idx_assets_job ON assets(job_id)",
};

Profile profileFromQuery(const QSqlQuery &query)
{
    Profile profile;
    profile.name = query.value(0).toString();
    profile.baseUrl = query.value(1).toString();
    profile.protocol = query.value(2).toString();
    profile.imageModel = query.value(3).toString();
    const QString allowed = query.value(4).toString();
    profile.allowedModels = allowed.isEmpty() ? QStringList() : allowed.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    profile.timeoutSeconds = query.value(5).toInt();
    profile.credentialTarget = query.value(6).toString();
    profile.createdAt = query.value(7).toLongLong();
    return profile;
}

Job jobFromQuery(const QSqlQuery &query)
{
    Job job;
    job.id = query.value(0).toString();
    job.createdAt = query.value(1).toLongLong();
    job.updatedAt = query.value(2).toLongLong();
    job.mode = query.value(3).toString();
    job.protocol = query.value(4).toString();
    job.profile = query.value(5).toString();
    job.model = query.value(6).toString();
    job.prompt = query.value(7).toString();
    job.size = query.value(8).toString();
    job.n = query.value(9).toInt();
    job.status = query.value(10).toString();
    job.endpoint = query.value(11).toString();
    job.clientRequestId = query.value(12).toString();
    job.error = query.value(13).toString();
    job.durationMs = query.value(14).toLongLong();
    job.requestJson = query.value(15).toString();
    job.resultJson = query.value(16).toString();
    job.pinned = query.value(17).toInt() != 0;
    job.deletedAt = query.value(18).toLongLong();
    return job;
}

Asset assetFromQuery(const QSqlQuery &query)
{
    Asset asset;
    asset.id = query.value(0).toString();
    asset.jobId = query.value(1).toString();
    asset.ordinal = query.value(2).toInt();
    asset.filename = query.value(3).toString();
    asset.relPath = query.value(4).toString();
    asset.bytes = query.value(5).toLongLong();
    asset.mime = query.value(6).toString();
    asset.width = query.value(7).toInt();
    asset.height = query.value(8).toInt();
    asset.sha256 = query.value(9).toString();
    asset.createdAt = query.value(10).toLongLong();
    return asset;
}

const char *kAssetColumns =
    " id, job_id, ordinal, filename, rel_path, bytes, mime, width, height, sha256, created_at";

// Statements are prepared here and executed by the caller, which is the only
// reason the split exists: exec-ing inside this helper would run the statement
// before any value has been bound to its placeholders.
bool run(QSqlQuery &query, const QString &sql, QString *error)
{
    if (query.prepare(sql)) {
        return true;
    }
    if (error != nullptr) {
        *error = QStringLiteral("SQL 准备失败：%1").arg(query.lastError().text());
    }
    return false;
}

// A default-constructed QString is *null* -- measured on this Qt: QString().isNull()
// is true while QStringLiteral("").isNull() is false -- and QSqlQuery turns a null
// into SQL NULL, which the NOT NULL text columns reject. "Caller left this out"
// means the empty string for these columns, so normalize at the bind boundary.
QVariant text(const QString &value)
{
    return QVariant(value.isNull() ? QStringLiteral("") : value);
}

bool exec(QSqlQuery &query, QString *error)
{
    if (query.exec()) {
        return true;
    }
    if (error != nullptr) {
        *error = QStringLiteral("SQL 执行失败：%1").arg(query.lastError().text());
    }
    return false;
}

// Switching a fresh database to WAL is a one-time, exclusive schema change, and
// the busy handler does NOT cover it: with several threads (JobManager's workers)
// opening the same brand-new file at once, the losers used to fail with
// "database is locked" instead of waiting -- observed 15/30 failures in
// tst_jobmanager. `PRAGMA journal_mode=WAL` is also idempotent: on a database that
// is already WAL it just reports back "wal" without taking a write lock, so the
// retry below costs nothing on the common (already-migrated) path. A lost race is
// expected contention, not a hard error, hence retry with a short backoff.
//
// Deliberately no separate "read the mode first" step: Qt's QSQLITE driver leaves
// the cursor open after next(), and a second QSqlQuery on the same connection then
// makes the switch fail with "cannot change into wal mode from within a
// transaction" (measured, 8/8 openers). Reusing one query, or writing directly, is
// what works.
bool ensureWalMode(QSqlDatabase &database, QString *error)
{
    constexpr int kMaxAttempts = 200;  // ~2 s at 10 ms; a first-open race settles in microseconds
    QString lastReason;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        QSqlQuery query(database);
        if (query.exec(QStringLiteral("PRAGMA journal_mode=WAL")) && query.next()
            && query.value(0).toString().compare(QStringLiteral("wal"), Qt::CaseInsensitive) == 0) {
            return true;
        }
        lastReason = query.lastError().text();
        QThread::msleep(10);
    }
    if (error != nullptr) {
        *error = QStringLiteral("设置 WAL 失败：%1").arg(lastReason);
    }
    return false;
}

}  // namespace

int Database::schemaVersion()
{
    return 1;
}

Database::Database(QString filePath)
    : m_filePath(std::move(filePath))
    , m_connectionName(QStringLiteral("oic-store-%1").arg(quintptr(QThread::currentThreadId())))
{
}

Database::~Database()
{
    close();
}

void Database::close()
{
    if (QSqlDatabase::contains(m_connectionName)) {
        {
            QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
            if (database.isOpen()) {
                database.close();
            }
        }
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool Database::open(QString *error)
{
    if (m_filePath.isEmpty()) {
        m_lastError = QStringLiteral("数据库路径为空");
        if (error != nullptr) {
            *error = m_lastError;
        }
        return false;
    }

    const QDir parent = QFileInfo(m_filePath).absoluteDir();
    if (!parent.exists() && !QDir().mkpath(parent.absolutePath())) {
        m_lastError = QStringLiteral("无法创建数据库目录 %1").arg(parent.absolutePath());
        if (error != nullptr) {
            *error = m_lastError;
        }
        return false;
    }

    if (!QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        database.setDatabaseName(m_filePath);
        database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    }

    QSqlDatabase database = QSqlDatabase::database(m_connectionName, /*open=*/false);
    if (!database.isOpen() && !database.open()) {
        m_lastError = QStringLiteral("打开数据库失败：%1").arg(database.lastError().text());
        if (error != nullptr) {
            *error = m_lastError;
        }
        return false;
    }

    {
        // Scoped so the PRAGMA cursor is closed before WAL/migration run: a live
        // QSqlQuery on this connection makes the later BEGIN IMMEDIATE contend badly
        // under concurrent first-opens (see applyMigrations).
        QSqlQuery pragma(database);
        // Set the busy timeout before any lock-taking statement, so contention below
        // waits instead of failing immediately. (connectOptions also sets it; this is
        // explicit and also covers the case where a driver ignored the option.)
        pragma.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
        pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    }

    // WAL: the GUI and the MCP server are separate processes over one file
    // (SPEC 6.1 allows them to coexist), and rollback-journal mode blocks the
    // second writer for the whole transaction. See ensureWalMode() for why this is
    // a read-then-switch with retry rather than a single PRAGMA.
    if (!ensureWalMode(database, error)) {
        m_lastError = error != nullptr ? *error : QStringLiteral("设置 WAL 失败");
        return false;
    }

    if (!applyMigrations(error)) {
        return false;
    }
    m_lastError.clear();
    return true;
}

bool Database::applyMigrations(QString *error)
{
    int current = 0;
    {
        // The cursor must not survive into the BEGIN below: Qt's QSQLITE driver keeps
        // the statement's read state alive as long as the QSqlQuery lives, and a
        // following BEGIN IMMEDIATE on the same connection then fails with "database
        // is locked" under contention (measured: 38/8 reps with the cursor alive,
        // 0 with it closed). Scope it and finish() before opening the transaction.
        QSqlQuery version(m_filePath.isEmpty() ? QSqlDatabase() : QSqlDatabase::database(m_connectionName, false));
        if (!version.exec(QStringLiteral("PRAGMA user_version")) || !version.next()) {
            m_lastError = QStringLiteral("读取 user_version 失败");
            if (error != nullptr) {
                *error = m_lastError;
            }
            return false;
        }
        current = version.value(0).toInt();
        version.finish();
    }
    if (current > schemaVersion()) {
        m_lastError = QStringLiteral("数据库由更新的版本写入（%1 > %2），拒绝降级打开")
                          .arg(current)
                          .arg(schemaVersion());
        if (error != nullptr) {
            *error = m_lastError;
        }
        return false;
    }

    if (current == schemaVersion()) {
        return true;
    }

    QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    // BEGIN IMMEDIATE, not QSqlDatabase::transaction() (which issues BEGIN DEFERRED).
    // A DEFERRED transaction takes a read lock first and only upgrades to a write
    // lock at the first write; if another connection holds the write lock by then,
    // SQLite returns SQLITE_BUSY *without* invoking the busy handler -- so the 5000
    // ms busy_timeout is bypassed. Concurrent first-opens were failing 19/8 reps
    // that way. IMMEDIATE takes the write lock up front, so contention waits out the
    // busy timeout instead of erroring. Measured: 0 failures with IMMEDIATE vs 19
    // with DEFERRED, same reproduction.
    QSqlQuery begin(database);
    if (!begin.exec(QStringLiteral("BEGIN IMMEDIATE"))) {
        m_lastError = begin.lastError().text();
        if (error != nullptr) {
            *error = QStringLiteral("开启迁移事务失败：%1").arg(m_lastError);
        }
        return false;
    }
    const auto rollback = [&database]() { QSqlQuery(database).exec(QStringLiteral("ROLLBACK")); };

    for (const char *statement : kSchemaV1) {
        QSqlQuery query(database);
        if (!run(query, QString::fromLatin1(statement), error) || !exec(query, error)) {
            rollback();
            m_lastError = *error;
            return false;
        }
    }

    QSqlQuery bump(database);
    // user_version does not accept a bound parameter, hence the guarded sprintf of
    // an integer we produced ourselves.
    if (!bump.exec(QStringLiteral("PRAGMA user_version=%1").arg(schemaVersion()))) {
        rollback();
        m_lastError = bump.lastError().text();
        if (error != nullptr) {
            *error = QStringLiteral("写入 user_version 失败：%1").arg(m_lastError);
        }
        return false;
    }
    QSqlQuery commit(database);
    if (!commit.exec(QStringLiteral("COMMIT"))) {
        rollback();
        m_lastError = commit.lastError().text();
        if (error != nullptr) {
            *error = QStringLiteral("提交迁移失败：%1").arg(m_lastError);
        }
        return false;
    }
    return true;
}

bool Database::writeProfile(const Profile &profile, QString *error)
{
    QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery query(database);
    // allowed_models is newline-joined: model ids never contain a newline, and a
    // JSON array here would make the column unreadable from a sqlite shell.
    if (!run(query,
             "INSERT INTO profiles (name, base_url, protocol, image_model, allowed_models, timeout_s,"
             " credential_target, created_at)"
             " VALUES (?, ?, ?, ?, ?, ?, ?, ?)"
             " ON CONFLICT(name) DO UPDATE SET base_url = excluded.base_url, protocol = excluded.protocol,"
             " image_model = excluded.image_model, allowed_models = excluded.allowed_models,"
             " timeout_s = excluded.timeout_s, credential_target = excluded.credential_target",
             error)) {
        return false;
    }
    query.addBindValue(text(profile.name));
    query.addBindValue(text(profile.baseUrl));
    query.addBindValue(text(profile.protocol));
    query.addBindValue(text(profile.imageModel));
    query.addBindValue(text(profile.allowedModels.join(QLatin1Char('\n'))));
    query.addBindValue(profile.timeoutSeconds);
    query.addBindValue(text(profile.credentialTarget));
    query.addBindValue(profile.createdAt > 0 ? profile.createdAt : nowSeconds());
    if (!exec(query, error)) {
        m_lastError = *error;
        return false;
    }
    return true;
}

QList<Profile> Database::profiles(QString *error) const
{
    QList<Profile> result;
    QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery query(database);
    if (!run(query,
             "SELECT name, base_url, protocol, image_model, allowed_models, timeout_s, credential_target,"
             " created_at FROM profiles ORDER BY name",
             error)) {
        return result;
    }
    if (!exec(query, error)) {
        return result;
    }
    while (query.next()) {
        result.append(profileFromQuery(query));
    }
    return result;
}

bool Database::removeProfile(const QString &name, QString *error)
{
    QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery query(database);
    if (!run(query, QStringLiteral("DELETE FROM profiles WHERE name = ?"), error)) {
        return false;
    }
    query.addBindValue(text(name));
    if (!query.exec()) {
        if (error != nullptr) {
            *error = query.lastError().text();
        }
        return false;
    }
    return true;
}

bool Database::createJob(const Job &job, QString *error)
{
    QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery query(database);
    if (!run(query,
             "INSERT INTO jobs (id, created_at, updated_at, mode, protocol, profile, model, prompt, size, n,"
             " status, endpoint, client_request_id, error, duration_ms, request_json, result_json, pinned,"
             " deleted_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
             error)) {
        return false;
    }
    const qint64 when = job.createdAt > 0 ? job.createdAt : nowSeconds();
    query.addBindValue(text(job.id));
    query.addBindValue(when);
    query.addBindValue(job.updatedAt > 0 ? job.updatedAt : when);
    query.addBindValue(text(job.mode));
    query.addBindValue(text(job.protocol));
    query.addBindValue(text(job.profile));
    query.addBindValue(text(job.model));
    query.addBindValue(text(job.prompt));
    query.addBindValue(text(job.size));
    query.addBindValue(job.n);
    query.addBindValue(text(job.status.isEmpty() ? QStringLiteral("queued") : job.status));
    query.addBindValue(text(job.endpoint));
    query.addBindValue(text(job.clientRequestId));
    query.addBindValue(text(job.error));
    query.addBindValue(job.durationMs);
    query.addBindValue(text(job.requestJson));
    query.addBindValue(text(job.resultJson));
    query.addBindValue(job.pinned ? 1 : 0);
    query.addBindValue(job.deletedAt);
    if (!exec(query, error)) {
        m_lastError = *error;
        return false;
    }
    return true;
}

bool Database::updateJob(const Job &job, QString *error)
{
    QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery query(database);
    if (!run(query,
             "UPDATE jobs SET updated_at = ?, status = ?, model = ?, prompt = ?, size = ?, n = ?, endpoint = ?,"
             " client_request_id = ?, error = ?, duration_ms = ?, request_json = ?, result_json = ?, pinned = ?,"
             " deleted_at = ? WHERE id = ?",
             error)) {
        return false;
    }
    query.addBindValue(job.updatedAt > 0 ? job.updatedAt : nowSeconds());
    query.addBindValue(job.status);
    query.addBindValue(text(job.model));
    query.addBindValue(text(job.prompt));
    query.addBindValue(text(job.size));
    query.addBindValue(job.n);
    query.addBindValue(text(job.endpoint));
    query.addBindValue(text(job.clientRequestId));
    query.addBindValue(text(job.error));
    query.addBindValue(job.durationMs);
    query.addBindValue(text(job.requestJson));
    query.addBindValue(text(job.resultJson));
    query.addBindValue(job.pinned ? 1 : 0);
    query.addBindValue(job.deletedAt);
    query.addBindValue(text(job.id));
    if (!exec(query, error)) {
        return false;
    }
    if (query.numRowsAffected() == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("任务 %1 不存在").arg(job.id);
        }
        return false;
    }
    return true;
}

Job Database::job(const QString &id, bool *found, QString *error) const
{
    if (found != nullptr) {
        *found = false;
    }
    QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery query(database);
    if (!run(query,
             QString("SELECT id, created_at, updated_at, mode, protocol, profile, model, prompt, size, n, %1,"
                     " endpoint, client_request_id, error, duration_ms, request_json, result_json, pinned, deleted_at"
                     " FROM jobs WHERE id = ?")
                 .arg(QLatin1String(kStatusColumn)),
             error)) {
        return Job();
    }
    query.addBindValue(text(id));
    if (!query.exec() || !query.next()) {
        return Job();
    }
    if (found != nullptr) {
        *found = true;
    }
    return jobFromQuery(query);
}

QList<Job> Database::listJobs(int limit, int offset, bool includeDeleted, QString *error) const
{
    QList<Job> result;
    QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery query(database);
    const QString filter = includeDeleted ? QString() : QStringLiteral(" AND deleted_at = 0");
    if (!run(query,
             QString("SELECT id, created_at, updated_at, mode, protocol, profile, model, prompt, size, n, %1,"
                     " endpoint, client_request_id, error, duration_ms, request_json, result_json, pinned, deleted_at"
                     " FROM jobs WHERE 1 = 1%2 ORDER BY created_at DESC, id LIMIT ? OFFSET ?")
                 .arg(QLatin1String(kStatusColumn), filter),
             error)) {
        return result;
    }
    query.addBindValue(limit);
    query.addBindValue(offset);
    if (!query.exec()) {
        if (error != nullptr) {
            *error = query.lastError().text();
        }
        return result;
    }
    while (query.next()) {
        result.append(jobFromQuery(query));
    }
    return result;
}

bool Database::setPinned(const QString &id, bool pinned, QString *error)
{
    QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery query(database);
    if (!run(query, QStringLiteral("UPDATE jobs SET pinned = ? WHERE id = ?"), error)) {
        return false;
    }
    query.addBindValue(pinned ? 1 : 0);
    query.addBindValue(text(id));
    if (!exec(query, error)) {
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool Database::softDeleteJob(const QString &id, qint64 when, QString *error)
{
    QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery query(database);
    if (!run(query, QStringLiteral("UPDATE jobs SET deleted_at = ?, updated_at = ? WHERE id = ?"), error)) {
        return false;
    }
    query.addBindValue(when > 0 ? when : nowSeconds());
    query.addBindValue(when > 0 ? when : nowSeconds());
    query.addBindValue(text(id));
    if (!exec(query, error)) {
        return false;
    }
    if (query.numRowsAffected() == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("任务 %1 不存在").arg(id);
        }
        return false;
    }
    return true;
}

bool Database::addAsset(const Asset &asset, QString *error)
{
    QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery query(database);
    if (!run(query,
             "INSERT INTO assets (id, job_id, ordinal, filename, rel_path, bytes, mime, width, height, sha256,"
             " created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
             error)) {
        return false;
    }
    query.addBindValue(text(asset.id));
    query.addBindValue(text(asset.jobId));
    query.addBindValue(asset.ordinal);
    query.addBindValue(text(asset.filename));
    query.addBindValue(text(asset.relPath));
    query.addBindValue(asset.bytes);
    query.addBindValue(text(asset.mime));
    query.addBindValue(asset.width);
    query.addBindValue(asset.height);
    query.addBindValue(text(asset.sha256));
    query.addBindValue(asset.createdAt > 0 ? asset.createdAt : nowSeconds());
    if (!exec(query, error)) {
        m_lastError = *error;
        return false;
    }
    return true;
}

QList<Asset> Database::assetsForJob(const QString &jobId, QString *error) const
{
    QList<Asset> result;
    QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery query(database);
    if (!run(query,
             QString("SELECT %1 FROM assets WHERE job_id = ? ORDER BY ordinal").arg(QLatin1String(kAssetColumns)),
             error)) {
        return result;
    }
    query.addBindValue(text(jobId));
    if (!query.exec()) {
        if (error != nullptr) {
            *error = query.lastError().text();
        }
        return result;
    }
    while (query.next()) {
        result.append(assetFromQuery(query));
    }
    return result;
}

QList<Asset> Database::allAssets(QString *error) const
{
    QList<Asset> result;
    QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery query(database);
    if (!run(query, QString("SELECT %1 FROM assets ORDER BY created_at, id").arg(QLatin1String(kAssetColumns)), error)) {
        return result;
    }
    if (!query.exec()) {
        if (error != nullptr) {
            *error = query.lastError().text();
        }
        return result;
    }
    while (query.next()) {
        result.append(assetFromQuery(query));
    }
    return result;
}

PruneReport Database::pruneToLimits(int maxRows, qint64 maxAssetBytes, QString *error)
{
    PruneReport report;
    QSqlDatabase database = QSqlDatabase::database(m_connectionName, false);

    // 1. Anything the user already deleted is purged, whatever the row budget is.
    {
        QSqlQuery query(database);
        if (!run(query, QStringLiteral("SELECT id FROM jobs WHERE deleted_at <> 0"), error)) {
            return report;
        }
        if (!query.exec()) {
            if (error != nullptr) {
                *error = query.lastError().text();
            }
            return report;
        }
        while (query.next()) {
            report.removedJobs.append(query.value(0).toString());
        }
    }

    // 2. Oldest unpinned live rows beyond the budget.
    {
        QSqlQuery query(database);
        if (!run(query,
                 "SELECT id FROM jobs WHERE pinned = 0 AND deleted_at = 0"
                 " ORDER BY created_at ASC, id ASC",
                 error)) {
            return report;
        }
        QList<QString> live;
        if (query.exec()) {
            while (query.next()) {
                live.append(query.value(0).toString());
            }
        }
        // live is oldest-first, so the rows past the budget are the leading ones.
        for (qsizetype i = 0; i < live.size() - maxRows; ++i) {
            report.removedJobs.append(live.at(i));
        }
    }

    if (!report.removedJobs.isEmpty()) {
        database.transaction();
        for (const QString &jobId : report.removedJobs) {
            QSqlQuery assets(database);
            if (!run(assets, QString("SELECT %1 FROM assets WHERE job_id = ?").arg(QLatin1String(kAssetColumns)), error)) {
                database.rollback();
                return report;
            }
            assets.addBindValue(text(jobId));
            if (assets.exec()) {
                while (assets.next()) {
                    const Asset asset = assetFromQuery(assets);
                    report.freedBytes += asset.bytes;
                    report.removedAssets.append(asset);
                }
            }
            QSqlQuery remove(database);
            if (!run(remove, QStringLiteral("DELETE FROM jobs WHERE id = ?"), error)) {
                database.rollback();
                return report;
            }
            remove.addBindValue(text(jobId));
            remove.exec();
        }
        database.commit();
    }

    // 3. Byte budget: drop assets of the oldest non-pinned jobs until under it.
    if (maxAssetBytes > 0) {
        qint64 total = 0;
        QList<Asset> remaining;
        QSqlQuery query(database);
        if (run(query, QString("SELECT %1 FROM assets ORDER BY created_at ASC, id ASC").arg(QLatin1String(kAssetColumns)),
                error)) {
            if (query.exec()) {
                while (query.next()) {
                    remaining.append(assetFromQuery(query));
                }
            }
        }
        for (const Asset &asset : remaining) {
            total += asset.bytes;
        }
        for (const Asset &asset : remaining) {
            if (total <= maxAssetBytes) {
                break;
            }
            QSqlQuery pinnedCheck(database);
            if (!run(pinnedCheck, QStringLiteral("SELECT pinned FROM jobs WHERE id = ?"), error)) {
                break;
            }
            pinnedCheck.addBindValue(text(asset.jobId));
            bool pinned = false;
            if (pinnedCheck.exec() && pinnedCheck.next()) {
                pinned = pinnedCheck.value(0).toInt() != 0;
            }
            if (pinned) {
                continue;
            }
            QSqlQuery remove(database);
            if (!run(remove, QStringLiteral("DELETE FROM assets WHERE id = ?"), error)) {
                break;
            }
            remove.addBindValue(text(asset.id));
            if (!remove.exec()) {
                // Do not count a row that was not actually deleted: freedBytes and
                // removedAssets must describe the database, not the intent.
                if (error != nullptr) {
                    *error = remove.lastError().text();
                }
                break;
            }
            total -= asset.bytes;
            report.freedBytes += asset.bytes;
            report.removedAssets.append(asset);
        }
    }

    return report;
}

}  // namespace oic::store
