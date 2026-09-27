// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QSqlDatabase>

namespace oic::store {

// The persisted history. SPEC 6.2: this is the rebuild's main functional addition
// over the old app, which kept jobs in memory and lost them on restart.
//
// Keys are never stored here -- profiles only carry the Credential Manager target
// name (SPEC 6.3).

struct Profile {
    QString name;
    QString baseUrl;
    QString protocol;   // "openai" | "grok" | "gemini" (SPEC 5.1)
    QString imageModel;
    QStringList allowedModels;
    int timeoutSeconds = 0;
    QString credentialTarget;
    qint64 createdAt = 0;
};

struct Job {
    QString id;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    QString mode;       // "generate" | "edit"
    QString protocol;
    QString profile;
    QString model;
    QString prompt;
    QString size;
    int n = 1;
    QString status;     // queued | running | succeeded | failed | cancelled
    QString endpoint;
    QString clientRequestId;
    QString error;
    qint64 durationMs = 0;
    QString requestJson;
    QString resultJson;
    bool pinned = false;
    qint64 deletedAt = 0;  // 0 = not soft-deleted
};

struct Asset {
    QString id;
    QString jobId;
    int ordinal = 0;
    QString filename;
    QString relPath;      // relative to Paths::assets, so the tree can move
    qint64 bytes = 0;
    QString mime;
    int width = 0;
    int height = 0;
    QString sha256;
    qint64 createdAt = 0;
};

struct PruneReport {
    QList<Asset> removedAssets;  // caller deletes these files
    QList<QString> removedJobs;
    qint64 freedBytes = 0;
};

class Database {
public:
    explicit Database(QString filePath);
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    // Opens (creating the schema when the file is new) and migrates to the latest
    // version. Fails rather than guessing when a newer app wrote the file.
    bool open(QString *error);
    void close();

    // Latest schema version this build understands.
    static int schemaVersion();

    QString lastError() const { return m_lastError; }

    bool writeProfile(const Profile &profile, QString *error);
    QList<Profile> profiles(QString *error) const;
    bool removeProfile(const QString &name, QString *error);

    bool createJob(const Job &job, QString *error);
    bool updateJob(const Job &job, QString *error);
    // Returns a default-constructed Job with id cleared when nothing matches;
    // found tells the two apart.
    Job job(const QString &id, bool *found, QString *error) const;
    // newest first; includes soft-deleted rows only when includeDeleted.
    QList<Job> listJobs(int limit, int offset, bool includeDeleted, QString *error) const;
    bool setPinned(const QString &id, bool pinned, QString *error);
    bool softDeleteJob(const QString &id, qint64 when, QString *error);

    bool addAsset(const Asset &asset, QString *error);
    QList<Asset> assetsForJob(const QString &jobId, QString *error) const;
    QList<Asset> allAssets(QString *error) const;

    // Drops the oldest non-pinned, non-deleted rows beyond maxRows, then the
    // oldest assets beyond maxAssetBytes. Pinned jobs survive any limit.
    PruneReport pruneToLimits(int maxRows, qint64 maxAssetBytes, QString *error);

private:
    bool applyMigrations(QString *error);

    QString m_filePath;
    QString m_connectionName;
    QString m_lastError;
};

}  // namespace oic::store
