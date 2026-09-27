// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include <QtTest>

#include "oic/core/endpoints.h"

using oic::core::Route;

class TstEndpoints : public QObject
{
    Q_OBJECT

private slots:
    void normalizeBaseUrl_data();
    void normalizeBaseUrl();

    void joinUrl();

    void candidateEndpoints_data();
    void candidateEndpoints();
};

void TstEndpoints::normalizeBaseUrl_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("plain") << QStringLiteral("https://api.x.com") << QStringLiteral("https://api.x.com");
    QTest::newRow("trailing slashes") << QStringLiteral("https://api.x.com///") << QStringLiteral("https://api.x.com");
    QTest::newRow("surrounding space") << QStringLiteral("  https://api.x.com/v1  ") << QStringLiteral("https://api.x.com/v1");
    QTest::newRow("only slashes") << QStringLiteral("/") << QString();
    QTest::newRow("empty") << QString() << QString();
}

void TstEndpoints::normalizeBaseUrl()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(oic::core::normalizeBaseUrl(input), expected);
}

void TstEndpoints::joinUrl()
{
    QCOMPARE(oic::core::joinUrl(QStringLiteral("https://a.b/"), QStringLiteral("/v1/x")),
             QStringLiteral("https://a.b/v1/x"));
    QCOMPARE(oic::core::joinUrl(QStringLiteral("https://a.b"), QStringLiteral("v1/x")),
             QStringLiteral("https://a.b/v1/x"));
    QCOMPARE(oic::core::joinUrl(QStringLiteral("https://a.b"), QStringLiteral("v1/x/")),
             QStringLiteral("https://a.b/v1/x/"));
}

void TstEndpoints::candidateEndpoints_data()
{
    QTest::addColumn<QString>("base");
    QTest::addColumn<QString>("route");
    QTest::addColumn<QStringList>("expected");

    // Base ending in the version prefix must not gain a second /v1.
    QTest::newRow("versioned base") << QStringLiteral("https://api.x.com/v1") << QStringLiteral("images/generations")
                                    << QStringList{QStringLiteral("https://api.x.com/v1/images/generations")};

    // A base that already names a whole endpoint must have that route replaced,
    // not appended to.
    QTest::newRow("full endpoint base") << QStringLiteral("https://api.x.com/v1/images/generations")
                                        << QStringLiteral("images/edits")
                                        << QStringList{QStringLiteral("https://api.x.com/v1/images/edits")};

    QTest::newRow("unversioned full endpoint") << QStringLiteral("https://api.x.com/images/edits")
                                               << QStringLiteral("images/generations")
                                               << QStringList{QStringLiteral("https://api.x.com/images/generations")};

    // Bare host probes /v1 first, then the raw route.
    QTest::newRow("bare host probes v1 first") << QStringLiteral("https://api.x.com") << QStringLiteral("images/generations")
                                               << QStringList{QStringLiteral("https://api.x.com/v1/images/generations"),
                                                              QStringLiteral("https://api.x.com/images/generations")};

    QTest::newRow("gateway subpath probes v1 first") << QStringLiteral("https://gateway.example/img-proxy")
                                                     << QStringLiteral("images/edits")
                                                     << QStringList{QStringLiteral("https://gateway.example/img-proxy/v1/images/edits"),
                                                                    QStringLiteral("https://gateway.example/img-proxy/images/edits")};

    QTest::newRow("blank base yields nothing") << QString() << QStringLiteral("images/generations") << QStringList();
    QTest::newRow("whitespace base yields nothing") << QStringLiteral("   ") << QStringLiteral("images/edits") << QStringList();
}

void TstEndpoints::candidateEndpoints()
{
    QFETCH(QString, base);
    QFETCH(QString, route);
    QFETCH(QStringList, expected);

    const Route parsedRoute = route == QLatin1String("images/edits") ? Route::Edits : Route::Generations;
    QCOMPARE(oic::core::candidateEndpoints(base, parsedRoute), expected);
}

QTEST_GUILESS_MAIN(TstEndpoints)
#include "tst_endpoints.moc"
