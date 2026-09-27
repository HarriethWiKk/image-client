// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QHostAddress>
#include <QList>
#include <QString>
#include <QStringList>

namespace oic::core {

// Syntax and policy checks on a caller-supplied URL, performed without any
// name resolution. DNS-dependent checks live in the address helpers below so
// that both halves stay testable without a network.
struct UrlCheck {
    bool ok = false;
    QString hostname;    // lower-cased, trailing dot removed
    quint16 port = 0;    // defaulted from the scheme when absent
    bool https = false;
    QString error;
};

// Rejects anything that is not http(s), carries credentials, omits a host, or
// exceeds the length bound. Credentials in particular are refused outright:
// they would leak into the Host header and into logs.
UrlCheck checkOutboundUrl(const QString &raw, qsizetype maxChars);

// Host names that are exempt from the non-global address rule, e.g. a trusted
// local gateway. Matching is exact and case-insensitive.
bool isExplicitlyTrustedHost(const QString &hostname, const QStringList &trustedHosts);

// Returns the addresses that make a request unsafe, i.e. everything that is not
// publicly routable. Empty means the resolved set is acceptable.
//
// IPv4 is checked against an explicit range table because Qt's own predicates
// leave out 172.16/12 and 100.64/10. IPv6 relies on Qt's predicates plus the
// unique-local range; documentation and Teredo blocks are not enumerated, which
// is a known gap rather than a decision.
QStringList offendingAddresses(const QList<QHostAddress> &resolved);

// Deterministic dial order: IPv4 first, then by the address's own string form.
// Without this the attempt order would follow DNS response order, which varies
// between resolvers and makes failures unreproducible.
QList<QHostAddress> orderForPinning(const QList<QHostAddress> &resolved);

// Whether a redirect target may inherit the credentials sent to `from`.
// Cross-authority hops drop the Authorization header: a provider that hands back
// a 302 to an unrelated host must not receive our API key.
bool retainsCredentialsAcrossRedirect(const QString &fromAuthority, const QString &toAuthority);

QString authorityOf(const QString &hostname, quint16 port, bool https);

}  // namespace oic::core
