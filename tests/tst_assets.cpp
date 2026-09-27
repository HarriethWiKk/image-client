// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <string>

#include <windows.h>

#include <aclapi.h>
#include <QTest>
#include <QThread>
#include <QDeadlineTimer>

#include "oic/store/assets.h"

namespace {

// Stand-in trustee list for the pure policy cases: a user, LOCAL SYSTEM and
// BUILTIN Administrators in SID form. The real SIDs are resolved by
// checkDirectoryAcl, which the freshDirectoryAclIsRestricted case exercises.
QStringList kTrustedSids()
{
    return QStringList{ QStringLiteral("S-1-5-21-1-2-3-1001"), QStringLiteral("S-1-5-18"),
                        QStringLiteral("S-1-5-32-544") };
}

// Holds the quota lock for a while from another thread. A Windows mutex is
// recursive for its owning thread, so contention only exists across threads --
// which is exactly the GUI-plus-MCP case SPEC 6.1 is about.
class LockHolder : public QThread {
public:
    explicit LockHolder(int holdMs) : m_holdMs(holdMs) {}

    bool acquired() const { return m_acquired; }

protected:
    void run() override
    {
        oic::store::QuotaLock lock;
        QString error;
        m_acquired = lock.acquire(2000, &error);
        if (m_acquired) {
            QThread::msleep(m_holdMs);
        }
    }

private:
    int m_holdMs = 0;
    bool m_acquired = false;
};

}  // namespace

class TstAssets : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_root;

    oic::store::AssetStore makeStore(qint64 quota, int lockTimeoutMs = 5000) const
    {
        return oic::store::AssetStore(m_root, quota, lockTimeoutMs);
    }

    static QStringList filesUnder(const QString &root)
    {
        QStringList out;
        QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            out.append(QDir(root).relativeFilePath(it.next()));
        }
        out.sort();
        return out;
    }

private slots:
    void init()
    {
        QVERIFY(m_dir.isValid());
        QDir(m_dir.path()).removeRecursively();
        QVERIFY(QDir(m_dir.path()).mkpath(QStringLiteral("assets")));
        m_root = m_dir.filePath(QStringLiteral("assets"));
    }

    void pathComponentRules_data();
    void pathComponentRules();

    void extensionMapping_data();
    void extensionMapping();

    void writesLandUnderJobDirectory();
    void singleImageBeyondQuotaIsRefused();
    void quotaStopsTheSecondWriteAndKeepsTheFirst();
    void contentionWithAnotherThreadFailsFast();
    void removeRejectsEscapingPaths_data();
    void removeRejectsEscapingPaths();
    void removeDeletesInsideTheTree();

    void aclPolicy_data();
    void aclPolicy();
    void freshDirectoryAclIsRestricted();
    void looseInheritedAclIsRepairedNotRefused();
};

// A directory whose DACL grants Everyone is the case that actually happened on a
// live run: the parent tree leaked Authenticated Users / BUILTIN\Users, and a
// check-only design refused to save the image. Windows-only by nature.
static bool makeWorldReadable(const QString &path, QString *error)
{
    SID_IDENTIFIER_AUTHORITY everyoneAuthority = SECURITY_WORLD_SID_AUTHORITY;
    PSID everyone = nullptr;
    if (!AllocateAndInitializeSid(&everyoneAuthority, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &everyone)) {
        *error = QStringLiteral("AllocateAndInitializeSid failed for Everyone SID");
        return false;
    }
    EXPLICIT_ACCESSW entry = {};
    entry.grfAccessPermissions = FILE_GENERIC_READ;
    entry.grfAccessMode = GRANT_ACCESS;
    entry.grfInheritance = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
    BuildTrusteeWithSidW(&entry.Trustee, everyone);

    PACL acl = nullptr;
    bool ok = SetEntriesInAclW(1, &entry, nullptr, &acl) == ERROR_SUCCESS && acl != nullptr;
    std::wstring target = reinterpret_cast<const wchar_t *>(path.utf16());
    if (ok) {
        ok = SetNamedSecurityInfoW(const_cast<LPWSTR>(target.data()), SE_FILE_OBJECT,
                                   DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                   acl, nullptr)
             == ERROR_SUCCESS;
    }
    if (acl != nullptr) {
        LocalFree(acl);
    }
    FreeSid(everyone);
    if (!ok && error != nullptr) {
        *error = QStringLiteral("could not loosen the ACL of %1").arg(path);
    }
    return ok;
}

void TstAssets::looseInheritedAclIsRepairedNotRefused()
{
    // The store creates job directories under its own root, so that is the
    // directory that has to be loosened for the repair path to be exercised.
    const QString jobDir = QDir(m_root).filePath(QStringLiteral("loose-job"));
    QVERIFY(QDir().mkpath(jobDir));
    QString error;
    if (!makeWorldReadable(jobDir, &error)) {
        QSKIP(qPrintable(error));  // some CI images deny ACL writes; that is not a failure
    }

    // Precondition, so the test cannot pass by accident.
    QVERIFY2(!oic::store::checkDirectoryAcl(jobDir).ok, "the directory was not loosened as intended");

    oic::store::AssetStore store = makeStore(1024 * 1024);
    const oic::store::AssetWriteResult written =
        store.write(QStringLiteral("loose-job"), QStringLiteral("asset-1"), QStringLiteral("image/png"),
                    QByteArray(16, 'q'));
    QVERIFY2(written.ok(), qPrintable(written.error));
    QVERIFY(QFile::exists(written.absolutePath));
    QVERIFY2(oic::store::checkDirectoryAcl(jobDir).ok, "write() left a world-readable directory in place");
}

void TstAssets::pathComponentRules_data()
{
    QTest::addColumn<QString>("value");
    QTest::addColumn<bool>("safe");

    QTest::newRow("job id") << QStringLiteral("job-2026-09-27-abc") << true;
    QTest::newRow("uuid-ish") << QStringLiteral("5f0a1b2c3d4e") << true;
    QTest::newRow("dots inside") << QStringLiteral("a.b_c-d") << true;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("dot") << QStringLiteral(".") << false;
    QTest::newRow("dotdot") << QStringLiteral("..") << false;
    QTest::newRow("leading dash") << QStringLiteral("-abc") << false;
    QTest::newRow("forward slash") << QStringLiteral("a/b") << false;
    QTest::newRow("backslash") << QStringLiteral("a\\b") << false;
    QTest::newRow("traversal") << QStringLiteral("../evil") << false;
    QTest::newRow("absolute") << QStringLiteral("C:/Windows") << false;
    QTest::newRow("space") << QStringLiteral("a b") << false;
    QTest::newRow("unicode") << QStringLiteral("图") << false;
    QTest::newRow("too long") << QString(81, QLatin1Char('a')) << false;
}

void TstAssets::pathComponentRules()
{
    QFETCH(const QString, value);
    QFETCH(const bool, safe);
    QCOMPARE(oic::store::isSafePathComponent(value), safe);
}

void TstAssets::extensionMapping_data()
{
    QTest::addColumn<QString>("mime");
    QTest::addColumn<QString>("extension");

    QTest::newRow("png") << QStringLiteral("image/png") << QStringLiteral("png");
    QTest::newRow("jpeg") << QStringLiteral("image/jpeg") << QStringLiteral("jpg");
    QTest::newRow("jpeg case and params") << QStringLiteral("IMAGE/JPEG; charset=x") << QStringLiteral("jpg");
    QTest::newRow("webp") << QStringLiteral("image/webp") << QStringLiteral("webp");
    QTest::newRow("gif") << QStringLiteral("image/gif") << QStringLiteral("gif");
    QTest::newRow("unknown") << QStringLiteral("application/pdf") << QStringLiteral("bin");
    QTest::newRow("empty") << QString() << QStringLiteral("bin");
}

void TstAssets::extensionMapping()
{
    QFETCH(const QString, mime);
    QFETCH(const QString, extension);
    QCOMPARE(oic::store::AssetStore::extensionFor(mime), extension);
}

void TstAssets::writesLandUnderJobDirectory()
{
    oic::store::AssetStore store = makeStore(10 * 1024 * 1024);
    QString error;

    const QByteArray png(2048, 'p');
    const oic::store::AssetWriteResult first =
        store.write(QStringLiteral("job-a"), QStringLiteral("asset-1"), QStringLiteral("image/png"), png);
    QVERIFY2(first.ok(), qPrintable(first.error));
    QCOMPARE(first.relPath, QStringLiteral("job-a/asset-1.png"));
    QCOMPARE(first.bytes, qint64(png.size()));
    QVERIFY(QFileInfo::exists(first.absolutePath));
    QFile written(first.absolutePath);
    QVERIFY(written.open(QIODevice::ReadOnly));
    QCOMPARE(written.readAll(), png);

    const oic::store::AssetWriteResult second =
        store.write(QStringLiteral("job-b"), QStringLiteral("asset-2"), QStringLiteral("image/jpeg"), QByteArray(1000, 'j'));
    QVERIFY2(second.ok(), qPrintable(second.error));
    QCOMPARE(second.relPath, QStringLiteral("job-b/asset-2.jpg"));

    QString sizeError;
    QCOMPARE(store.usedBytes(&sizeError), qint64(3048));
    QCOMPARE(filesUnder(m_root), QStringList({ QStringLiteral("job-a/asset-1.png"), QStringLiteral("job-b/asset-2.jpg") }));
}

void TstAssets::singleImageBeyondQuotaIsRefused()
{
    oic::store::AssetStore store = makeStore(1000);
    const oic::store::AssetWriteResult result =
        store.write(QStringLiteral("job-a"), QStringLiteral("asset-1"), QStringLiteral("image/png"), QByteArray(2000, 'x'));
    QVERIFY(!result.ok());
    QVERIFY2(result.error.contains(QString::fromUtf8("配额")), qPrintable(result.error));
    QVERIFY(filesUnder(m_root).isEmpty());
    QVERIFY(!QDir(m_root).exists(QStringLiteral("job-a")));  // no empty job directory either
}

void TstAssets::quotaStopsTheSecondWriteAndKeepsTheFirst()
{
    oic::store::AssetStore store = makeStore(3000);
    QVERIFY(store.write(QStringLiteral("job-a"), QStringLiteral("one"), QStringLiteral("image/png"), QByteArray(2000, 'a')).ok());

    const oic::store::AssetWriteResult second =
        store.write(QStringLiteral("job-b"), QStringLiteral("two"), QStringLiteral("image/png"), QByteArray(2000, 'b'));
    QVERIFY(!second.ok());
    QVERIFY2(second.error.contains(QString::fromUtf8("配额")), qPrintable(second.error));
    QCOMPARE(filesUnder(m_root), QStringList({ QStringLiteral("job-a/one.png") }));
    QVERIFY(!QDir(m_root).exists(QStringLiteral("job-b")));
}

void TstAssets::contentionWithAnotherThreadFailsFast()
{
    LockHolder holder(1200);
    holder.start();
    QTRY_VERIFY_WITH_TIMEOUT(holder.acquired(), 2000);

    // 150 ms budget against a lock held for 1.2 s: the write must give up rather
    // than queue silently behind the other process.
    oic::store::AssetStore store = makeStore(10 * 1024 * 1024, /*lockTimeoutMs=*/150);
    const oic::store::AssetWriteResult result =
        store.write(QStringLiteral("job-a"), QStringLiteral("asset-1"), QStringLiteral("image/png"), QByteArray(10, 'x'));
    QVERIFY(!result.ok());
    QVERIFY2(result.error.contains(QString::fromUtf8("超时")), qPrintable(result.error));
    QVERIFY(filesUnder(m_root).isEmpty());

    holder.wait(5000);
}

void TstAssets::removeRejectsEscapingPaths_data()
{
    QTest::addColumn<QString>("relPath");

    QTest::newRow("parent") << QStringLiteral("../outside.png");
    QTest::newRow("nested escape") << QStringLiteral("job-a/../../outside.png");
    QTest::newRow("absolute posix") << QStringLiteral("/etc/passwd");
    QTest::newRow("absolute win") << QStringLiteral("C:/Windows/win.ini");
    QTest::newRow("bare dotdot") << QStringLiteral("..");
}

void TstAssets::removeRejectsEscapingPaths()
{
    QFETCH(const QString, relPath);
    QFile victim(m_dir.filePath(QStringLiteral("outside.png")));
    QVERIFY(victim.open(QIODevice::WriteOnly));
    victim.write("do not delete me");
    victim.close();

    oic::store::AssetStore store = makeStore(1024 * 1024);
    QString error;
    QVERIFY2(!store.remove(relPath, &error), qPrintable(QStringLiteral("accepted %1").arg(relPath)));
    QVERIFY(!error.isEmpty());
    QVERIFY(QFile::exists(m_dir.filePath(QStringLiteral("outside.png"))));
}

void TstAssets::removeDeletesInsideTheTree()
{
    oic::store::AssetStore store = makeStore(1024 * 1024);
    const oic::store::AssetWriteResult written =
        store.write(QStringLiteral("job-a"), QStringLiteral("asset-1"), QStringLiteral("image/png"), QByteArray(10, 'x'));
    QVERIFY(written.ok());

    QString error;
    QVERIFY2(store.remove(written.relPath, &error), qPrintable(error));
    QVERIFY(!QFile::exists(written.absolutePath));

    error.clear();
    QVERIFY(!store.remove(written.relPath, &error));  // already gone
    QVERIFY(!error.isEmpty());
}

void TstAssets::aclPolicy_data()
{
    QTest::addColumn<QStringList>("grants");
    QTest::addColumn<bool>("ok");
    QTest::addColumn<bool>("expectOffenders");

    auto withTrusted = [](const QString &extra) {
        QStringList list = kTrustedSids();
        if (!extra.isEmpty()) {
            list.append(extra);
        }
        return list;
    };

    QTest::newRow("owners only") << withTrusted(QString()) << true << false;
    QTest::newRow("capability sid tolerated") << withTrusted(QStringLiteral("S-1-15-3-600613-1-3-843889043")) << true
                                              << false;
    QTest::newRow("everyone added") << withTrusted(QStringLiteral("S-1-1-0")) << false << true;
    QTest::newRow("builtin users added") << withTrusted(QStringLiteral("S-1-5-32-545")) << false << true;
    QTest::newRow("network added") << withTrusted(QStringLiteral("S-1-5-2")) << false << true;
    QTest::newRow("anonymous added") << withTrusted(QStringLiteral("S-1-5-7")) << false << true;
    // Fail closed for a different reason: no readable grant proves nothing about
    // the directory, so it is not-ok even though nothing widened access.
    QTest::newRow("no grants") << QStringList{} << false << false;
}

void TstAssets::aclPolicy()
{
    QFETCH(const QStringList, grants);
    QFETCH(const bool, ok);
    QFETCH(const bool, expectOffenders);
    const oic::store::AclVerdict verdict = oic::store::evaluateGrants(grants, kTrustedSids());
    QCOMPARE(verdict.ok, ok);
    QCOMPARE(!verdict.offenders.isEmpty(), expectOffenders);
}

void TstAssets::freshDirectoryAclIsRestricted()
{
    const QString path = m_dir.filePath(QStringLiteral("acl-probe"));
    QVERIFY(QDir().mkpath(path));

    const oic::store::AclReport report = oic::store::checkDirectoryAcl(path);
    QVERIFY2(!report.allowedSids.isEmpty(), qPrintable(report.error));
    QVERIFY2(report.error.isEmpty(), qPrintable(report.error));
    QVERIFY2(report.verdict.grants.size() >= 2,
             qPrintable(QStringLiteral("expected the owner and SYSTEM grants to be visible, saw %1")
                            .arg(report.verdict.grants.join(QStringLiteral(", ")) )));
    // The whole point of SPEC 6.1: a directory created by us inherits a DACL that
    // names only this user, SYSTEM and Administrators.
    QVERIFY2(report.ok, qPrintable(QStringLiteral("offenders: %1").arg(report.verdict.offenders.join(QStringLiteral(", ")))));
}

QTEST_GUILESS_MAIN(TstAssets)
#include "tst_assets.moc"
