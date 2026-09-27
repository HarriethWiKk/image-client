// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/core/urlpolicy.h"

#include <QUrl>

#include <algorithm>

namespace oic::core {
namespace {

QString normalizeHostname(const QString &host)
{
    QString value = host.trimmed().toLower();
    while (value.endsWith(QLatin1Char('.')))
        value.chop(1);
    return value;
}

bool inCidr(quint32 address, quint32 network, int prefixBits)
{
    const quint32 mask = prefixBits >= 32 ? 0xffffffffu : (0xffffffffu << (32 - prefixBits));
    return (address & mask) == (network & mask);
}

// IPv4 ranges that Qt's isGlobal() reports as global but that no provider
// endpoint should ever resolve to. Each one is a route to an internal service:
// 169.254.169.254 in particular is where cloud instance metadata credentials
// live, and 100.64/10 and 172.16/12 are the ranges Qt leaves out.
bool isPubliclyRoutableV4(quint32 address)
{
    struct Range {
        quint32 network;
        int prefix;
    };
    static const Range reserved[] = {
        { 0x00000000u, 8 },  // 0/8        "this network"
        { 0x0a000000u, 8 },  // 10/8       private
        { 0x64400000u, 10 }, // 100.64/10  CGNAT
        { 0x7f000000u, 8 },  // 127/8      loopback
        { 0xa9fe0000u, 16 }, // 169.254/16 link-local, incl. instance metadata
        { 0xac100000u, 12 }, // 172.16/12  private
        { 0xc0000000u, 24 }, // 192.0.0/24 IETF assignments
        { 0xc0000200u, 24 }, // 192.0.2/24 TEST-NET-1
        { 0xc0a80000u, 16 }, // 192.168/16 private
        { 0xc6120000u, 15 }, // 198.18/15  benchmarking
        { 0xc6336400u, 24 }, // 198.51.100/24 TEST-NET-2
        { 0xcb000000u, 24 }, // 203.0.113/24 TEST-NET-3
        { 0xe0000000u, 4 },  // 224/4      multicast
        { 0xf0000000u, 4 },  // 240/4      reserved, incl. broadcast
    };
    for (const Range &range : reserved) {
        if (inCidr(address, range.network, range.prefix))
            return false;
    }
    return true;
}

bool isPubliclyRoutable(const QHostAddress &address)
{
    if (address.isNull())
        return false;

    // toIPv4Address also answers for IPv4-mapped IPv6 (::ffff:127.0.0.1), which is
    // otherwise the classic way around a v4-only check.
    if (address.protocol() == QAbstractSocket::IPv4Protocol
        || (address.protocol() == QAbstractSocket::IPv6Protocol && address.toIPv4Address() != 0)) {
        return isPubliclyRoutableV4(address.toIPv4Address());
    }

    return address.isGlobal() && !address.isPrivateUse() && !address.isUniqueLocalUnicast();
}

}  // namespace

UrlCheck checkOutboundUrl(const QString &raw, qsizetype maxChars)
{
    UrlCheck result;
    const QString trimmed = raw.trimmed();

    if (trimmed.isEmpty()) {
        result.error = QStringLiteral("URL 不能为空");
        return result;
    }
    if (static_cast<qsizetype>(trimmed.size()) > maxChars) {
        result.error = QStringLiteral("URL 过长");
        return result;
    }

    const QUrl url(trimmed);
    // QUrl folds a malformed port (99999, "abc") into general invalidity, so this
    // single check covers those cases too.
    if (!url.isValid()) {
        result.error = QStringLiteral("URL 不合法");
        return result;
    }

    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
        result.error = QStringLiteral("URL 只允许 http 或 https");
        return result;
    }

    const QString hostname = normalizeHostname(url.host());
    if (hostname.isEmpty()) {
        result.error = QStringLiteral("URL 缺少主机名");
        return result;
    }

    if (!url.userName().isEmpty() || !url.password().isEmpty()) {
        result.error = QStringLiteral("URL 不允许包含用户名或密码");
        return result;
    }

    const bool https = scheme == QLatin1String("https");
    // QUrl reports an absent port as 0 and has already rejected out-of-range ports
    // through isValid(), so the only work left is applying the scheme default.
    const int explicitPort = url.port(0);
    if (explicitPort < 0 || explicitPort > 0xffff) {
        result.error = QStringLiteral("URL 端口不合法");
        return result;
    }

    result.ok = true;
    result.hostname = hostname;
    result.port = static_cast<quint16>(explicitPort > 0 ? explicitPort : (https ? 443 : 80));
    result.https = https;
    return result;
}

bool isExplicitlyTrustedHost(const QString &hostname, const QStringList &trustedHosts)
{
    const QString needle = normalizeHostname(hostname);
    if (needle.isEmpty())
        return false;
    return std::any_of(trustedHosts.begin(), trustedHosts.end(), [&needle](const QString &entry) {
        return normalizeHostname(entry) == needle;
    });
}

QStringList offendingAddresses(const QList<QHostAddress> &resolved)
{
    QStringList offending;
    for (const QHostAddress &address : resolved) {
        if (!isPubliclyRoutable(address))
            offending.append(address.toString());
    }
    offending.sort();
    return offending;
}

QList<QHostAddress> orderForPinning(const QList<QHostAddress> &resolved)
{
    QList<QHostAddress> ordered = resolved;
    std::sort(ordered.begin(), ordered.end(), [](const QHostAddress &left, const QHostAddress &right) {
        const bool leftV4 = left.protocol() == QAbstractSocket::IPv4Protocol;
        const bool rightV4 = right.protocol() == QAbstractSocket::IPv4Protocol;
        if (leftV4 != rightV4)
            return leftV4;
        return left.toString() < right.toString();
    });
    return ordered;
}

QString hostHeaderOf(const QString &hostname, quint16 port, bool https)
{
    const QString host = normalizeHostname(hostname);
    const quint16 defaultPort = https ? 443 : 80;
    if (port == defaultPort || port == 0)
        return host;
    return QStringLiteral("%1:%2").arg(host).arg(port);
}

QString originOf(const QString &hostname, quint16 port, bool https)
{
    return QStringLiteral("%1://%2").arg(https ? QLatin1String("https") : QLatin1String("http"),
                                         hostHeaderOf(hostname, port, https));
}

bool retainsCredentialsAcrossRedirect(const QString &fromAuthority, const QString &toAuthority)
{
    return fromAuthority == toAuthority;
}

}  // namespace oic::core
