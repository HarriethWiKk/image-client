// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include <QtTest>

#include "oic/core/limits.h"
#include "oic/core/urlpolicy.h"

using oic::core::UrlCheck;

class TstUrlPolicy : public QObject
{
    Q_OBJECT

private slots:
    void checkOutboundUrl_data();
    void checkOutboundUrl();

    void addressPolicy_data();
    void addressPolicy();

    void trustedHostOverridesAddressPolicy();

    void pinningOrderIsDeterministic();

    void redirectCredentials_data();
    void redirectCredentials();
};

void TstUrlPolicy::checkOutboundUrl_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<bool>("accepted");
    QTest::addColumn<QString>("hostname");
    QTest::addColumn<int>("port");

    QTest::newRow("https default port") << QStringLiteral("https://api.openai.com/v1") << true
                                        << QStringLiteral("api.openai.com") << 443;
    QTest::newRow("http default port") << QStringLiteral("http://gateway.local/v1") << true
                                       << QStringLiteral("gateway.local") << 80;
    QTest::newRow("explicit port") << QStringLiteral("https://api.x.com:8443/v1") << true
                                   << QStringLiteral("api.x.com") << 8443;
    QTest::newRow("mixed case host lowered") << QStringLiteral("https://API.X.AI/v1") << true
                                             << QStringLiteral("api.x.ai") << 443;
    QTest::newRow("trailing dot stripped") << QStringLiteral("https://api.x.ai./v1") << true
                                           << QStringLiteral("api.x.ai") << 443;
    QTest::newRow("surrounding whitespace") << QStringLiteral("  https://api.x.com/v1 ") << true
                                            << QStringLiteral("api.x.com") << 443;

    QTest::newRow("empty") << QString() << false << QString() << 0;
    QTest::newRow("whitespace only") << QStringLiteral("   ") << false << QString() << 0;
    // The scheme is what makes this a provider call rather than a file read.
    QTest::newRow("no scheme") << QStringLiteral("api.x.com/v1") << false << QString() << 0;
    QTest::newRow("file scheme") << QStringLiteral("file:///etc/passwd") << false << QString() << 0;
    QTest::newRow("gopher scheme") << QStringLiteral("gopher://127.0.0.1:6379") << false << QString() << 0;
    // Credentials would travel into the Host header and into any proxy log.
    QTest::newRow("userinfo") << QStringLiteral("https://user:pw@api.x.com/v1") << false << QString() << 0;
    QTest::newRow("username only") << QStringLiteral("https://user@api.x.com/v1") << false << QString() << 0;
    QTest::newRow("no host") << QStringLiteral("https:///v1") << false << QString() << 0;
    QTest::newRow("port out of range") << QStringLiteral("https://api.x.com:99999/v1") << false << QString() << 0;
}

void TstUrlPolicy::checkOutboundUrl()
{
    QFETCH(QString, url);
    QFETCH(bool, accepted);
    QFETCH(QString, hostname);
    QFETCH(int, port);

    const UrlCheck check = oic::core::checkOutboundUrl(url, oic::limits::kMaxBaseUrlChars);
    QCOMPARE(check.ok, accepted);
    if (accepted) {
        QCOMPARE(check.hostname, hostname);
        QCOMPARE(static_cast<int>(check.port), port);
        QVERIFY(check.error.isEmpty());
    } else {
        QVERIFY2(!check.error.isEmpty(), "a rejection must explain itself");
    }
}

void TstUrlPolicy::addressPolicy_data()
{
    QTest::addColumn<QString>("address");
    QTest::addColumn<bool>("offending");

    // Anything the caller could use to reach an internal service.
    QTest::newRow("loopback v4") << QStringLiteral("127.0.0.1") << true;
    QTest::newRow("loopback v6") << QStringLiteral("::1") << true;
    QTest::newRow("private 10/8") << QStringLiteral("10.1.2.3") << true;
    QTest::newRow("private 172/12 low") << QStringLiteral("172.16.0.1") << true;
    QTest::newRow("private 172/12 high") << QStringLiteral("172.31.255.255") << true;
    // Qt's isGlobal() reports the two ranges below as global; they are not.
    QTest::newRow("private 192/16") << QStringLiteral("192.168.0.7") << true;
    QTest::newRow("CGNAT low") << QStringLiteral("100.64.0.1") << true;
    QTest::newRow("CGNAT high") << QStringLiteral("100.127.255.255") << true;
    QTest::newRow("just past CGNAT") << QStringLiteral("100.128.0.1") << false;
    QTest::newRow("benchmarking") << QStringLiteral("198.18.0.1") << true;
    QTest::newRow("TEST-NET-1") << QStringLiteral("192.0.2.1") << true;
    // The mapped form is the way around a v4-only check.
    QTest::newRow("mapped loopback") << QStringLiteral("::ffff:127.0.0.1") << true;
    QTest::newRow("mapped private") << QStringLiteral("::ffff:10.1.2.3") << true;
    QTest::newRow("mapped public") << QStringLiteral("::ffff:8.8.8.8") << false;
    QTest::newRow("link local") << QStringLiteral("169.254.169.254") << true;
    QTest::newRow("this network") << QStringLiteral("0.0.0.0") << true;
    QTest::newRow("broadcast") << QStringLiteral("255.255.255.255") << true;
    QTest::newRow("unique local v6") << QStringLiteral("fc00::1") << true;
    QTest::newRow("v6 link local") << QStringLiteral("fe80::1") << true;
    QTest::newRow("multicast") << QStringLiteral("224.0.0.1") << true;

    // Public space is what a provider endpoint should resolve to.
    QTest::newRow("public dns") << QStringLiteral("8.8.8.8") << false;
    QTest::newRow("public v6") << QStringLiteral("2606:4700:4700::1111") << false;
}

void TstUrlPolicy::addressPolicy()
{
    QFETCH(QString, address);
    QFETCH(bool, offending);

    const QList<QHostAddress> resolved{QHostAddress(address)};
    const QStringList rejected = oic::core::offendingAddresses(resolved);
    QCOMPARE(rejected.isEmpty(), !offending);
}

void TstUrlPolicy::trustedHostOverridesAddressPolicy()
{
    const QStringList trusted{QStringLiteral("Gateway.Local"), QStringLiteral("other.internal")};
    QVERIFY(oic::core::isExplicitlyTrustedHost(QStringLiteral("gateway.local"), trusted));
    QVERIFY(oic::core::isExplicitlyTrustedHost(QStringLiteral("OTHER.INTERNAL."), trusted));
    // Exact host matching only: a suffix or prefix must not inherit trust, or one
    // attacker-controlled domain defeats the whole allow list.
    QVERIFY(!oic::core::isExplicitlyTrustedHost(QStringLiteral("evil-gateway.local"), trusted));
    QVERIFY(!oic::core::isExplicitlyTrustedHost(QStringLiteral("gateway.local.evil.com"), trusted));
    QVERIFY(!oic::core::isExplicitlyTrustedHost(QStringLiteral("gateway.local"), QStringList()));
}

void TstUrlPolicy::pinningOrderIsDeterministic()
{
    const QList<QHostAddress> resolved{
        QHostAddress(QStringLiteral("2001:db8::2")),
        QHostAddress(QStringLiteral("93.184.216.34")),
        QHostAddress(QStringLiteral("2001:db8::1")),
        QHostAddress(QStringLiteral("93.184.216.33")),
    };

    const QList<QHostAddress> ordered = oic::core::orderForPinning(resolved);
    QStringList text;
    for (const QHostAddress &address : ordered)
        text.append(address.toString());

    // IPv4 first, then a stable string ordering, so a failure is reproducible
    // regardless of the order the resolver returned.
    QStringList expected;
    expected << QStringLiteral("93.184.216.33") << QStringLiteral("93.184.216.34")
             << QStringLiteral("2001:db8::1") << QStringLiteral("2001:db8::2");
    QCOMPARE(text, expected);

    // The input must not be reordered in place.
    QCOMPARE(resolved.at(0).toString(), QStringLiteral("2001:db8::2"));
}

void TstUrlPolicy::redirectCredentials_data()
{
    QTest::addColumn<QString>("from");
    QTest::addColumn<QString>("to");
    QTest::addColumn<bool>("retains");

    QTest::newRow("same origin") << QStringLiteral("https://api.x.com") << QStringLiteral("https://api.x.com") << true;
    QTest::newRow("same host different port") << QStringLiteral("https://api.x.com") << QStringLiteral("https://api.x.com:8443") << false;
    QTest::newRow("scheme downgrade") << QStringLiteral("https://api.x.com") << QStringLiteral("http://api.x.com") << false;
    // A provider that bounces us to a CDN must not receive the API key.
    QTest::newRow("cross host") << QStringLiteral("https://api.x.com") << QStringLiteral("https://cdn.example") << false;
}

void TstUrlPolicy::redirectCredentials()
{
    QFETCH(QString, from);
    QFETCH(QString, to);
    QFETCH(bool, retains);
    QCOMPARE(oic::core::retainsCredentialsAcrossRedirect(from, to), retains);
}

QTEST_GUILESS_MAIN(TstUrlPolicy)
#include "tst_urlpolicy.moc"
