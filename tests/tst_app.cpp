// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
//
// oic-app foundation: Backend wiring (injectable paths + fake seams, no real network or
// Credential Manager), AssetImageProvider SPEC 6.4 decode safety, and the QML-facing
// controllers over a temp store.

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "oic/app/assetimageprovider.h"
#include "oic/app/backend.h"
#include "oic/app/historymodel.h"
#include "oic/app/jobcontroller.h"
#include "oic/app/profilecontroller.h"
#include "oic/app/settingscontroller.h"
#include "oic/jobs/executor.h"
#include "oic/net/transport.h"
#include "oic/store/paths.h"

using namespace oic::app;

namespace {

QByteArray fakePng(int size)
{
    QByteArray payload = QByteArray::fromHex("89504E470D0A1A0A0000000D494844520000000800000004");
    payload += QByteArray(qMax(0, size - payload.size()), 'p');
    return payload;
}

oic::net::Reply imageReply()
{
    QJsonObject item;
    item.insert(QStringLiteral("b64_json"), QString::fromLatin1(fakePng(40).toBase64()));
    QJsonArray data;
    data.append(item);
    QJsonObject body;
    body.insert(QStringLiteral("data"), data);

    oic::net::Reply reply;
    reply.status = 200;
    reply.contentType = QStringLiteral("application/json");
    reply.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return reply;
}

oic::store::Paths tempPaths(const QTemporaryDir &dir)
{
    oic::store::Paths paths;
    paths.root = QDir::cleanPath(dir.path());
    paths.database = paths.root + QStringLiteral("/library.sqlite3");
    paths.assets = paths.root + QStringLiteral("/assets");
    return paths;
}

oic::jobs::JobDeps stubDeps(const QString &dbPath, const QString &assetsRoot)
{
    oic::jobs::JobDeps deps;
    deps.databasePath = dbPath;
    deps.assetsRoot = assetsRoot;
    deps.assetQuotaBytes = 64LL * 1024 * 1024;
    deps.sender = [](const oic::net::Request &) { return oic::net::Reply{}; };
    deps.secrets = [](const QString &, QString *, bool *) { return QByteArray("k"); };
    return deps;
}

oic::jobs::JobDeps imageDeps(const QString &dbPath, const QString &assetsRoot)
{
    oic::jobs::JobDeps deps = stubDeps(dbPath, assetsRoot);
    deps.sender = [](const oic::net::Request &) { return imageReply(); };
    return deps;
}

oic::store::Job makeJob(const QString &id, qint64 createdAt)
{
    oic::store::Job job;
    job.id = id;
    job.createdAt = createdAt;
    job.updatedAt = createdAt;
    job.mode = QStringLiteral("generate");
    job.protocol = QStringLiteral("openai");
    job.profile = QStringLiteral("p");
    job.model = QStringLiteral("gpt-image-2");
    job.prompt = QStringLiteral("a cat");
    job.status = QStringLiteral("succeeded");
    return job;
}

bool writePng(const QString &path, int width, int height)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::red);
    return image.save(path, "PNG");
}

}  // namespace

class TstApp : public QObject
{
    Q_OBJECT

private slots:
    void backendInitWithFakeDeps();
    void providerDecodesRealImage();
    void providerHonorsAllocationLimit();
    void providerMissingAndTraversalGivePlaceholder();
    void profileControllerCrudDerivesCredentialTarget();
    void historyModelRolesPinAndDelete();
    void jobControllerGenerateFinishes();
    void settingsControllerRetentionPrunes();
};

void TstApp::backendInitWithFakeDeps()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const oic::store::Paths paths = tempPaths(dir);

    Backend backend;
    QString error;
    QVERIFY2(backend.initWithPaths(paths, stubDeps(paths.database, paths.assets), &error), qPrintable(error));
    QVERIFY(backend.isReady());
    QVERIFY(backend.database() != nullptr);
    QVERIFY(backend.assetStore() != nullptr);
    QVERIFY(backend.jobs() != nullptr);
    QVERIFY(QFileInfo::exists(paths.database));
    QVERIFY(QDir(paths.assets).exists());
}

void TstApp::providerDecodesRealImage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString rel = QStringLiteral("j1/real.png");
    QVERIFY(writePng(QDir(dir.path()).filePath(rel), 8, 4));

    AssetImageProvider provider(dir.path());
    QSize size;
    const QImage image = provider.requestImage(rel, &size, QSize());
    QVERIFY2(image.size() == QSize(8, 4),
             qPrintable(QStringLiteral("got %1x%2").arg(image.width()).arg(image.height())));
}

void TstApp::providerHonorsAllocationLimit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString rel = QStringLiteral("j1/big.png");
    QVERIFY(writePng(QDir(dir.path()).filePath(rel), 1024, 1024));

    // A 1 MiB cap must reject a 1024x1024 RGBA (4 MiB) decode -- proving the SPEC 6.4 limit
    // is live. setAllocationLimit(0) would DISABLE this exact check (the spec's original bug).
    AssetImageProvider tight(dir.path(), /*allocationLimitMiB*/ 1);
    QSize size;
    const QImage rejected = tight.requestImage(rel, &size, QSize());
    QVERIFY2(rejected.size() == QSize(256, 256),
             qPrintable(QStringLiteral("expected placeholder, got %1x%2").arg(rejected.width()).arg(rejected.height())));

    AssetImageProvider roomy(dir.path());
    QSize size2;
    const QImage decoded = roomy.requestImage(rel, &size2, QSize());
    QVERIFY2(decoded.size() == QSize(1024, 1024),
             qPrintable(QStringLiteral("got %1x%2").arg(decoded.width()).arg(decoded.height())));
}

void TstApp::providerMissingAndTraversalGivePlaceholder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AssetImageProvider provider(dir.path());
    QSize size;

    const QImage missing = provider.requestImage(QStringLiteral("j1/nope.png"), &size, QSize());
    QVERIFY(missing.size() == QSize(256, 256));

    const QImage escape = provider.requestImage(QStringLiteral("../../etc/passwd"), &size, QSize());
    QVERIFY2(escape.size() == QSize(256, 256), "a path-escape id must yield the placeholder, not read outside the root");
}

void TstApp::profileControllerCrudDerivesCredentialTarget()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const oic::store::Paths paths = tempPaths(dir);
    Backend backend;
    QString error;
    QVERIFY2(backend.initWithPaths(paths, stubDeps(paths.database, paths.assets), &error), qPrintable(error));

    ProfileController profiles(&backend);
    QVERIFY(profiles.saveProfile(QStringLiteral("p1"), QStringLiteral("https://api.example.com"),
                                 QStringLiteral("openai"), QStringLiteral("gpt-image-2"), 300, &error));
    QVERIFY2(profiles.names().contains(QStringLiteral("p1")), qPrintable(error));

    // credential_target is derived from the name (SPEC 6.3); the key is never stored here.
    QCOMPARE(profiles.profile(QStringLiteral("p1")).credentialTarget,
             QStringLiteral("image-client/profile/p1"));

    // Same name updates rather than duplicating.
    QVERIFY(profiles.saveProfile(QStringLiteral("p1"), QStringLiteral("https://other.example.com/v1"),
                                 QStringLiteral("openai"), QStringLiteral("gpt-image-2"), 300, &error));
    QCOMPARE(profiles.count(), 1);

    QVERIFY(profiles.removeProfile(QStringLiteral("p1"), &error));
    QCOMPARE(profiles.count(), 0);
}

void TstApp::historyModelRolesPinAndDelete()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const oic::store::Paths paths = tempPaths(dir);
    Backend backend;
    QString error;
    QVERIFY2(backend.initWithPaths(paths, stubDeps(paths.database, paths.assets), &error), qPrintable(error));

    QVERIFY(backend.database()->createJob(makeJob(QStringLiteral("j1"), 10), &error));
    QVERIFY(backend.database()->createJob(makeJob(QStringLiteral("j2"), 20), &error));
    oic::store::Asset asset;
    asset.id = QStringLiteral("a1");
    asset.jobId = QStringLiteral("j1");
    asset.ordinal = 0;
    asset.filename = QStringLiteral("a1.png");
    asset.relPath = QStringLiteral("j1/a1.png");
    asset.bytes = 100;
    asset.mime = QStringLiteral("image/png");
    QVERIFY(backend.database()->addAsset(asset, &error));

    HistoryModel model(&backend);
    QCOMPARE(model.count(), 2);  // newest first: j2 then j1
    QCOMPARE(model.data(model.index(0, 0), HistoryModel::IdRole).toString(), QStringLiteral("j2"));
    QCOMPARE(model.data(model.index(0, 0), HistoryModel::CreatedAtRole).toLongLong(), 20LL);
    QCOMPARE(model.data(model.index(1, 0), HistoryModel::ThumbnailRole).toString(), QStringLiteral("j1/a1.png"));
    QCOMPARE(model.data(model.index(0, 0), HistoryModel::ThumbnailRole).toString(), QString());  // j2 has no asset

    QVERIFY(model.pin(1, true));  // pin j1
    QCOMPARE(model.data(model.index(1, 0), HistoryModel::PinnedRole).toBool(), true);

    QVERIFY(model.removeAt(0));  // soft-delete j2
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.data(model.index(0, 0), HistoryModel::IdRole).toString(), QStringLiteral("j1"));
}

void TstApp::jobControllerGenerateFinishes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const oic::store::Paths paths = tempPaths(dir);
    Backend backend;
    QString error;
    QVERIFY2(backend.initWithPaths(paths, imageDeps(paths.database, paths.assets), &error), qPrintable(error));

    ProfileController profiles(&backend);
    QVERIFY(profiles.saveProfile(QStringLiteral("p1"), QStringLiteral("https://api.example.com"),
                                 QStringLiteral("openai"), QStringLiteral("gpt-image-2"), 300, &error));

    JobController jobs(&backend);
    QSignalSpy finished(&jobs, &JobController::jobFinished);
    QVERIFY(finished.isValid());

    QString submitError;
    const QString id = jobs.generate(QStringLiteral("p1"), QStringLiteral("gpt-image-2"), QStringLiteral("a cat"),
                                     QStringLiteral("1024x1024"), 1, QStringLiteral("openai"), &submitError);
    QVERIFY2(!id.isEmpty(), qPrintable(submitError));

    // Drain queued jobFinished signals until the one for our id arrives.
    QString status;
    for (int i = 0; i < 100 && status.isEmpty(); ++i) {
        QTest::qWait(20);
        for (const QList<QVariant> &args : finished) {
            if (args.at(0).toString() == id) {
                status = args.at(1).toString();
                break;
            }
        }
    }
    QVERIFY2(status == QLatin1String("succeeded"),
             qPrintable(QStringLiteral("status=%1 error=%2").arg(status, submitError)));
}

void TstApp::settingsControllerRetentionPrunes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const oic::store::Paths paths = tempPaths(dir);
    Backend backend;
    QString error;
    QVERIFY2(backend.initWithPaths(paths, stubDeps(paths.database, paths.assets), &error), qPrintable(error));

    QVERIFY(backend.database()->createJob(makeJob(QStringLiteral("j1"), 10), &error));
    QVERIFY(backend.database()->createJob(makeJob(QStringLiteral("j2"), 20), &error));
    QVERIFY(backend.database()->createJob(makeJob(QStringLiteral("j3"), 30), &error));

    SettingsController settings(&backend);
    settings.setRetentionRows(1);  // keep 1, drop the 2 oldest unpinned
    QCOMPARE(settings.applyRetention(), 2);
    QCOMPARE(backend.database()->listJobs(10, 0, false, &error).size(), 1);
}

QTEST_GUILESS_MAIN(TstApp)
#include "tst_app.moc"
