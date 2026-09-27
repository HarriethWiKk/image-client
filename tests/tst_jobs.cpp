// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
//
// The jobs executor: the wiring that turns protocol + transport + parsers + store into
// one persisted task. Every external seam (network, secret, clock, media fetch) is
// injected, so these run with no network and no real Credential Manager. The path that
// uses a real net::Transport over loopback lives in tst_jobs_integration, because a fake
// Sender bypasses the SSRF / Host-pinning / redirect behaviour the transport exists for.

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "oic/core/limits.h"
#include "oic/jobs/executor.h"
#include "oic/net/transport.h"
#include "oic/protocol/imageprotocol.h"
#include "oic/store/database.h"

using namespace oic::jobs;

namespace {

const char *kSentinel = "SENTINEL-KEY-do-not-persist-9f3a";

// A real PNG container header declaring 8x4 (non-square, so a width/height swap fails),
// padded to `size`. Same construction tst_parsers uses; keeps this suite GUI-less.
QByteArray fakePng(int size)
{
    QByteArray payload = QByteArray::fromHex("89504E470D0A1A0A0000000D494844520000000800000004");
    payload += QByteArray(qMax(0, size - payload.size()), 'p');
    return payload;
}

QString b64(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toBase64());
}

oic::net::Reply makeReply(int status, const QString &contentType, const QByteArray &body)
{
    oic::net::Reply reply;
    reply.status = status;
    reply.contentType = contentType;
    reply.body = body;
    return reply;
}

QByteArray b64ImagesBody(const QStringList &items)
{
    QJsonArray data;
    for (const QString &item : items) {
        QJsonObject object;
        object.insert(QStringLiteral("b64_json"), item);
        data.append(object);
    }
    QJsonObject body;
    body.insert(QStringLiteral("data"), data);
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QByteArray urlBody(const QString &url)
{
    QJsonArray data;
    QJsonObject item;
    item.insert(QStringLiteral("url"), url);
    data.append(item);
    QJsonObject body;
    body.insert(QStringLiteral("data"), data);
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

// Records what the executor sent and serves canned replies in order (the last repeats).
struct FakeNet {
    int calls = 0;
    QList<oic::net::Request> requests;
    QList<oic::net::Reply> replies;
    std::function<void(int)> afterCall;

    oic::net::Reply send(const oic::net::Request &request)
    {
        requests.append(request);
        const int index = calls++;
        if (afterCall)
            afterCall(index);
        if (replies.isEmpty())
            return oic::net::Reply{};
        return replies.at(qMin<qsizetype>(index, replies.size() - 1));
    }
};

// A bare host yields two candidate endpoints (SPEC 4): .../v1/<route> then .../<route>.
JobSpec genSpec()
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

JobDeps makeDeps(const QString &dbPath, const QString &assetsRoot, const std::shared_ptr<FakeNet> &net)
{
    JobDeps deps;
    deps.databasePath = dbPath;
    deps.assetsRoot = assetsRoot;
    deps.assetQuotaBytes = 64LL * 1024 * 1024;
    deps.sender = [net](const oic::net::Request &request) { return net->send(request); };
    deps.secrets = [](const QString &, QString *, bool *) { return QByteArray(kSentinel); };
    deps.clock = [] { return QDateTime::currentMSecsSinceEpoch(); };
    return deps;
}

bool hasContentType(const oic::net::Request &request, const QByteArray &needle)
{
    for (const oic::net::Header &header : request.headers) {
        if (header.name.toLower() == QByteArrayLiteral("content-type") && header.value.contains(needle))
            return true;
    }
    return false;
}

}  // namespace

class TstJobs : public QObject
{
    Q_OBJECT

private slots:
    void successWritesAssetAndPersistsJob();
    void candidateRetryOn500UsesSecondEndpoint();
    void clientErrorStopsWithoutResendingMultipart();
    void htmlErrorPageKeepsProbing();
    void notFoundTriesNextCandidate();
    void cumulativeSizeCapFailsWholeJob();
    void nonImageBase64IsRejectedByProbe();
    void cancelStopsJobWithNoAssets();
    void deadlineStopsJobBeforeSending();
    void urlDownloadFailureKeepsRemoteLink();
    void urlDownloadSuccessWritesAsset();
    void keyNeverPersisted();
};

void TstJobs::successWritesAssetAndPersistsJob()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("lib.sqlite3"));

    auto net = std::make_shared<FakeNet>();
    net->replies.append(makeReply(200, QStringLiteral("application/json"), b64ImagesBody({ b64(fakePng(40)) })));
    const JobDeps deps = makeDeps(dbPath, dir.path(), net);

    QString error;
    const JobOutcome outcome = runJob(genSpec(), deps, nullptr, &error);
    QVERIFY2(outcome.status == QLatin1String("succeeded"), qPrintable(outcome.error));
    QCOMPARE(net->calls, 1);
    QCOMPARE(outcome.assets.size(), 1);

    const AssetRef ref = outcome.assets.at(0);
    QCOMPARE(ref.width, 8);
    QCOMPARE(ref.height, 4);
    QCOMPARE(ref.mime, QStringLiteral("image/png"));
    QVERIFY2(QFile::exists(QDir(deps.assetsRoot).filePath(ref.relPath)), qPrintable(ref.relPath));

    oic::store::Database verify(dbPath);
    QVERIFY(verify.open(&error));
    bool found = false;
    const oic::store::Job job = verify.job(outcome.jobId, &found, &error);
    QVERIFY(found);
    QCOMPARE(job.status, QStringLiteral("succeeded"));
    QCOMPARE(job.protocol, QStringLiteral("openai"));
    const QList<oic::store::Asset> assets = verify.assetsForJob(outcome.jobId, &error);
    QCOMPARE(assets.size(), 1);
    QCOMPARE(assets.at(0).width, 8);
    QCOMPARE(assets.at(0).height, 4);
    QCOMPARE(assets.at(0).sha256.size(), 64);  // hex sha256
    QVERIFY2(job.resultJson.contains(ref.relPath), qPrintable(job.resultJson));
}

void TstJobs::candidateRetryOn500UsesSecondEndpoint()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto net = std::make_shared<FakeNet>();
    net->replies.append(makeReply(500, QStringLiteral("application/json"),
                                  QByteArray("{\"error\":{\"type\":\"upstream\"}}")));
    net->replies.append(makeReply(200, QStringLiteral("application/json"), b64ImagesBody({ b64(fakePng(30)) })));
    const JobDeps deps = makeDeps(dir.filePath(QStringLiteral("lib.sqlite3")), dir.path(), net);

    QString error;
    const JobOutcome outcome = runJob(genSpec(), deps, nullptr, &error);
    QVERIFY2(outcome.status == QLatin1String("succeeded"), qPrintable(outcome.error));
    QCOMPARE(net->calls, 2);
    // First candidate is the /v1 form, second is not: proves ordered probing (SPEC 4).
    QVERIFY2(net->requests.at(0).url.toString().contains(QStringLiteral("/v1/")),
             qPrintable(net->requests.at(0).url.toString()));
    QVERIFY2(!net->requests.at(1).url.toString().contains(QStringLiteral("/v1/")),
             qPrintable(net->requests.at(1).url.toString()));
}

void TstJobs::clientErrorStopsWithoutResendingMultipart()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    JobSpec spec = genSpec();
    spec.image.editMode = true;
    oic::protocol::ReferenceImage ref;
    ref.name = QStringLiteral("r.png");
    ref.dataUrl = QStringLiteral("data:image/png;base64,") + b64(fakePng(60));
    spec.image.references.append(ref);

    auto net = std::make_shared<FakeNet>();
    net->replies.append(makeReply(400, QStringLiteral("application/json"), QByteArray("{\"error\":\"bad\"}")));
    const JobDeps deps = makeDeps(dir.filePath(QStringLiteral("lib.sqlite3")), dir.path(), net);

    QString error;
    const JobOutcome outcome = runJob(spec, deps, nullptr, &error);
    QCOMPARE(outcome.status, QStringLiteral("failed"));
    // SPEC 4: a 4xx client refusal must NOT re-POST the multipart body to candidate 2 --
    // that resend is what double-billed in the old implementation.
    QCOMPARE(net->calls, 1);
    QVERIFY(hasContentType(net->requests.at(0), QByteArrayLiteral("multipart")));
}

void TstJobs::htmlErrorPageKeepsProbing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto net = std::make_shared<FakeNet>();
    net->replies.append(makeReply(400, QStringLiteral("text/html"), QByteArray("<html>gateway</html>")));
    net->replies.append(makeReply(200, QStringLiteral("application/json"), b64ImagesBody({ b64(fakePng(30)) })));
    const JobDeps deps = makeDeps(dir.filePath(QStringLiteral("lib.sqlite3")), dir.path(), net);

    QString error;
    const JobOutcome outcome = runJob(genSpec(), deps, nullptr, &error);
    QVERIFY2(outcome.status == QLatin1String("succeeded"), qPrintable(outcome.error));
    // A 400 whose body is HTML is not a payload refusal, so probing continues (SPEC 4).
    QCOMPARE(net->calls, 2);
}

void TstJobs::notFoundTriesNextCandidate()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto net = std::make_shared<FakeNet>();
    net->replies.append(makeReply(404, QStringLiteral("application/json"), QByteArray("{\"error\":\"nf\"}")));
    net->replies.append(makeReply(200, QStringLiteral("application/json"), b64ImagesBody({ b64(fakePng(30)) })));
    const JobDeps deps = makeDeps(dir.filePath(QStringLiteral("lib.sqlite3")), dir.path(), net);

    QString error;
    const JobOutcome outcome = runJob(genSpec(), deps, nullptr, &error);
    QVERIFY2(outcome.status == QLatin1String("succeeded"), qPrintable(outcome.error));
    QCOMPARE(net->calls, 2);  // 404 is excluded from the stop set (SPEC 4)
}

void TstJobs::cumulativeSizeCapFailsWholeJob()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QStringList items;
    for (int i = 0; i < 4; ++i)
        items.append(b64(fakePng(1000)));

    auto net = std::make_shared<FakeNet>();
    net->replies.append(makeReply(200, QStringLiteral("application/json"), b64ImagesBody(items)));
    JobDeps deps = makeDeps(dir.filePath(QStringLiteral("lib.sqlite3")), dir.path(), net);
    deps.mediaLimits.totalBytes = 2500;  // parser aborts the whole result (SPEC 7.1)

    QString error;
    const JobOutcome outcome = runJob(genSpec(), deps, nullptr, &error);
    QCOMPARE(outcome.status, QStringLiteral("failed"));
    QVERIFY2(outcome.error.contains(QString::fromUtf8("总大小")), qPrintable(outcome.error));
    QVERIFY(outcome.assets.isEmpty());

    oic::store::Database verify(deps.databasePath);
    QVERIFY(verify.open(&error));
    const QList<oic::store::Asset> assets = verify.assetsForJob(outcome.jobId, &error);
    QVERIFY2(assets.isEmpty(), "an oversized result must leave zero assets behind");
}

void TstJobs::nonImageBase64IsRejectedByProbe()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Valid base64, but it decodes to an HTML error page, not an image. SPEC 6.4: legal
    // base64 is not proof of an image; the store probe is the gate before persisting.
    const QByteArray html = QByteArray("<!DOCTYPE html><html><body>not an image</body></html>");
    auto net = std::make_shared<FakeNet>();
    net->replies.append(makeReply(200, QStringLiteral("application/json"), b64ImagesBody({ b64(html) })));
    const JobDeps deps = makeDeps(dir.filePath(QStringLiteral("lib.sqlite3")), dir.path(), net);

    QString error;
    const JobOutcome outcome = runJob(genSpec(), deps, nullptr, &error);
    QCOMPARE(outcome.status, QStringLiteral("failed"));
    QVERIFY2(outcome.error.contains(QString::fromUtf8("不是可识别的图片")), qPrintable(outcome.error));
    QVERIFY(outcome.assets.isEmpty());
}

void TstJobs::cancelStopsJobWithNoAssets()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    CancelToken token;
    auto net = std::make_shared<FakeNet>();
    net->replies.append(makeReply(500, QStringLiteral("application/json"),
                                  QByteArray("{\"error\":{\"type\":\"upstream\"}}")));
    net->afterCall = [&token](int index) {
        if (index == 0)
            token.cancel();
    };
    const JobDeps deps = makeDeps(dir.filePath(QStringLiteral("lib.sqlite3")), dir.path(), net);

    QString error;
    const JobOutcome outcome = runJob(genSpec(), deps, &token, &error);
    QCOMPARE(outcome.status, QStringLiteral("cancelled"));
    QVERIFY2(outcome.error.contains(QString::fromUtf8("取消")), qPrintable(outcome.error));
    QVERIFY(outcome.assets.isEmpty());
    QCOMPARE(net->calls, 1);  // stopped at the next checkpoint, before candidate 2
}

void TstJobs::deadlineStopsJobBeforeSending()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto net = std::make_shared<FakeNet>();
    net->replies.append(makeReply(200, QStringLiteral("application/json"), b64ImagesBody({ b64(fakePng(30)) })));
    JobDeps deps = makeDeps(dir.filePath(QStringLiteral("lib.sqlite3")), dir.path(), net);

    const qint64 base = 1000000;
    int ticks = 0;
    deps.clock = [base, &ticks] {
        ++ticks;
        return ticks == 1 ? base : base + 901000;  // first read is start; afterwards past the 900s ceiling
    };

    QString error;
    const JobOutcome outcome = runJob(genSpec(), deps, nullptr, &error);
    QCOMPARE(outcome.status, QStringLiteral("cancelled"));
    QVERIFY2(outcome.error.contains(QString::fromUtf8("最长运行时间")), qPrintable(outcome.error));
    QCOMPARE(net->calls, 0);  // the deadline tripped before any request went out
}

void TstJobs::urlDownloadFailureKeepsRemoteLink()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto net = std::make_shared<FakeNet>();
    net->replies.append(makeReply(200, QStringLiteral("application/json"),
                                  urlBody(QStringLiteral("https://cdn.example.com/x.png"))));
    JobDeps deps = makeDeps(dir.filePath(QStringLiteral("lib.sqlite3")), dir.path(), net);
    deps.fetcher = [](const QUrl &, QByteArray *, QString *, QString *) { return false; };

    QString error;
    const JobOutcome outcome = runJob(genSpec(), deps, nullptr, &error);
    QVERIFY2(outcome.status == QLatin1String("succeeded"), qPrintable(outcome.error));
    QVERIFY(outcome.assets.isEmpty());
    const QStringList expected{ QStringLiteral("https://cdn.example.com/x.png") };
    QCOMPARE(outcome.remoteOnly, expected);  // SPEC 7.1 frozen fallback: keep the link
}

void TstJobs::urlDownloadSuccessWritesAsset()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto net = std::make_shared<FakeNet>();
    net->replies.append(makeReply(200, QStringLiteral("application/json"),
                                  urlBody(QStringLiteral("https://cdn.example.com/x.png"))));
    JobDeps deps = makeDeps(dir.filePath(QStringLiteral("lib.sqlite3")), dir.path(), net);
    deps.fetcher = [](const QUrl &, QByteArray *bytes, QString *contentType, QString *) {
        *bytes = fakePng(50);
        *contentType = QStringLiteral("image/png");
        return true;
    };

    QString error;
    const JobOutcome outcome = runJob(genSpec(), deps, nullptr, &error);
    QVERIFY2(outcome.status == QLatin1String("succeeded"), qPrintable(outcome.error));
    QCOMPARE(outcome.assets.size(), 1);
    QCOMPARE(outcome.assets.at(0).width, 8);
    QVERIFY(outcome.remoteOnly.isEmpty());
}

void TstJobs::keyNeverPersisted()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto net = std::make_shared<FakeNet>();
    net->replies.append(makeReply(200, QStringLiteral("application/json"), b64ImagesBody({ b64(fakePng(40)) })));
    const JobDeps deps = makeDeps(dir.filePath(QStringLiteral("lib.sqlite3")), dir.path(), net);

    QString error;
    const JobOutcome outcome = runJob(genSpec(), deps, nullptr, &error);
    QVERIFY2(outcome.status == QLatin1String("succeeded"), qPrintable(outcome.error));
    QVERIFY(outcome.diagnosticJson.isEmpty());

    // Strongest form: scan every byte the job wrote (db, db-wal, asset files) for the key.
    // The whole-profile scan lives in tst_credentials (SPEC 6.3); here we cover this job's
    // own outputs, which is where an executor leak would actually land.
    const QByteArray sentinel(kSentinel);
    int scanned = 0;
    QDirIterator it(dir.path(), QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();  // next() must advance before filePath() is valid
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray bytes = file.readAll();
        QVERIFY2(!bytes.contains(sentinel), qPrintable(path));
        ++scanned;
    }
    QVERIFY(scanned > 0);

    oic::store::Database verify(deps.databasePath);
    QVERIFY(verify.open(&error));
    bool found = false;
    const oic::store::Job job = verify.job(outcome.jobId, &found, &error);
    QVERIFY(found);
    QVERIFY(!job.requestJson.contains(QLatin1String(kSentinel)));
    QVERIFY(!job.resultJson.contains(QLatin1String(kSentinel)));
}

QTEST_GUILESS_MAIN(TstJobs)
#include "tst_jobs.moc"
