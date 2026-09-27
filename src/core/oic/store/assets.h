// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace oic::store {

// SPEC 6.1: generated images go under %LOCALAPPDATA%\image-client\assets\<job-id>,
// never the system temp directory, and directory creation plus quota accounting
// happen under one named mutex so the GUI and the MCP server can coexist.

// Cross-process gate. Abandoned (owner crashed) is treated as acquired, which is
// the point of a mutex over a lock file.
class QuotaLock {
public:
    explicit QuotaLock(QString name = QStringLiteral("image-client-asset-quota"));
    ~QuotaLock();

    QuotaLock(const QuotaLock &) = delete;
    QuotaLock &operator=(const QuotaLock &) = delete;

    bool acquire(int timeoutMs, QString *error);
    void release();
    bool isHeld() const { return m_held; }

private:
    QString m_name;
    void *m_handle = nullptr;  // HANDLE
    bool m_held = false;
};

// An ID becomes a path component, so it must not be able to name anything else.
bool isSafePathComponent(const QString &value);

// Pure ACL policy, split out so the rules are testable without touching Win32.
// SIDs arrive in string form (S-1-5-21-...); allowed holds the current user,
// LOCAL SYSTEM and BUILTIN Administrators.
//
// "ok" means at least one grant was readable and none of them widened access to
// another ordinary principal. Windows also stamps app-container capability SIDs
// (S-1-15-*) into subtrees of the profile -- measured on this machine's temp tree.
// Those are sandbox-scoped rather than a route for another user to read the file,
// so they land in notes instead of offending the check.
struct AclVerdict {
    bool ok = false;
    QStringList offenders;  // principals that widen access beyond this user
    QStringList notes;      // tolerated built-ins, kept for the log
    QStringList grants;     // everything seen
};

AclVerdict evaluateGrants(const QStringList &grantSids, const QStringList &allowedSids);

struct AclReport {
    bool ok = false;
    AclVerdict verdict;
    QStringList allowedSids;
    QString error;
};

// Reads the DACL of a directory. Fails closed: anything it cannot read is
// reported as not-ok rather than as a pass.
AclReport checkDirectoryAcl(const QString &path);

struct AssetWriteResult {
    QString relPath;      // <job-id>/<file>
    QString absolutePath;
    qint64 bytes = 0;
    QString error;

    bool ok() const { return error.isEmpty(); }
};

class AssetStore {
public:
    // lockTimeoutMs bounds how long a write waits for the quota lock before
    // failing; it is a knob only so contention is testable without a 5 s stall.
    AssetStore(QString assetsRoot, qint64 quotaBytes, int lockTimeoutMs = 5000);

    // Writes bytes under the quota lock. On refusal nothing is left on disk.
    AssetWriteResult write(const QString &jobId, const QString &assetId, const QString &mime,
                           const QByteArray &bytes) const;

    bool remove(const QString &relPath, QString *error) const;
    qint64 usedBytes(QString *error) const;

    static QString extensionFor(const QString &mime);

private:
    QString m_root;
    qint64 m_quotaBytes = 0;
    int m_lockTimeoutMs = 0;
};

}  // namespace oic::store
