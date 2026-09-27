// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QSemaphore>
#include <QStringList>
#include <memory>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include "oic/store/database.h"
#include "oic/store/paths.h"

namespace {

// Writes rows from a worker thread: the store keys its connections by thread id,
// so this is what proves two threads (and, by the same code path, the GUI and MCP
// processes) do not fight over one handle.
class Writer : public QThread {
public:
    Writer(QString path, QString prefix, int count)
        : m_path(std::move(path)), m_prefix(std::move(prefix)), m_count(count)
    {
    }

    QString errorText() const { return m_error; }
    int written() const { return m_written; }

protected:
    void run() override
    {
        oic::store::Database database(m_path);
        QString error;
        if (!database.open(&error)) {
            m_error = error;
            return;
        }
        for (int i = 0; i < m_count; ++i) {
            oic::store::Job job;
            job.id = QStringLiteral("%1-%2").arg(m_prefix).arg(i);
            job.mode = QStringLiteral("generate");
            job.protocol = QStringLiteral("openai");
            job.profile = m_prefix;
            job.status = QStringLiteral("succeeded");
            if (!database.createJob(job, &error)) {
                m_error = error;
                return;
            }
            ++m_written;
        }
        database.close();
    }

private:
    QString m_path;
    QString m_prefix;
    int m_count = 0;
    QString m_error;
    int m_written = 0;
};

}  // namespace

class TstStore : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_file;

    std::unique_ptr<oic::store::Database> openStore(QString *error)
    {
        auto store = std::make_unique<oic::store::Database>(m_file);
        if (!store->open(error)) {
            return nullptr;
        }
        return store;
    }

    static oic::store::Job makeJob(const QString &id, qint64 createdAt, const QString &status = QStringLiteral("succeeded"))
    {
        oic::store::Job job;
        job.id = id;
        job.createdAt = createdAt;
        job.updatedAt = createdAt;
        job.mode = QStringLiteral("generate");
        job.protocol = QStringLiteral("openai");
        job.profile = QStringLiteral("default");
        job.model = QStringLiteral("gpt-image-2");
        job.prompt = QStringLiteral("一只戴帽子的猫");
        job.size = QStringLiteral("1024x1024");
        job.n = 1;
        job.status = status;
        return job;
    }

    static oic::store::Asset makeAsset(const QString &id, const QString &jobId, qint64 bytes, qint64 createdAt)
    {
        oic::store::Asset asset;
        asset.id = id;
        asset.jobId = jobId;
        asset.ordinal = 0;
        asset.filename = id + QStringLiteral(".png");
        asset.relPath = jobId + QStringLiteral("/") + id + QStringLiteral(".png");
        asset.bytes = bytes;
        asset.mime = QStringLiteral("image/png");
        asset.width = 8;
        asset.height = 8;
        asset.createdAt = createdAt;
        return asset;
    }

private slots:
    void init()
    {
        QVERIFY(m_dir.isValid());
        m_file = m_dir.filePath(QStringLiteral("library.sqlite3"));
        QFile::remove(m_file);
    }

    void pathsUseLocalAppDataAndRefuseToGuess();
    void freshFileGetsSchemaVersion();
    void newerSchemaIsRefusedNotDowngraded();
    void reopenKeepsRows();
    void profileRoundTrip();
    void jobLifecycleAndUnicode();
    void softDeleteHidesFromDefaultListing();
    void assetCascadeFollowsJob();
    void retentionKeepsNewestAndPinned();
    void byteBudgetDropsOldestUnpinnedAssets();
    void twoThreadsWriteConcurrently();
    void concurrentFirstOpenCreatesTheDatabase();
    void secretTextNeverEntersDatabaseFiles();
};

void TstStore::pathsUseLocalAppDataAndRefuseToGuess()
{
    const QString saved = qEnvironmentVariable("LOCALAPPDATA");
    QVERIFY(!saved.isEmpty());

    const oic::store::Paths good = oic::store::resolvePaths();
    QVERIFY2(good.ok(), qPrintable(good.error));
    QVERIFY2(good.root.startsWith(QDir::fromNativeSeparators(saved)),
             qPrintable(QStringLiteral("root=%1 localAppData=%2").arg(good.root, saved)));
    QVERIFY(good.root.endsWith(QStringLiteral("/image-client")));
    QVERIFY(good.database.endsWith(QStringLiteral("library.sqlite3")));
    QVERIFY(good.assets.endsWith(QStringLiteral("/assets")));

    // No LOCALAPPDATA means no safe location, so the answer must be an error --
    // not a temp-directory fallback, which is exactly what SPEC 6.1 forbids.
    qputenv("LOCALAPPDATA", QByteArray());
    const oic::store::Paths without = oic::store::resolvePaths();
    QVERIFY(!without.ok());
    QVERIFY(without.root.isEmpty());
    QVERIFY(without.error.contains(QString::fromUtf8("LOCALAPPDATA")));

    oic::store::Paths unresolved;
    QVERIFY(!oic::store::ensureDirectories(&unresolved));
    QVERIFY(!unresolved.error.isEmpty());
    qputenv("LOCALAPPDATA", saved.toUtf8());
}

void TstStore::freshFileGetsSchemaVersion()
{
    QString error;
    auto store = openStore(&error);
    QVERIFY2(store != nullptr, qPrintable(error));
    QCOMPARE(oic::store::Database::schemaVersion(), 1);
    QVERIFY(QFileInfo::exists(m_file));
}

void TstStore::newerSchemaIsRefusedNotDowngraded()
{
    {
        QString error;
        auto store = openStore(&error);
        QVERIFY2(store != nullptr, qPrintable(error));
    }

    // Simulate a future build having written the file.
    QSqlDatabase probe = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("tst-store-probe"));
    probe.setDatabaseName(m_file);
    QVERIFY(probe.open());
    QVERIFY(QSqlQuery(probe).exec(QStringLiteral("PRAGMA user_version=77")));
    probe.close();
    QSqlDatabase::removeDatabase(QStringLiteral("tst-store-probe"));

    oic::store::Database store(m_file);
    QString error;
    QVERIFY2(!store.open(&error), "opened a database written by a newer version");
    QVERIFY2(error.contains(QString::fromUtf8("更新")), qPrintable(error));
}

void TstStore::reopenKeepsRows()
{
    QString error;
    {
        auto store = openStore(&error);
        QVERIFY2(store != nullptr, qPrintable(error));
        QVERIFY2(store->createJob(makeJob(QStringLiteral("keep-me"), 100), &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    }
    {
        auto reopened = openStore(&error);
        QVERIFY2(reopened != nullptr, qPrintable(error));
        bool found = false;
        const oic::store::Job job = reopened->job(QStringLiteral("keep-me"), &found, &error);
        QVERIFY2(found, qPrintable(error));
        QCOMPARE(job.prompt, QStringLiteral("一只戴帽子的猫"));
        QCOMPARE(job.status, QStringLiteral("succeeded"));
    }
}

void TstStore::profileRoundTrip()
{
    QString error;
    auto store = openStore(&error);
    QVERIFY2(store != nullptr, qPrintable(error));

    oic::store::Profile profile;
    profile.name = QStringLiteral("默认");
    profile.baseUrl = QStringLiteral("https://api.example.com/v1");
    profile.protocol = QStringLiteral("openai");
    profile.imageModel = QStringLiteral("gpt-image-2");
    profile.allowedModels = { QStringLiteral("gpt-image-2"), QStringLiteral("grok-imagine-image") };
    profile.timeoutSeconds = 300;
    profile.credentialTarget = QStringLiteral("image-client/profile/default");
    QVERIFY2(store->writeProfile(profile, &error), qPrintable(error));

    const QList<oic::store::Profile> all = store->profiles(&error);
    QCOMPARE(all.size(), 1);
    QCOMPARE(all.at(0).allowedModels, profile.allowedModels);
    QCOMPARE(all.at(0).timeoutSeconds, 300);
    QCOMPARE(all.at(0).createdAt > 0, true);

    // Same name updates rather than duplicating, and keeps created_at.
    oic::store::Profile edited = profile;
    edited.baseUrl = QStringLiteral("https://other.example.com/v1");
    edited.allowedModels = { QStringLiteral("gpt-image-2.5-flare") };
    QVERIFY2(store->writeProfile(edited, &error), qPrintable(error));
    const QList<oic::store::Profile> after = store->profiles(&error);
    QCOMPARE(after.size(), 1);
    QCOMPARE(after.at(0).baseUrl, edited.baseUrl);
    QCOMPARE(after.at(0).allowedModels, edited.allowedModels);
    // created_at is the store's stamp from the first write; an update must not
    // rewrite it. profile.createdAt was left 0 on purpose, so it cannot be the
    // expected value here.
    QCOMPARE(after.at(0).createdAt, all.at(0).createdAt);
    QVERIFY(all.at(0).createdAt > 0);

    QVERIFY2(store->removeProfile(QStringLiteral("默认"), &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    QVERIFY2(store->profiles(&error).isEmpty(), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
}

void TstStore::jobLifecycleAndUnicode()
{
    QString error;
    auto store = openStore(&error);
    QVERIFY2(store != nullptr, qPrintable(error));

    QVERIFY2(store->createJob(makeJob(QStringLiteral("a"), 10), &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    QVERIFY2(store->createJob(makeJob(QStringLiteral("b"), 20), &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    QVERIFY2(store->createJob(makeJob(QStringLiteral("c"), 30, QStringLiteral("failed")), &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));

    const QList<oic::store::Job> newest = store->listJobs(2, 0, false, &error);
    QCOMPARE(newest.size(), 2);
    QCOMPARE(newest.at(0).id, QStringLiteral("c"));  // created_at DESC
    QCOMPARE(newest.at(1).id, QStringLiteral("b"));

    oic::store::Job failed = makeJob(QStringLiteral("c"), 30, QStringLiteral("failed"));
    failed.error = QStringLiteral("接口返回异常，未找到 data");
    failed.durationMs = 4211;
    failed.endpoint = QStringLiteral("https://api.example.com/v1/images/generations");
    QVERIFY2(store->updateJob(failed, &error), qPrintable(error));

    bool found = false;
    const oic::store::Job reread = store->job(QStringLiteral("c"), &found, &error);
    QVERIFY(found);
    QCOMPARE(reread.error, failed.error);
    QCOMPARE(reread.durationMs, 4211LL);

    oic::store::Job missing;
    QVERIFY2(!store->updateJob(makeJob(QStringLiteral("nope"), 1), &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    QVERIFY(!error.isEmpty());
    store->job(QStringLiteral("nope"), &found, &error);
    QVERIFY(!found);
}

void TstStore::softDeleteHidesFromDefaultListing()
{
    QString error;
    auto store = openStore(&error);
    QVERIFY2(store != nullptr, qPrintable(error));
    QVERIFY2(store->createJob(makeJob(QStringLiteral("live"), 10), &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    QVERIFY2(store->createJob(makeJob(QStringLiteral("gone"), 20), &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));

    QVERIFY2(store->softDeleteJob(QStringLiteral("gone"), 999, &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    QStringList visible;
    const QList<oic::store::Job> listed = store->listJobs(10, 0, false, &error);
    for (const oic::store::Job &job : listed) {
        visible.append(job.id);
    }
    QVERIFY(!visible.contains(QStringLiteral("gone")));
    QVERIFY(visible.contains(QStringLiteral("live")));
    QCOMPARE(store->listJobs(10, 0, true, &error).size(), 2);

    bool found = false;
    QCOMPARE(store->job(QStringLiteral("gone"), &found, &error).deletedAt, 999LL);

    QVERIFY2(store->setPinned(QStringLiteral("live"), true, &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    QVERIFY2(store->listJobs(10, 0, false, &error).at(0).pinned, qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    QVERIFY2(!store->setPinned(QStringLiteral("not-there"), true, &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
}

void TstStore::assetCascadeFollowsJob()
{
    QString error;
    auto store = openStore(&error);
    QVERIFY2(store != nullptr, qPrintable(error));
    QVERIFY2(store->createJob(makeJob(QStringLiteral("j1"), 10), &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    QVERIFY2(store->addAsset(makeAsset(QStringLiteral("a1"), QStringLiteral("j1"), 100, 10), &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    QVERIFY2(store->addAsset(makeAsset(QStringLiteral("a2"), QStringLiteral("j1"), 200, 10), &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    QCOMPARE(store->assetsForJob(QStringLiteral("j1"), &error).size(), 2);
    QCOMPARE(store->allAssets(&error).size(), 2);

    // ON DELETE CASCADE plus PRAGMA foreign_keys=ON per connection: no orphans.
    QVERIFY2(store->setPinned(QStringLiteral("j1"), false, &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    oic::store::PruneReport report = store->pruneToLimits(0, 0, &error);
    QCOMPARE(report.removedJobs.size(), 1);
    QCOMPARE(report.removedAssets.size(), 2);
    QCOMPARE(report.freedBytes, 300LL);
    QVERIFY2(store->allAssets(&error).isEmpty(), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));

    // An asset pointing at nothing is a bug, not a row to keep.
    QVERIFY2(!store->addAsset(makeAsset(QStringLiteral("orphan"), QStringLiteral("ghost"), 5, 5), &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    QVERIFY(!error.isEmpty());
}

void TstStore::retentionKeepsNewestAndPinned()
{
    QString error;
    auto store = openStore(&error);
    QVERIFY2(store != nullptr, qPrintable(error));
    for (int i = 1; i <= 5; ++i) {
        QVERIFY2(store->createJob(makeJob(QStringLiteral("j%1").arg(i), i * 10), &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    }
    QVERIFY2(store->setPinned(QStringLiteral("j1"), true, &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));

    const oic::store::PruneReport report = store->pruneToLimits(2, 0, &error);
    QStringList dropped;
    for (const QString &id : report.removedJobs) {
        dropped.append(id);
    }
    // A budget of 2 live rows drops the two oldest; j1 is pinned so it is not
    // even a candidate, and j5 is the newest.
    QCOMPARE(dropped, QStringList({ QStringLiteral("j2"), QStringLiteral("j3") }));

    QStringList left;
    const QList<oic::store::Job> remaining = store->listJobs(10, 0, false, &error);
    for (const oic::store::Job &job : remaining) {
        left.append(job.id);
    }
    left.sort();
    // The budget counts *live unpinned* rows, so the pinned j1 sits outside it:
    // j4 and j5 are the two newest live rows that survive alongside j1.
    QCOMPARE(left, QStringList({ QStringLiteral("j1"), QStringLiteral("j4"), QStringLiteral("j5") }));

    // Soft-deleted rows are purged regardless of the row budget.
    QVERIFY2(store->softDeleteJob(QStringLiteral("j5"), 500, &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    const oic::store::PruneReport second = store->pruneToLimits(50, 0, &error);
    QCOMPARE(second.removedJobs, QStringList({ QStringLiteral("j5") }));
}

void TstStore::byteBudgetDropsOldestUnpinnedAssets()
{
    QString error;
    auto store = openStore(&error);
    QVERIFY2(store != nullptr, qPrintable(error));
    QVERIFY2(store->createJob(makeJob(QStringLiteral("old"), 10), &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    QVERIFY2(store->createJob(makeJob(QStringLiteral("kept"), 20), &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    QVERIFY2(store->createJob(makeJob(QStringLiteral("pinned"), 30), &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    QVERIFY2(store->setPinned(QStringLiteral("pinned"), true, &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));

    QVERIFY2(store->addAsset(makeAsset(QStringLiteral("a-old"), QStringLiteral("old"), 400, 10), &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    QVERIFY2(store->addAsset(makeAsset(QStringLiteral("a-kept"), QStringLiteral("kept"), 300, 20), &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    QVERIFY2(store->addAsset(makeAsset(QStringLiteral("a-pinned"), QStringLiteral("pinned"), 900, 30), &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));

    // 1600 total against a 1000 budget: the oldest unpinned asset goes, then the
    // next, and the pinned job's 900 bytes are untouchable even though dropping
    // them alone would satisfy the budget.
    const oic::store::PruneReport report = store->pruneToLimits(1000, 1000, &error);
    QStringList freed;
    for (const oic::store::Asset &asset : report.removedAssets) {
        freed.append(asset.id);
    }
    QVERIFY2(freed.contains(QStringLiteral("a-old")), qPrintable(error));
    QVERIFY2(freed.contains(QStringLiteral("a-kept")), qPrintable(freed.join(",")));
    QVERIFY(!freed.contains(QStringLiteral("a-pinned")));
    QVERIFY(report.freedBytes >= 700);

    QStringList remaining;
    for (const oic::store::Asset &asset : store->allAssets(&error)) {
        remaining.append(asset.id);
    }
    QCOMPARE(remaining, QStringList({ QStringLiteral("a-pinned") }));
}

void TstStore::twoThreadsWriteConcurrently()
{
    QString error;
    {
        auto store = openStore(&error);
        QVERIFY2(store != nullptr, qPrintable(error));
    }

    Writer first(m_file, QStringLiteral("t1"), 25);
    Writer second(m_file, QStringLiteral("t2"), 25);
    first.start();
    second.start();
    first.wait(20000);
    second.wait(20000);

    QCOMPARE(first.written(), 25);
    QCOMPARE(second.written(), 25);
    QVERIFY2(first.errorText().isEmpty(), qPrintable(first.errorText()));
    QVERIFY2(second.errorText().isEmpty(), qPrintable(second.errorText()));

    auto store = openStore(&error);
    QVERIFY2(store != nullptr, qPrintable(error));
    QCOMPARE(store->listJobs(100, 0, false, &error).size(), 50);
}

// A thread that opens the database at a shared starting gate, so every opener
// reaches Database::open() as close to simultaneously as possible.
class FirstOpener : public QThread {
public:
    FirstOpener(QString path, QSemaphore *gate)
        : m_path(std::move(path)), m_gate(gate)
    {
    }

    QString errorText() const { return m_error; }
    bool opened() const { return m_opened; }

protected:
    void run() override
    {
        m_gate->acquire();  // block until the test releases all openers at once
        oic::store::Database database(m_path);
        QString error;
        m_opened = database.open(&error);
        if (!m_opened) {
            m_error = error;
        }
        database.close();
    }

private:
    QString m_path;
    QSemaphore *m_gate = nullptr;
    QString m_error;
    bool m_opened = false;
};

// SPEC 6.2's WAL switch is a one-time, exclusive schema change: switching the
// journal mode on a brand-new file takes an exclusive lock that the busy handler
// does NOT cover. When several threads (JobManager's workers) open a *fresh*
// database at the same time, the losers used to fail with "database is locked"
// instead of waiting. This is the case twoThreadsWriteConcurrently misses: it
// opens the store once first, so WAL is already set before the threads race.
void TstStore::concurrentFirstOpenCreatesTheDatabase()
{
    const int kOpeners = 8;
    QSemaphore gate;
    QList<FirstOpener *> openers;
    for (int i = 0; i < kOpeners; ++i)
        openers.append(new FirstOpener(m_file, &gate));

    for (FirstOpener *opener : openers)
        opener->start();
    gate.release(kOpeners);  // start them together

    int failures = 0;
    QStringList messages;
    for (FirstOpener *opener : openers) {
        QVERIFY2(opener->wait(20000), "an opener should not hang");
        if (!opener->opened()) {
            ++failures;
            messages.append(opener->errorText());
        }
        delete opener;
    }

    QVERIFY2(failures == 0,
             qPrintable(QStringLiteral("%1/%2 concurrent first opens failed: %3")
                            .arg(failures)
                            .arg(kOpeners)
                            .arg(messages.join(QStringLiteral(" | ")))));
}

// The point of SPEC 6.3 is that the key lives only in the Credential Manager, so
// the database file must contain the pointer and never the secret.
void TstStore::secretTextNeverEntersDatabaseFiles()
{
    const QByteArray sentinel = QByteArrayLiteral("sk-SENTINEL-must-never-hit-disk-0123456789");

    QString error;
    auto store = openStore(&error);
    QVERIFY2(store != nullptr, qPrintable(error));
    oic::store::Profile profile;
    profile.name = QStringLiteral("default");
    profile.baseUrl = QStringLiteral("https://api.example.com/v1");
    profile.protocol = QStringLiteral("openai");
    profile.credentialTarget = QStringLiteral("image-client/profile/default");
    QVERIFY2(store->writeProfile(profile, &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));
    QVERIFY2(store->createJob(makeJob(QStringLiteral("withprompt"), 10), &error), qPrintable(QStringLiteral("createJob/update: %1").arg(error)));

    // WAL keeps recent writes in sidecar files, so scan every file in the dir.
    const QFileInfoList entries = QDir(m_dir.path()).entryInfoList(QDir::Files);
    QVERIFY(!entries.isEmpty());
    bool sawTargetName = false;
    for (const QFileInfo &entry : entries) {
        QFile file(entry.absoluteFilePath());
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
        const QByteArray content = file.readAll();
        QVERIFY2(!content.contains(sentinel),
                 qPrintable(QStringLiteral("key plaintext found in %1").arg(entry.fileName())));
        sawTargetName = sawTargetName || content.contains("image-client/profile/default");
    }
    QVERIFY2(sawTargetName, "the credential pointer should be stored; the test would be vacuous without it");
}

QTEST_GUILESS_MAIN(TstStore)
#include "tst_store.moc"
