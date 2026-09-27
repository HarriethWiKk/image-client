// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/net/transport.h"

#include <QEventLoop>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QTimer>
#include <QVariant>

#include <algorithm>

#include "oic/core/retrypolicy.h"
#include "oic/core/urlpolicy.h"

namespace oic::net {
namespace {

// §7.4: accumulate in bounded chunks instead of trusting one delivery.
constexpr qint64 kReadChunkBytes = 64 * 1024;

QString pinnedHostText(const QHostAddress &address)
{
    const QString text = address.toString();
    if (address.protocol() == QAbstractSocket::IPv6Protocol && !text.startsWith(QLatin1Char('['))) {
        return QLatin1Char('[') + text + QLatin1Char(']');
    }
    return text;
}

bool isCredentialHeader(const QByteArray &name)
{
    // Authorization covers the OpenAI and Grok paths; x-goog-api-key carries the
    // same class of secret on the Gemini path (§5.6), so both must die at a
    // cross-authority hop.
    return name.compare("authorization", Qt::CaseInsensitive) == 0
           || name.compare("x-goog-api-key", Qt::CaseInsensitive) == 0;
}

bool isBodyHeader(const QByteArray &name)
{
    return name.compare("content-length", Qt::CaseInsensitive) == 0
           || name.compare("content-type", Qt::CaseInsensitive) == 0;
}

bool isRedirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

QByteArray headerValue(const QList<Header> &headers, const char *name)
{
    for (const Header &header : headers) {
        if (header.name.compare(name, Qt::CaseInsensitive) == 0) {
            return header.value;
        }
    }
    return QByteArray();
}

struct Hop {
    core::UrlCheck check;
    QList<QHostAddress> ordered;
    QString error;
};

// Resolve-once and validate: everything after this point dials one of these
// addresses and never asks the resolver again, which is what closes the window a
// "validate then reconnect" design leaves open.
Hop validateHop(const QUrl &url, const QStringList &trustedHosts, const Resolver &resolver)
{
    Hop hop;
    hop.check = core::checkOutboundUrl(url.toString(QUrl::FullyEncoded), limits::kMaxBaseUrlChars);
    if (!hop.check.ok) {
        hop.error = hop.check.error;
        return hop;
    }

    QHostAddress literal(hop.check.hostname);
    QList<QHostAddress> addresses;
    if (!literal.isNull()) {
        addresses.append(literal);  // the URL already carries the address
    } else {
        addresses = resolver(hop.check.hostname);
        if (addresses.isEmpty()) {
            hop.error = QStringLiteral("域名解析失败：%1").arg(hop.check.hostname);
            return hop;
        }
    }

    // A literal IP in the URL is held to the same rule as a resolved one: not
    // asking DNS is no reason to allow 169.254.169.254.
    const QStringList offending = core::offendingAddresses(addresses);
    if (!offending.isEmpty() && !core::isExplicitlyTrustedHost(hop.check.hostname, trustedHosts)) {
        hop.error = QStringLiteral("%1 指向不允许访问的地址：%2").arg(hop.check.hostname,
                                                                    offending.join(QStringLiteral(", ")));
        return hop;
    }

    hop.ordered = core::orderForPinning(addresses);
    return hop;
}

struct Attempt {
    bool responded = false;
    bool timedOut = false;
    bool oversized = false;
    int status = 0;
    QByteArray body;
    QList<Header> headers;
    QString detail;
};

Attempt dial(const QUrl &pinned, const QString &hostHeader, const QString &verifyName, qint64 maxBytes,
             int timeoutSeconds, const QByteArray &method, const QList<Header> &headers, const QByteArray &body)
{
    QNetworkAccessManager manager;
    // The pinned address *is* the routing decision, so no system or PAC proxy may
    // sit in front of it: a proxy would re-resolve the name we just declined to
    // trust.
    manager.setProxy(QNetworkProxy::NoProxy);

    QNetworkRequest outgoing(pinned);
    outgoing.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    // Caller headers go first so the two below always win: without an explicit
    // Host the pinned URL would send an IP authority to a vhost router (§8.3),
    // and a caller must not be able to smuggle its own Host either.
    for (const Header &header : headers) {
        if (header.name.compare("host", Qt::CaseInsensitive) == 0) {
            continue;
        }
        outgoing.setRawHeader(header.name, header.value);
    }
    outgoing.setRawHeader("Host", hostHeader.toUtf8());
    if (pinned.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0) {
        outgoing.setPeerVerifyName(verifyName);
    }
    outgoing.setTransferTimeout(timeoutSeconds * 1000);

    QNetworkReply *reply = method.compare("GET", Qt::CaseInsensitive) == 0
                               ? manager.get(outgoing)
                               : manager.sendCustomRequest(outgoing, method, body);

    Attempt attempt;
    bool oversized = false;
    bool lengthChecked = false;
    auto pump = [reply, &attempt, &oversized, &lengthChecked, maxBytes] {
        // The declared length describes the compressed body while the accumulator
        // counts the decompressed one, so this is a fast path and not the bound.
        if (!lengthChecked) {
            lengthChecked = true;
            const QVariant declared = reply->header(QNetworkRequest::ContentLengthHeader);
            if (declared.isValid() && declared.toLongLong() > maxBytes) {
                oversized = true;
                reply->abort();
                return;
            }
        }
        while (reply->bytesAvailable() > 0) {
            const qint64 room = maxBytes + 1 - attempt.body.size();
            if (room <= 0) {
                oversized = true;
                break;
            }
            const QByteArray chunk = reply->read(std::min(kReadChunkBytes, room));
            if (chunk.isEmpty()) {
                break;
            }
            attempt.body += chunk;
            if (attempt.body.size() > maxBytes) {
                oversized = true;
                break;
            }
        }
        if (oversized) {
            reply->abort();
        }
    };
    QObject::connect(reply, &QNetworkReply::readyRead, reply, pump);

    QEventLoop loop;
    bool timedOut = false;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&loop, &timedOut, reply] {
        timedOut = true;
        reply->abort();
        loop.quit();
    });
    deadline.start(timeoutSeconds * 1000);
    loop.exec();

    if (!oversized) {
        pump();
    }

    attempt.responded = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() > 0;
    attempt.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    attempt.detail = reply->errorString();
    attempt.timedOut = timedOut;
    attempt.oversized = oversized;
    for (const QNetworkReply::RawHeaderPair &pair : reply->rawHeaderPairs()) {
        attempt.headers.append(Header{pair.first, pair.second});
    }
    reply->deleteLater();
    return attempt;
}

}  // namespace

Transport::Transport(Settings settings) : settings_(std::move(settings)) {}

QList<QHostAddress> Transport::systemResolve(const QString &hostname)
{
    return QHostInfo::fromName(hostname).addresses();
}

Reply Transport::send(const Request &request) const
{
    Reply reply;
    const int timeoutSeconds =
        std::clamp(request.timeoutSeconds, limits::kMinTimeoutSeconds, limits::kMaxTimeoutSeconds);
    const Resolver resolver = settings_.resolver ? settings_.resolver : &Transport::systemResolve;

    QUrl current = request.url;
    QByteArray method = request.method;
    QList<Header> headers = request.headers;
    QByteArray body = request.body;
    QString previousAuthority;
    bool mustBeGet = false;

    for (int hopIndex = 0; hopIndex <= limits::kMaxRedirects; ++hopIndex) {
        const Hop hop = validateHop(current, settings_.trustedHosts, resolver);
        if (!hop.error.isEmpty()) {
            reply.error = hop.error;
            return reply;
        }

        const QString authority = core::hostHeaderOf(hop.check.hostname, hop.check.port, hop.check.https);
        if (!previousAuthority.isEmpty() && !core::retainsCredentialsAcrossRedirect(previousAuthority, authority)) {
            // A provider that answers 302 with a link to an unrelated host must
            // not receive the API key, and the replayed request becomes a GET.
            headers.erase(std::remove_if(headers.begin(), headers.end(),
                                         [](const Header &header) { return isCredentialHeader(header.name); }),
                          headers.end());
            mustBeGet = true;
        }
        previousAuthority = authority;

        if (mustBeGet) {
            method = QByteArrayLiteral("GET");
            body.clear();
            headers.erase(std::remove_if(headers.begin(), headers.end(),
                                         [](const Header &header) { return isBodyHeader(header.name); }),
                          headers.end());
            mustBeGet = false;
        }

        Attempt last;
        bool dialed = false;
        for (const QHostAddress &address : hop.ordered) {
            QUrl pinned = current;
            pinned.setHost(pinnedHostText(address), QUrl::StrictMode);
            if (!pinned.isValid()) {
                continue;
            }
            Attempt attempt =
                dial(pinned, authority, hop.check.hostname, request.maxResponseBytes, timeoutSeconds, method, headers,
                     body);
            dialed = true;
            last = std::move(attempt);
            if (last.responded) {
                break;  // next address only matters when this one never answered
            }
        }
        if (!dialed) {
            reply.error = QStringLiteral("%1 没有可用的目标地址").arg(hop.check.hostname);
            return reply;
        }

        reply.status = last.status;
        reply.body = std::move(last.body);
        reply.headers = std::move(last.headers);
        reply.contentType = QString::fromUtf8(headerValue(reply.headers, "content-type"));
        reply.effectiveUrl = current;
        reply.hops = hopIndex;
        reply.detail = last.detail;

        if (last.oversized) {
            reply.status = 0;
            reply.error = QStringLiteral("响应体超过 %1 字节上限").arg(request.maxResponseBytes);
            reply.retryable = true;
            return reply;
        }
        if (last.timedOut) {
            reply.status = 0;
            reply.error = QStringLiteral("请求超时（%1 秒）").arg(timeoutSeconds);
            reply.retryable = true;
            return reply;
        }
        if (!last.responded) {
            reply.status = 0;
            reply.error = QStringLiteral("无法连接到 %1").arg(authority);
            reply.retryable = true;  // endpoint probing: another candidate may answer
            return reply;
        }

        if (isRedirect(reply.status)) {
            const QByteArray location = headerValue(reply.headers, "location");
            if (!location.isEmpty()) {
                if (hopIndex == limits::kMaxRedirects) {
                    reply.error = QStringLiteral("重定向次数超过上限（%1 次）").arg(limits::kMaxRedirects);
                    return reply;
                }
                const QUrl target = current.resolved(QUrl(QString::fromUtf8(location)));
                if (!target.isValid() || target.host().isEmpty()) {
                    reply.error = QStringLiteral("重定向目标不合法：%1").arg(QString::fromUtf8(location));
                    return reply;
                }
                if (reply.status == 303) {
                    mustBeGet = true;  // 303 is unconditional, same authority or not
                }
                current = target;
                continue;
            }
        }

        const QString errorType = core::jsonErrorType(reply.body, reply.contentType);
        reply.retryable = !core::refusesRetryAsClientError(reply.status, reply.contentType)
                          && core::looksLikeGatewayOrUpstreamError(reply.status, errorType);
        return reply;
    }

    reply.error = QStringLiteral("重定向次数超过上限（%1 次）").arg(limits::kMaxRedirects);
    return reply;
}

}  // namespace oic::net
