// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
//
// JobManager: the bounded pool, its fail-fast submit, cooperative cancel, queued
// finished signal, and TTL eviction. The fake sender is mutex-guarded because workers
// call it concurrently. Real-transport wiring is covered separately (tst_jobs_integration).

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <atomic>
#include <functional>
#include <memory>

#include "oic/jobs/jobmanager.h"
#include "oic/net/transport.h"

using namespace oic::jobs;

namespace {

const char *kSentinel = "SENTINEL-KEY-9f3a";

QByteArray fakePng(int size)
{
    QByteArray payload = QByteArray::fromHex("89504E470D0A1A0A0000000D494844520000000800000004");
    payload += QByteArray(qMax(0, size - payload.size()), 'p');
    return payload;
}

QByteArray imageBody()
{
    QJsonObject item;
    item.insert(QStringLiteral("b64_json"), QString::fromLatin1(fakePng(40).toBase64()));
    QJsonArray data;
    data.append(item);
    QJsonObject body;
    body.insert(QStringLiteral("data"), data);
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

oic::net::Reply makeReply(int status, const QString &contentType, const QByteArray &body)
{
    oic::net::Reply reply;
    reply.status = status;
    reply.contentType = contentType;
    reply.body = body;
    return reply;
}

// Shared, thread-safe state the fake sender reads and updates from worker threads.
struct Shared {
    QMutex mutex;
    int active = 0;
    int maxActive = 0;
    int total = 0;
    bool block = false;              // hold inside send until release is set
    int holdMs = 0;                  // otherwise sleep this long to force overlap
    std::atomic_bool release{false};
    oic::net::Reply reply;
};

// Releases a blocked fake sender on scope exit, even when an assertion fails mid-test, so
// the JobManager destructor can always join its workers instead of hanging. Without this a
// genuine regression would surface as a CI timeout rather than a reported failure.
struct ReleaseGuard {
    std::shared_ptr<Shared> shared;
    ~ReleaseGuard() { shared->release.store(true); }
};

Sender makeSender(const std::shared_ptr<Shared> &shared)
{
    return [shared](const oic::net::Request &) -> oic::net::Reply {
        {
            QMutexLocker lock(&shared->mutex);
            ++shared->active;
            shared->maxActive = qMax(shared->maxActive, shared->active);
            ++shared->total;
        }
        if (shared->block) {
            while (!shared->release.load())
                QThread::msleep(5);
        } else if (shared->holdMs > 0) {
            QThread::msleep(shared->holdMs);
        }
        {
            QMutexLocker lock(&shared->mutex);
            --shared->active;
        }
        return shared->reply;
    };
}

// A bare host → two candidate endpoints, so a cancelled/failed first probe has a
// second checkpoint where the cancel is observed.
JobSpec twoCandidateSpec()
{
    JobSpec spec;
    spec.profileName = QStringLiteral("p");
    spec.baseUrl = QStringLiteral("https://api.example.com");
    spec.protocolHint = QStringLiteral("openai");
    spec.credentialTarget = QStringLiteral("image-client/profile/p");
    spec.image.model = QStringLiteral("gpt-image-2");
    spec.image.prompt = QStringLiteral("a cat");
    spec.image.size = QStringLiteral("1024x1024");
    spec.image.n = 1;
    return spec;
}

JobDeps baseDeps(const QString &dbPath, const QString &assetsRoot, const std::shared_ptr<Shared> &shared)
{
    JobDeps deps;
    deps.databasePath = dbPath;
    deps.assetsRoot = assetsRoot;
    deps.assetQuotaBytes = 64LL * 1024 * 1024;
    deps.sender = makeSender(shared);
    deps.secrets = [](const QString &, QString *, bool *) { return QByteArray(kSentinel); };
    return deps;
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate()) {
        if (timer.elapsed() > timeoutMs)
            return false;
        QTest::qWait(10);
    }
    return true;
}

}  // namespace

class TstJobManager : public QObject
{
    Q_OBJECT

private slots:
    void finishedSignalCarriesOutcome();
    void concurrencyIsBoundedByConfig();
    void submitFailsFastWhenQueueFull();
    void cancelProducesCancelledOutcome();
    void expiredEntriesAreEvicted();
};

void TstJobManager::finishedSignalCarriesOutcome()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto shared = std::make_shared<Shared>();
    shared->reply = makeReply(200, QStringLiteral("application/json"), imageBody());
    const JobDeps deps = baseDeps(dir.filePath(QStringLiteral("lib.sqlite3")), dir.path(), shared);

    JobManager manager(deps);
    QSignalSpy finished(&manager, &JobManager::finished);
    QVERIFY(finished.isValid());

    QString error;
    const QString id = manager.submit(twoCandidateSpec(), &error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(0).toString(), id);

    const JobOutcome outcome = finished.at(0).at(1).value<JobOutcome>();
    QCOMPARE(outcome.jobId, id);
    QCOMPARE(outcome.status, QStringLiteral("succeeded"));
    QCOMPARE(outcome.assets.size(), 1);
}

void TstJobManager::concurrencyIsBoundedByConfig()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto shared = std::make_shared<Shared>();
    shared->reply = makeReply(200, QStringLiteral("application/json"), imageBody());
    shared->holdMs = 40;  // long enough that two workers overlap
    const JobDeps deps = baseDeps(dir.filePath(QStringLiteral("lib.sqlite3")), dir.path(), shared);

    JobManager::Config config;
    config.maxConcurrent = 2;
    JobManager manager(deps, config);
    QSignalSpy finished(&manager, &JobManager::finished);

    for (int i = 0; i < 5; ++i) {
        QString error;
        QVERIFY2(!manager.submit(twoCandidateSpec(), &error).isEmpty(), qPrintable(error));
    }
    QVERIFY2(waitFor([&] { return finished.count() >= 5; }, 8000), "five jobs should all finish");
    QCOMPARE(finished.count(), 5);

    QMutexLocker lock(&shared->mutex);
    QVERIFY2(shared->maxActive <= 2,
             qPrintable(QStringLiteral("maxActive=%1 exceeded the pool bound").arg(shared->maxActive)));
    QCOMPARE(shared->total, 5);
}

void TstJobManager::submitFailsFastWhenQueueFull()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto shared = std::make_shared<Shared>();
    shared->block = true;  // hold the single worker so the queue fills deterministically
    shared->reply = makeReply(500, QStringLiteral("application/json"),
                              QByteArray("{\"error\":{\"type\":\"upstream\"}}"));
    const JobDeps deps = baseDeps(dir.filePath(QStringLiteral("lib.sqlite3")), dir.path(), shared);

    JobManager::Config config;
    config.maxConcurrent = 1;
    config.maxPending = 2;
    JobManager manager(deps, config);
    ReleaseGuard guard{shared};  // after manager → destroyed first → releases the worker before the dtor joins

    QString error;
    const QString a = manager.submit(twoCandidateSpec(), &error);
    QVERIFY2(!a.isEmpty(), qPrintable(error));
    QVERIFY2(waitFor([&] { return manager.runningCount() == 1; }, 3000), "worker should pick up the first job");

    const QString b = manager.submit(twoCandidateSpec(), &error);
    QVERIFY(!b.isEmpty());  // queue = 1
    const QString c = manager.submit(twoCandidateSpec(), &error);
    QVERIFY(!c.isEmpty());  // queue = 2
    const QString d = manager.submit(twoCandidateSpec(), &error);
    QVERIFY2(d.isEmpty(), "a full queue must reject rather than block or grow");
    QVERIFY(!error.isEmpty());
}

void TstJobManager::cancelProducesCancelledOutcome()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto shared = std::make_shared<Shared>();
    shared->block = true;
    shared->reply = makeReply(500, QStringLiteral("application/json"),
                              QByteArray("{\"error\":{\"type\":\"upstream\"}}"));
    const JobDeps deps = baseDeps(dir.filePath(QStringLiteral("lib.sqlite3")), dir.path(), shared);

    JobManager::Config config;
    config.maxConcurrent = 1;
    JobManager manager(deps, config);
    ReleaseGuard guard{shared};  // after manager → destroyed first → releases before the dtor joins
    QSignalSpy finished(&manager, &JobManager::finished);

    QString error;
    const QString id = manager.submit(twoCandidateSpec(), &error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY2(waitFor([&] { return manager.runningCount() == 1; }, 3000), "job should be running");

    QVERIFY(manager.cancel(id));
    shared->release.store(true);  // the blocked send returns; the next checkpoint sees the cancel

    QVERIFY(finished.wait(5000));
    const JobOutcome outcome = finished.at(0).at(1).value<JobOutcome>();
    QCOMPARE(outcome.jobId, id);
    QCOMPARE(outcome.status, QStringLiteral("cancelled"));
    QVERIFY(outcome.assets.isEmpty());
}

void TstJobManager::expiredEntriesAreEvicted()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto shared = std::make_shared<Shared>();
    shared->reply = makeReply(200, QStringLiteral("application/json"), imageBody());
    JobDeps deps = baseDeps(dir.filePath(QStringLiteral("lib.sqlite3")), dir.path(), shared);

    std::atomic<qint64> fakeNow{1000000};
    deps.clock = [&fakeNow] { return fakeNow.load(); };

    JobManager::Config config;
    config.maxConcurrent = 1;
    config.ttlSeconds = 60;  // short, so the test does not wait an hour
    JobManager manager(deps, config);
    QSignalSpy finished(&manager, &JobManager::finished);

    QString error;
    const QString first = manager.submit(twoCandidateSpec(), &error);
    QVERIFY2(!first.isEmpty(), qPrintable(error));
    QVERIFY(finished.wait(5000));
    QCOMPARE(manager.liveCount(), 1);

    // Advance past the TTL and submit again: submit() evicts the expired finished entry.
    fakeNow.store(1000000 + 61 * 1000);
    const QString second = manager.submit(twoCandidateSpec(), &error);
    QVERIFY2(!second.isEmpty(), qPrintable(error));
    QCOMPARE(manager.liveCount(), 1);  // first evicted, second tracked
    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.count(), 2);
}

QTEST_GUILESS_MAIN(TstJobManager)
#include "tst_jobmanager.moc"
