// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
//
// Transport tests run against a listener on 127.0.0.1, so every case has to put
// the host name on the trusted list: that is the §6.3 "trusted local gateway"
// path, and it is what lets the pinning code run for real instead of being
// mocked out.

#include <QHostAddress>
#include <QList>
#include <QPair>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <memory>

#include "oic/core/limits.h"
#include "oic/net/transport.h"

namespace {

QByteArray headerOf(const QByteArray &raw, const char *name)
{
    const QList<QByteArray> lines = raw.split('\n');
    const QByteArray needle = QByteArray(name).toLower() + ":";
    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.toLower().startsWith(needle)) {
            return trimmed.mid(needle.size()).trimmed();
        }
    }
    return QByteArray();
}

class FakeUpstream : public QObject {
public:
    bool listen() { return server_.listen(QHostAddress::LocalHost, 0); }
    quint16 port() const { return server_.serverPort(); }
    QList<QByteArray> requests() const { return requests_; }

    void respond(int status, const QByteArray &body, const QList<QPair<QByteArray, QByteArray>> &headers = {})
    {
        QByteArray reason;
        switch (status) {
        case 200: reason = "OK"; break;
        case 302: reason = "Found"; break;
        case 303: reason = "See Other"; break;
        case 400: reason = "Bad Request"; break;
        case 502: reason = "Bad Gateway"; break;
        default: reason = "Status"; break;
        }
        QByteArray raw;
        raw += "HTTP/1.1 ";
        raw += QByteArray::number(status);
        raw += ' ';
        raw += reason;
        raw += "\r\nContent-Length: ";
        raw += QByteArray::number(body.size());
        for (const auto &header : headers) {
            raw += "\r\n";
            raw += header.first;
            raw += ": ";
            raw += header.second;
        }
        raw += "\r\n\r\n";
        raw += body;
        queue_.append(raw);
    }

    // No Content-Length and no close-delimited framing beyond the bytes given:
    // this is the shape that forces the accumulator, not the header, to bound it.
    void respondRaw(const QByteArray &raw) { queue_.append(raw); }

    void start()
    {
        QObject::connect(&server_, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *sock = server_.nextPendingConnection();
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(sock, &QTcpSocket::readyRead, this, [this, sock, buffer] {
                buffer->append(sock->readAll());
                const int headerEnd = buffer->indexOf("\r\n\r\n");
                if (headerEnd < 0) {
                    return;
                }
                const qint64 declared = headerOf(buffer->left(headerEnd), "content-length").toLongLong();
                if (buffer->size() < headerEnd + 4 + static_cast<int>(declared)) {
                    return;  // request body still arriving
                }
                requests_.append(*buffer);
                if (queue_.isEmpty()) {
                    sock->disconnectFromHost();
                    return;
                }
                const QByteArray raw = queue_.takeFirst();
                sock->write(raw);
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

oic::net::Resolver loopbackFor(QStringList *seen)
{
    return [seen](const QString &hostname) {
        if (seen != nullptr) {
            seen->append(hostname);
        }
        return QList<QHostAddress>{ QHostAddress(QHostAddress::LocalHost) };
    };
}

// Answers by hanging up. Deliberately not a refused port: Winsock retries a
// refused loopback connect for ~4 s, which sits one rounding error away from the
// 5 s minimum request timeout and would make the case flake in CI.
class SilentServer : public QObject {
public:
    bool listen() { return server_.listen(QHostAddress::LocalHost, 0); }
    quint16 port() const { return server_.serverPort(); }

    void start()
    {
        QObject::connect(&server_, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *sock = server_.nextPendingConnection();
            sock->abort();
            sock->deleteLater();
        });
    }

private:
    QTcpServer server_;
};

}  // namespace

class TstTransport : public QObject
{
    Q_OBJECT

private:
    FakeUpstream upstream_;
    SilentServer silent_;

    oic::net::Transport makeTransport(const QStringList &trustedHosts, QStringList *lookups = nullptr) const
    {
        oic::net::Transport::Settings settings;
        settings.trustedHosts = trustedHosts;
        settings.resolver = loopbackFor(lookups);
        return oic::net::Transport(std::move(settings));
    }

    oic::net::Request makeRequest(const QString &path) const
    {
        oic::net::Request request;
        request.url = QUrl(QStringLiteral("http://pin.example.test:%1%2").arg(upstream_.port()).arg(path));
        return request;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(upstream_.listen());
        upstream_.start();
        QVERIFY(silent_.listen());
        silent_.start();
    }

    // The three-part pinning shape from §8.3: an IP-literal URL makes QNAM send
    // the IP as Host, so the original authority has to be put back explicitly.
    void pinnedRequestKeepsOriginalHost()
    {
        upstream_.respond(200, "{\"ok\":true}");
        const oic::net::Reply reply = makeTransport({ "pin.example.test" }).send(makeRequest("/v1/images/generations"));
        QCOMPARE(reply.status, 200);
        QVERIFY2(reply.error.isEmpty(), qPrintable(reply.error));
        QCOMPARE(reply.body, QByteArray("{\"ok\":true}"));
        QCOMPARE(headerOf(upstream_.requests().last(), "host"),
                 QByteArray("pin.example.test:") + QByteArray::number(upstream_.port()));
    }

    void hostHeaderFromCallerIsOverridden()
    {
        upstream_.respond(200, "ok");
        oic::net::Request request = makeRequest("/v1");
        request.headers.append(oic::net::Header{ "Host", "evil.example.net" });
        makeTransport({ "pin.example.test" }).send(request);
        QCOMPARE(headerOf(upstream_.requests().last(), "host"),
                 QByteArray("pin.example.test:") + QByteArray::number(upstream_.port()));
    }

    void loopbackLiteralIsRefused()
    {
        const int before = upstream_.requests().size();
        oic::net::Request request;
        request.url = QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(upstream_.port()));
        const oic::net::Reply reply = makeTransport({}).send(request);
        QCOMPARE(reply.status, 0);
        QVERIFY2(reply.error.contains(QString::fromUtf8("不允许")), qPrintable(reply.error));
        QVERIFY2(reply.error.contains("127.0.0.1"), qPrintable(reply.error));
        QCOMPARE(upstream_.requests().size(), before);  // refused before dialing
    }

    void loopbackResolutionIsRefusedForUntrustedHost()
    {
        const int before = upstream_.requests().size();
        oic::net::Request request = makeRequest("/v1");
        const oic::net::Reply reply = makeTransport({}).send(request);
        QCOMPARE(reply.status, 0);
        QVERIFY2(reply.error.contains("127.0.0.1"), qPrintable(reply.error));
        QVERIFY2(reply.error.contains("pin.example.test"), qPrintable(reply.error));
        QCOMPARE(upstream_.requests().size(), before);
    }

    void emptyResolutionIsReported()
    {
        oic::net::Transport::Settings settings;
        settings.resolver = [](const QString &) { return QList<QHostAddress>(); };
        const oic::net::Reply reply = oic::net::Transport(std::move(settings)).send(makeRequest("/v1"));
        QCOMPARE(reply.status, 0);
        QVERIFY2(reply.error.contains(QString::fromUtf8("解析失败")), qPrintable(reply.error));
    }

    void credentialsInUrlAreRefused()
    {
        oic::net::Request request;
        request.url = QUrl(QStringLiteral("http://user:secret@pin.example.test:%1/").arg(upstream_.port()));
        const oic::net::Reply reply = makeTransport({ "pin.example.test" }).send(request);
        QCOMPARE(reply.status, 0);
        QVERIFY2(reply.error.contains(QString::fromUtf8("用户名或密码")), qPrintable(reply.error));
    }

    void crossAuthorityRedirectDropsCredentialsAndMethod()
    {
        upstream_.respond(302, "", { { "Location", QByteArray("http://other.example.test:") + QByteArray::number(upstream_.port()) + "/next" } });
        upstream_.respond(200, "landed");

        oic::net::Request request = makeRequest("/v1/images/generations");
        request.method = "POST";
        request.body = "{\"model\":\"gpt-image-2\"}";
        request.headers.append(oic::net::Header{ "Content-Type", "application/json" });
        request.headers.append(oic::net::Header{ "Authorization", "Bearer super-secret" });

        QStringList lookups;
        const oic::net::Reply reply = makeTransport({ "pin.example.test", "other.example.test" }, &lookups).send(request);
        QCOMPARE(reply.status, 200);
        QCOMPARE(reply.body, QByteArray("landed"));
        QCOMPARE(reply.hops, 1);
        QCOMPARE(lookups, QStringList({ "pin.example.test", "other.example.test" }));

        const QList<QByteArray> seen = upstream_.requests();
        QVERIFY(headerOf(seen.at(seen.size() - 2), "authorization").contains("super-secret"));
        QVERIFY2(headerOf(seen.last(), "authorization").isEmpty(), "credential leaked across authorities");
        QVERIFY2(seen.last().startsWith("GET"), "cross-authority hop must be replayed as GET");
        QVERIFY2(headerOf(seen.last(), "content-type").isEmpty(), "stale body headers must not survive a downgrade");
        QCOMPARE(headerOf(seen.last(), "host"), QByteArray("other.example.test:") + QByteArray::number(upstream_.port()));
    }

    void sameAuthorityRedirectKeepsMethodAndBody()
    {
        upstream_.respond(307, "", { { "Location", QByteArray("http://pin.example.test:") + QByteArray::number(upstream_.port()) + "/again" } });
        upstream_.respond(200, "ok");

        oic::net::Request request = makeRequest("/v1");
        request.method = "POST";
        request.body = "payload";
        request.headers.append(oic::net::Header{ "Content-Type", "application/json" });
        request.headers.append(oic::net::Header{ "Authorization", "Bearer keep-me" });

        const oic::net::Reply reply = makeTransport({ "pin.example.test" }).send(request);
        QCOMPARE(reply.status, 200);
        const QList<QByteArray> seen = upstream_.requests();
        QVERIFY2(seen.last().startsWith("POST"), "same-authority 307 must replay the method");
        QVERIFY(seen.last().contains("payload"));
        QVERIFY(headerOf(seen.last(), "authorization").contains("keep-me"));
    }

    void redirectChainIsCapped()
    {
        const int before = upstream_.requests().size();
        for (int i = 0; i <= oic::limits::kMaxRedirects; ++i) {
            upstream_.respond(302, "", { { "Location", QByteArray("http://pin.example.test:") + QByteArray::number(upstream_.port()) + "/loop" } });
        }
        const oic::net::Reply reply = makeTransport({ "pin.example.test" }).send(makeRequest("/loop"));
        QVERIFY2(reply.error.contains(QString::fromUtf8("重定向次数超过上限")), qPrintable(reply.error));
        QCOMPARE(upstream_.requests().size(), before + oic::limits::kMaxRedirects + 1);
    }

    void declaredLengthBeyondCapIsRejected()
    {
        upstream_.respond(200, QByteArray(4096, 'x'));
        oic::net::Request request = makeRequest("/v1");
        request.maxResponseBytes = 100;
        const oic::net::Reply reply = makeTransport({ "pin.example.test" }).send(request);
        QCOMPARE(reply.status, 0);
        QVERIFY2(reply.error.contains(QString::fromUtf8("超过")), qPrintable(reply.error));
    }

    // No Content-Length at all: only the accumulator can bound this one, which is
    // also the shape a gzip'd stream takes (§8.3 notes Content-Length is then the
    // compressed length).
    void unboundedStreamIsRejected()
    {
        upstream_.respondRaw("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n\r\n"
                             + QByteArray(4096, 'y'));
        oic::net::Request request = makeRequest("/v1");
        request.maxResponseBytes = 100;
        const oic::net::Reply reply = makeTransport({ "pin.example.test" }).send(request);
        QCOMPARE(reply.status, 0);
        QVERIFY2(reply.error.contains(QString::fromUtf8("超过")), qPrintable(reply.error));
    }

    void gatewayStatusIsRetryableAndClientErrorIsNot()
    {
        upstream_.respond(502, "{\"error\":{\"type\":\"upstream_error\",\"message\":\"boom\"}}",
                          { { "Content-Type", "application/json" } });
        const oic::net::Reply gateway = makeTransport({ "pin.example.test" }).send(makeRequest("/v1"));
        QCOMPARE(gateway.status, 502);
        QVERIFY2(gateway.error.isEmpty(), qPrintable(gateway.error));  // a response *was* received
        QVERIFY(gateway.retryable);
        QCOMPARE(gateway.contentType, QString("application/json"));

        upstream_.respond(400, "{\"error\":{\"type\":\"invalid_request_error\"}}",
                          { { "Content-Type", "application/json" } });
        const oic::net::Reply client = makeTransport({ "pin.example.test" }).send(makeRequest("/v1"));
        QCOMPARE(client.status, 400);
        QVERIFY2(!client.retryable, "a 4xx refusal must not re-POST a 30 MB body");
    }

    void connectionClosedWithoutAnswerIsReportedAndRetryable()
    {
        // The peer accepts and hangs up before sending a byte: no HTTP happened,
        // so the next candidate endpoint is worth a try.
        oic::net::Request request;
        request.url = QUrl(QStringLiteral("http://pin.example.test:%1/").arg(silent_.port()));
        const oic::net::Reply reply = makeTransport({ "pin.example.test" }).send(request);
        QCOMPARE(reply.status, 0);
        QVERIFY2(reply.error.contains(QString::fromUtf8("无法连接")), qPrintable(reply.error));
        QVERIFY(reply.retryable);
    }
};

QTEST_GUILESS_MAIN(TstTransport)
#include "tst_transport.moc"
