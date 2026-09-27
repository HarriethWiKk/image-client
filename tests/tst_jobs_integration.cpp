// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
//
// jobs <-> real net::Transport integration over loopback. A fake Sender would bypass the
// SSRF gate, Host pinning and candidate retry the transport exists for (review point #9),
// so this drives runJob() through a real Transport against a 127.0.0.1 listener and checks
// what actually hit the wire.

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "oic/jobs/executor.h"
#include "oic/net/transport.h"

using namespace oic::jobs;

namespace {

const char *kKey = "integration-test-key";

QByteArray headerOf(const QByteArray &raw, const char *name)
{
    const QList<QByteArray> lines = raw.split('\n');
    const QByteArray needle = QByteArray(name).toLower() + ":";
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.toLower().startsWith(needle))
            return trimmed.mid(needle.size()).trimmed();
    }
    return QByteArray();
}

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

// Minimal loopback HTTP server: queue one raw response per connection, record each request.
class FakeUpstream : public QObject {
public:
    bool listen() { return server_.listen(QHostAddress::LocalHost, 0); }
    int port() const { return static_cast<int>(server_.serverPort()); }
    QList<QByteArray> requests() const { return requests_; }

    void respond(int status, const QByteArray &body, const QList<QPair<QByteArray, QByteArray>> &headers = {})
    {
        QByteArray raw = "HTTP/1.1 " + QByteArray::number(status) + " Status\r\nContent-Length: "
                         + QByteArray::number(body.size());
        for (const auto &header : headers)
            raw += "\r\n" + header.first + ": " + header.second;
        raw += "\r\n\r\n";
        raw += body;
        queue_.append(raw);
    }

    void start()
    {
        QObject::connect(&server_, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *sock = server_.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(sock, &QTcpSocket::readyRead, this, [this, sock, buffer] {
                buffer->append(sock->readAll());
                const int headerEnd = buffer->indexOf("\r\n\r\n");
                if (headerEnd < 0)
                    return;
                const qint64 declared = headerOf(buffer->left(headerEnd), "content-length").toLongLong();
                if (buffer->size() < headerEnd + 4 + static_cast<int>(declared))
                    return;  // request body still arriving
                requests_.append(*buffer);
                if (queue_.isEmpty()) {
                    sock->disconnectFromHost();
                    return;
                }
                sock->write(queue_.takeFirst());
                sock->flush();
                sock->disconnectFromHost();
            });
            QObject::connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
        });
    }

private:
    QTcpServer server_;
    QList<QByteArray> queue_;
    QList<QByteArray> requests_;
};

oic::net::Resolver loopbackResolver()
{
    return [](const QString &) { return QList<QHostAddress>{ QHostAddress(QHostAddress::LocalHost) }; };
}

// A real Transport (SSRF gate + pinning + HTTP/1.1 all live), with only the credential
// read faked so the test never touches the Windows Credential Manager.
JobDeps makeRealDeps(const QString &dbPath, const QString &assetsRoot)
{
    oic::net::Transport::Settings settings;
    settings.trustedHosts = { QStringLiteral("pin.example.test") };
    settings.resolver = loopbackResolver();
    auto transport = std::make_shared<oic::net::Transport>(std::move(settings));

    JobDeps deps;
    deps.databasePath = dbPath;
    deps.assetsRoot = assetsRoot;
    deps.assetQuotaBytes = 64LL * 1024 * 1024;
    deps.sender = [transport](const oic::net::Request &request) { return transport->send(request); };
    deps.secrets = [](const QString &, QString *, bool *) { return QByteArray(kKey); };
    return deps;
}

JobSpec loopbackSpec(int port)
{
    JobSpec spec;
    spec.profileName = QStringLiteral("it");
    spec.baseUrl = QStringLiteral("http://pin.example.test:%1").arg(port);
    spec.protocolHint = QStringLiteral("openai");
    spec.credentialTarget = QStringLiteral("image-client/profile/it");
    spec.image.model = QStringLiteral("gpt-image-2");
    spec.image.prompt = QStringLiteral("a cat");
    spec.image.size = QStringLiteral("1024x1024");
    spec.image.n = 1;
    return spec;
}

}  // namespace

class TstJobsIntegration : public QObject
{
    Q_OBJECT

private:
    FakeUpstream upstream_;

private slots:
    void initTestCase()
    {
        QVERIFY(upstream_.listen());
        upstream_.start();
    }

    void happyPathPinsHostAndStoresAsset();
    void retriesAcrossCandidatesOverRealSockets();
};

void TstJobsIntegration::happyPathPinsHostAndStoresAsset()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const qsizetype before = upstream_.requests().size();
    upstream_.respond(200, imageBody(), { { "Content-Type", "application/json" } });
    const JobDeps deps = makeRealDeps(dir.filePath(QStringLiteral("lib.sqlite3")), dir.path());

    QString error;
    const JobOutcome outcome = runJob(loopbackSpec(upstream_.port()), deps, nullptr, &error);
    QVERIFY2(outcome.status == QLatin1String("succeeded"), qPrintable(outcome.error));
    QCOMPARE(outcome.assets.size(), 1);
    // The bytes that came over a real socket were probed and stored with true geometry.
    QCOMPARE(outcome.assets.at(0).width, 8);
    QCOMPARE(outcome.assets.at(0).height, 4);
    QVERIFY(QFile::exists(QDir(deps.assetsRoot).filePath(outcome.assets.at(0).relPath)));

    QCOMPARE(upstream_.requests().size() - before, 1);
    const QByteArray request = upstream_.requests().at(before);
    QVERIFY2(request.startsWith("POST /v1/images/generations"), request.left(40).constData());
    // Host pinned to the original authority, not the loopback IP the transport dialed.
    QCOMPARE(headerOf(request, "host"),
             QByteArray("pin.example.test:") + QByteArray::number(upstream_.port()));
    QVERIFY(headerOf(request, "authorization").contains(kKey));
}

void TstJobsIntegration::retriesAcrossCandidatesOverRealSockets()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const qsizetype before = upstream_.requests().size();
    // First candidate (/v1/...) gets a retryable 502; second (bare) succeeds.
    upstream_.respond(502, QByteArray("{\"error\":{\"type\":\"upstream_error\"}}"),
                      { { "Content-Type", "application/json" } });
    upstream_.respond(200, imageBody(), { { "Content-Type", "application/json" } });
    const JobDeps deps = makeRealDeps(dir.filePath(QStringLiteral("lib.sqlite3")), dir.path());

    QString error;
    const JobOutcome outcome = runJob(loopbackSpec(upstream_.port()), deps, nullptr, &error);
    QVERIFY2(outcome.status == QLatin1String("succeeded"), qPrintable(outcome.error));
    QCOMPARE(outcome.assets.size(), 1);

    QCOMPARE(upstream_.requests().size() - before, 2);
    QVERIFY2(upstream_.requests().at(before).startsWith("POST /v1/images/generations"),
             upstream_.requests().at(before).left(40).constData());
    QVERIFY2(upstream_.requests().at(before + 1).startsWith("POST /images/generations"),
             upstream_.requests().at(before + 1).left(40).constData());
}

QTEST_GUILESS_MAIN(TstJobsIntegration)
#include "tst_jobs_integration.moc"
