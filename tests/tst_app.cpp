// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
//
// oic-app foundation: Backend wiring (injectable paths + fake seams, no real network or
// Credential Manager), AssetImageProvider SPEC 6.4 decode safety, and the QML-facing
// controllers over a temp store.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include "oic/app/assetimageprovider.h"
#include "oic/app/backend.h"
#include "oic/app/historymodel.h"
#include "oic/app/jobcontroller.h"
#include "oic/app/lightboxcontroller.h"
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
    void providerHonorsRequestedSize();
    void profileControllerCrudDerivesCredentialTarget();
    void historyModelRolesPinAndDelete();
    void jobControllerGenerateFinishes();
    void jobControllerEditPersistsReferenceAssets();
    void historyModelPagination();
    void historyModelRoleNamesAvoidQmlReserved();
    void retryResubmitsGenerateJob();
    void retryRestoresEditReferences();
    void addReferenceRejectsNonImage();
    void generateEditWithoutReferenceFails();
    void jobControllerWrapperSurfacesError();
    void profileControllerInvokables();
    void profileControllerListAndDetail();
    void settingsControllerPersistence();
    void settingsControllerRetentionPrunes();
    void lightboxControllerStateMachine();
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

void TstApp::providerHonorsRequestedSize()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString rel = QStringLiteral("j1/wide.png");
    QVERIFY(writePng(QDir(dir.path()).filePath(rel), 200, 100));

    // The provider honours requestedSize by scaling here, and reports the size of what it
    // returns -- self-consistent (200x100 at 2:1 into 50x50 -> 50x25), so QML never gets a
    // full-size bitmap for a thumbnail.
    AssetImageProvider provider(dir.path());
    QSize size;
    const QImage image = provider.requestImage(rel, &size, QSize(50, 50));
    QVERIFY2(image.size() == QSize(50, 25),
             qPrintable(QStringLiteral("got %1x%2").arg(image.width()).arg(image.height())));
    QVERIFY(size == QSize(50, 25));
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

void TstApp::jobControllerEditPersistsReferenceAssets()
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

    // A real reference image on disk; the controller reads + probes it into a staged data URL.
    const QString refFile = QDir(dir.path()).filePath(QStringLiteral("input/ref.png"));
    QVERIFY(writePng(refFile, 40, 20));

    JobController jobs(&backend);
    const QString source = jobs.addReferencePath(QUrl::fromLocalFile(refFile).toString());
    QVERIFY2(!source.isEmpty(), qPrintable(jobs.lastError()));
    QCOMPARE(jobs.referenceCount(), 1);
    QCOMPARE(jobs.referenceSources(), QStringList({ source }));  // the UI re-syncs its list from here

    QSignalSpy finished(&jobs, &JobController::jobFinished);
    QVERIFY(finished.isValid());
    const QString id = jobs.generateEdit(QStringLiteral("p1"), QStringLiteral("gpt-image-2"),
                                         QStringLiteral("make it a painting"), QStringLiteral("1024x1024"), 1,
                                         QStringLiteral("openai"));
    QVERIFY2(!id.isEmpty(), qPrintable(jobs.lastError()));
    QCOMPARE(jobs.referenceCount(), 0);  // staged references are consumed on submit

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
    QVERIFY2(status == QLatin1String("succeeded"), qPrintable(QStringLiteral("status=%1").arg(status)));

    int results = 0;
    int references = 0;
    for (const oic::store::Asset &asset : backend.database()->assetsForJob(id, &error)) {
        if (asset.role == QLatin1String("result")) {
            ++results;
        } else if (asset.role == QLatin1String("reference")) {
            ++references;
            QCOMPARE(asset.mime, QStringLiteral("image/png"));
            QCOMPARE(asset.width, 40);  // the on-disk reference is probed, not guessed
            QCOMPARE(asset.height, 20);
        }
    }
    QCOMPARE(results, 1);
    QCOMPARE(references, 1);
}

void TstApp::addReferenceRejectsNonImage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Backend backend;  // addReferencePath only touches the filesystem + probe, so no deps needed

    const QString notePath = QDir(dir.path()).filePath(QStringLiteral("note.txt"));
    QFile note(notePath);
    QVERIFY(note.open(QIODevice::WriteOnly));
    note.write("this is not an image");
    note.close();

    JobController jobs(&backend);
    QVERIFY2(jobs.addReferencePath(notePath).isEmpty(), "a non-image must be rejected");
    QVERIFY(!jobs.lastError().isEmpty());
    QCOMPARE(jobs.referenceCount(), 0);

    // A missing file is likewise rejected, never staged as a broken reference.
    const QString ghost = QDir(dir.path()).filePath(QStringLiteral("ghost.png"));
    QVERIFY2(jobs.addReferencePath(ghost).isEmpty(), "a missing file must be rejected");
    QVERIFY(!jobs.lastError().isEmpty());
}

void TstApp::generateEditWithoutReferenceFails()
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

    JobController jobs(&backend);
    const QString id = jobs.generateEdit(QStringLiteral("p1"), QStringLiteral("gpt-image-2"),
                                         QStringLiteral("no refs"), QStringLiteral("1024x1024"), 1,
                                         QStringLiteral("openai"));
    QVERIFY2(id.isEmpty(), "an edit submit with no reference must fail");
    QVERIFY2(jobs.lastError().contains(QString::fromUtf8("参考图")), qPrintable(jobs.lastError()));
}

void TstApp::historyModelPagination()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const oic::store::Paths paths = tempPaths(dir);
    Backend backend;
    QString error;
    QVERIFY2(backend.initWithPaths(paths, stubDeps(paths.database, paths.assets), &error), qPrintable(error));

    for (int i = 1; i <= 6; ++i)
        QVERIFY(backend.database()->createJob(makeJob(QStringLiteral("j%1").arg(i), i * 10), &error));

    HistoryModel model(&backend);
    model.refresh(4);  // page size 4
    QCOMPARE(model.count(), 4);
    QCOMPARE(model.hasMore(), true);
    model.loadMore();
    QCOMPARE(model.count(), 6);
    QCOMPARE(model.hasMore(), false);  // second page was short -> no more
    // These jobs have no result assets, so the lightbox list for a row is empty.
    QVERIFY(model.resultRelPaths(0).isEmpty());
}

void TstApp::historyModelRoleNamesAvoidQmlReserved()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const oic::store::Paths paths = tempPaths(dir);
    Backend backend;
    QString error;
    QVERIFY2(backend.initWithPaths(paths, stubDeps(paths.database, paths.assets), &error), qPrintable(error));

    HistoryModel model(&backend);
    const QList<QByteArray> names = model.roleNames().values();
    // "model" and "index" are reserved by QML's delegate scope: using either poisons the whole
    // role map so every model.<name> resolves undefined (the live bug: black thumbnails + retry
    // reading an empty jobId). Guard the invariant so it can never come back.
    QVERIFY2(!names.contains(QByteArrayLiteral("model")), "role name 'model' is reserved by QML");
    QVERIFY2(!names.contains(QByteArrayLiteral("index")), "role name 'index' is reserved by QML");
    QVERIFY(names.contains(QByteArrayLiteral("status")));
    QVERIFY(names.contains(QByteArrayLiteral("jobId")));
    QVERIFY(names.contains(QByteArrayLiteral("thumbnail")));
}

void TstApp::retryResubmitsGenerateJob()
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

    oic::store::Job failed;
    failed.id = QStringLiteral("src1");
    failed.createdAt = 10;
    failed.updatedAt = 10;
    failed.mode = QStringLiteral("generate");
    failed.protocol = QStringLiteral("openai");
    failed.profile = QStringLiteral("p1");
    failed.model = QStringLiteral("gpt-image-2");
    failed.prompt = QStringLiteral("a cat");
    failed.size = QStringLiteral("1024x1024");
    failed.n = 1;
    failed.status = QStringLiteral("failed");
    failed.error = QStringLiteral("上游超时");
    QVERIFY(backend.database()->createJob(failed, &error));

    JobController jobs(&backend);
    QSignalSpy finished(&jobs, &JobController::jobFinished);
    QVERIFY(finished.isValid());
    const QString id = jobs.retryFromHistory(QStringLiteral("src1"));
    QVERIFY2(!id.isEmpty(), qPrintable(jobs.lastError()));

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
    QVERIFY2(status == QLatin1String("succeeded"), qPrintable(QStringLiteral("status=%1").arg(status)));
}

void TstApp::retryRestoresEditReferences()
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

    oic::store::Job edit;
    edit.id = QStringLiteral("ed1");
    edit.createdAt = 10;
    edit.updatedAt = 10;
    edit.mode = QStringLiteral("edit");
    edit.protocol = QStringLiteral("openai");
    edit.profile = QStringLiteral("p1");
    edit.model = QStringLiteral("gpt-image-2");
    edit.prompt = QStringLiteral("make it a watercolor");
    edit.size = QStringLiteral("1024x1024");
    edit.n = 1;
    edit.status = QStringLiteral("failed");
    QVERIFY(backend.database()->createJob(edit, &error));

    // A reference asset persisted under the original edit job (as GUI-C's runJob does).
    const QByteArray png = fakePng(60);
    const oic::store::AssetWriteResult wr = backend.assetStore()->write(QStringLiteral("ed1"), QStringLiteral("ref-1"),
                                                                        QStringLiteral("image/png"), png);
    QVERIFY2(wr.ok(), qPrintable(wr.error));
    oic::store::Asset refAsset;
    refAsset.id = QStringLiteral("ref-1");
    refAsset.jobId = QStringLiteral("ed1");
    refAsset.filename = QStringLiteral("ref.png");
    refAsset.relPath = wr.relPath;
    refAsset.bytes = png.size();
    refAsset.mime = QStringLiteral("image/png");
    refAsset.role = QStringLiteral("reference");
    refAsset.createdAt = 10;
    QVERIFY(backend.database()->addAsset(refAsset, &error));

    JobController jobs(&backend);
    QSignalSpy finished(&jobs, &JobController::jobFinished);
    QVERIFY(finished.isValid());
    const QString id = jobs.retryFromHistory(QStringLiteral("ed1"));
    QVERIFY2(!id.isEmpty(), qPrintable(jobs.lastError()));

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
    QVERIFY2(status == QLatin1String("succeeded"), qPrintable(QStringLiteral("status=%1").arg(status)));

    // The restored reference flowed through runJob and was re-persisted on the new job.
    bool sawReference = false;
    bool sawResult = false;
    for (const oic::store::Asset &asset : backend.database()->assetsForJob(id, &error)) {
        if (asset.role == QLatin1String("reference"))
            sawReference = true;
        if (asset.role == QLatin1String("result"))
            sawResult = true;
    }
    QVERIFY2(sawReference, "retry of an edit job must carry its reference through");
    QVERIFY2(sawResult, "retried job should have produced a result");
}

void TstApp::profileControllerListAndDetail()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const oic::store::Paths paths = tempPaths(dir);
    Backend backend;
    QString error;
    QVERIFY2(backend.initWithPaths(paths, stubDeps(paths.database, paths.assets), &error), qPrintable(error));

    ProfileController profiles(&backend);
    QVERIFY(profiles.saveProfile(QStringLiteral("svc"), QStringLiteral("https://gw.example.com/v1"),
                                 QStringLiteral("openai"), QStringLiteral("gpt-image-2"), 300, &error));

    QVERIFY(profiles.protocols().contains(QStringLiteral("openai")));
    QVERIFY(profiles.protocols().contains(QStringLiteral("grok")));
    QVERIFY(profiles.protocols().contains(QStringLiteral("gemini")));

    const QVariantMap detail = profiles.profileDetail(QStringLiteral("svc"));
    QCOMPARE(detail.value(QStringLiteral("name")).toString(), QStringLiteral("svc"));
    QCOMPARE(detail.value(QStringLiteral("baseUrl")).toString(), QStringLiteral("https://gw.example.com/v1"));
    QCOMPARE(detail.value(QStringLiteral("imageModel")).toString(), QStringLiteral("gpt-image-2"));
    QCOMPARE(detail.value(QStringLiteral("timeoutSeconds")).toInt(), 300);
    // No key written -> hasCredential false (a read, not a Credential Manager write).
    QCOMPARE(detail.value(QStringLiteral("hasCredential")).toBool(), false);

    const QVariantList list = profiles.profilesList();
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("svc"));

    QVERIFY(profiles.removeProfileByName(QStringLiteral("svc")));
    QVERIFY(profiles.profilesList().isEmpty());
}

void TstApp::settingsControllerPersistence()
{
    // Settings are persisted via QSettings; drive a real ini scope (not the registry) so the
    // round-trip is deterministic and isolated.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("oic-test"));
    QCoreApplication::setApplicationName(QStringLiteral("oic-test"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    // dir is a fresh QTemporaryDir -> the scoped ini starts empty.

    {
        SettingsController settings(nullptr);  // theme/retention/trustedHosts need no backend
        QCOMPARE(settings.themeName(), QStringLiteral("dark"));  // default
        settings.setThemeName(QStringLiteral("light"));
        settings.setRetentionRows(42);
        settings.setRetentionBytes(123456);
        settings.addTrustedHost(QStringLiteral("  GW.Internal  "));
        settings.addTrustedHost(QStringLiteral("gw.internal"));  // deduped, lower-cased
    }

    {
        SettingsController reopened(nullptr);
        QCOMPARE(reopened.themeName(), QStringLiteral("light"));
        QCOMPARE(reopened.retentionRows(), 42);
        QCOMPARE(reopened.retentionBytes(), qint64(123456));
        QCOMPARE(reopened.trustedHosts(), QStringList({QStringLiteral("gw.internal")}));
        QCOMPARE(SettingsController::storedTrustedHosts(), QStringList({QStringLiteral("gw.internal")}));
    }
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

void TstApp::jobControllerWrapperSurfacesError()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const oic::store::Paths paths = tempPaths(dir);
    Backend backend;
    QString error;
    QVERIFY2(backend.initWithPaths(paths, stubDeps(paths.database, paths.assets), &error), qPrintable(error));

    JobController jobs(&backend);
    // No profile saved -> the QML wrapper returns empty AND surfaces why via lastError(),
    // which a plain return value cannot convey.
    const QString id = jobs.generateJob(QStringLiteral("missing"), QStringLiteral("gpt-image-2"),
                                        QStringLiteral("a cat"), QStringLiteral("1024x1024"), 1);
    QVERIFY(id.isEmpty());
    QVERIFY(!jobs.lastError().isEmpty());
}

void TstApp::profileControllerInvokables()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const oic::store::Paths paths = tempPaths(dir);
    Backend backend;
    QString error;
    QVERIFY2(backend.initWithPaths(paths, stubDeps(paths.database, paths.assets), &error), qPrintable(error));

    ProfileController profiles(&backend);
    QVERIFY(profiles.addProfile(QStringLiteral("ok"), QStringLiteral("https://api.example.com"),
                                 QStringLiteral("openai"), QStringLiteral("gpt-image-2"), 300));
    QVERIFY(profiles.profileNames().contains(QStringLiteral("ok")));

    // An invalid profile name (leading '-') is rejected by targetNameFor (SPEC 6.3); the
    // wrapper must set lastError rather than silently fail.
    QVERIFY(!profiles.addProfile(QStringLiteral("-bad"), QStringLiteral("https://api.example.com"),
                                  QStringLiteral("openai"), QStringLiteral(""), 0));
    QVERIFY(!profiles.lastError().isEmpty());
}

void TstApp::lightboxControllerStateMachine()
{
    oic::app::LightboxController lb;
    QSignalSpy changed(&lb, &oic::app::LightboxController::changed);
    QVERIFY(changed.isValid());

    // Empty model: openAt refuses rather than showing a phantom index.
    lb.openAt(0);
    QVERIFY(!lb.isOpen());

    lb.setModel({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    QCOMPARE(lb.count(), 3);
    QCOMPARE(lb.canCompare(), true);

    lb.openAt(1);
    QVERIFY(lb.isOpen());
    QCOMPARE(lb.currentIndex(), 1);
    QCOMPARE(lb.zoom(), 1.0);
    QCOMPARE(lb.isCompare(), false);

    // Navigation wraps in both directions (SPEC 9.1 keyboard history nav).
    lb.next();
    QCOMPARE(lb.currentIndex(), 2);
    lb.next();
    QCOMPARE(lb.currentIndex(), 0);
    lb.prev();
    QCOMPARE(lb.currentIndex(), 2);

    // openAt clamps out-of-range indices.
    lb.openAt(99);
    QCOMPARE(lb.currentIndex(), 2);
    lb.openAt(-5);
    QCOMPARE(lb.currentIndex(), 0);

    // Zoom clamps to [1, 8] and a step resets it to 1.
    for (int i = 0; i < 40; ++i)
        lb.zoomIn();
    QVERIFY(lb.zoom() <= 8.0 + 1e-9);
    QVERIFY(lb.zoom() > 1.0);
    lb.resetZoom();
    QCOMPARE(lb.zoom(), 1.0);
    for (int i = 0; i < 40; ++i)
        lb.zoomOut();
    QCOMPARE(lb.zoom(), 1.0);  // never below 1x
    lb.zoomIn();
    lb.step(1);
    QCOMPARE(lb.zoom(), 1.0);  // stepping a new image drops zoom

    // Compare needs >= 2 images; toggling pairs currentIndex with the next one.
    lb.openAt(0);
    lb.toggleCompare();
    QVERIFY(lb.isCompare());
    QCOMPARE(lb.compareIndex(), 1);
    lb.toggleCompare();
    QVERIFY(!lb.isCompare());
    QCOMPARE(lb.compareIndex(), -1);

    // A single-image set cannot compare.
    lb.setModel({QStringLiteral("only")});
    lb.openAt(0);
    QCOMPARE(lb.canCompare(), false);
    lb.toggleCompare();
    QVERIFY(!lb.isCompare());

    // Close resets transient state.
    lb.setModel({QStringLiteral("a"), QStringLiteral("b")});
    lb.openAt(0);
    lb.zoomIn();
    lb.close();
    QVERIFY(!lb.isOpen());
    QCOMPARE(lb.zoom(), 1.0);
    QVERIFY(!lb.isCompare());

    // Every mutation notified (the view binds to `changed`).
    QVERIFY(changed.count() >= 20);
}

QTEST_GUILESS_MAIN(TstApp)
#include "tst_app.moc"
