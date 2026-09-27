// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include <QtTest>

#include "oic/core/retrypolicy.h"

class TstRetryPolicy : public QObject
{
    Q_OBJECT

private slots:
    void jsonErrorType_data();
    void jsonErrorType();

    void clientErrorsRefusingRetry_data();
    void clientErrorsRefusingRetry();

    void gatewayErrors_data();
    void gatewayErrors();
};

void TstRetryPolicy::jsonErrorType_data()
{
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QString>("contentType");
    QTest::addColumn<QString>("expected");

    QTest::newRow("nested error")
        << QByteArray(R"({"error": {"type": "upstream_error", "message": "boom"}})")
        << QStringLiteral("application/json") << QStringLiteral("upstream_error");
    QTest::newRow("top level type") << QByteArray(R"({"type": "rate_limit"})")
                                    << QStringLiteral("application/json") << QStringLiteral("rate_limit");
    QTest::newRow("charset suffix") << QByteArray(R"({"error": {"type": "server_error"}})")
                                     << QStringLiteral("application/json; charset=utf-8")
                                     << QStringLiteral("server_error");
    QTest::newRow("no type field") << QByteArray(R"({"error": {"message": "boom"}})")
                                   << QStringLiteral("application/json") << QString();
    // An HTML login wall must not be parsed as an API error document.
    QTest::newRow("html body") << QByteArray("<html>login</html>")
                               << QStringLiteral("text/html") << QString();
    QTest::newRow("json array") << QByteArray("[1, 2, 3]")
                                << QStringLiteral("application/json") << QString();
    QTest::newRow("json content type but broken body") << QByteArray("{oops")
                                                       << QStringLiteral("application/json") << QString();
}

void TstRetryPolicy::jsonErrorType()
{
    QFETCH(QByteArray, body);
    QFETCH(QString, contentType);
    QFETCH(QString, expected);
    QCOMPARE(oic::core::jsonErrorType(body, contentType), expected);
}

void TstRetryPolicy::clientErrorsRefusingRetry_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<QString>("contentType");
    QTest::addColumn<bool>("refuses");

    // A refusal of the payload itself: repeating it at the next candidate endpoint
    // would resend up to 50 MB of reference images and could bill twice.
    QTest::newRow("bad request") << 400 << QStringLiteral("application/json") << true;
    QTest::newRow("unprocessable") << 422 << QStringLiteral("application/json") << true;
    QTest::newRow("forbidden") << 403 << QStringLiteral("application/json") << true;
    // 404/405 are how endpoint probing finds the right path.
    QTest::newRow("not found stays probeable") << 404 << QStringLiteral("application/json") << false;
    QTest::newRow("method not allowed stays probeable") << 405 << QStringLiteral("application/json") << false;
    // An HTML body means something is answering in front of the API.
    QTest::newRow("bad request behind portal") << 400 << QStringLiteral("text/html") << false;
    QTest::newRow("server error") << 500 << QStringLiteral("application/json") << false;
    QTest::newRow("success") << 200 << QStringLiteral("application/json") << false;
}

void TstRetryPolicy::clientErrorsRefusingRetry()
{
    QFETCH(int, status);
    QFETCH(QString, contentType);
    QFETCH(bool, refuses);
    QCOMPARE(oic::core::refusesRetryAsClientError(status, contentType), refuses);
}

void TstRetryPolicy::gatewayErrors_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<QString>("errorType");
    QTest::addColumn<bool>("retryNextEndpoint");

    QTest::newRow("bad gateway") << 502 << QString() << true;
    QTest::newRow("unavailable") << 503 << QString() << true;
    QTest::newRow("gateway timeout") << 504 << QString() << true;
    QTest::newRow("ok") << 200 << QString() << false;
    // Some relays report an upstream failure with a 4xx status; the type field is
    // what makes it worth retrying.
    QTest::newRow("400 flagged upstream") << 400 << QStringLiteral("upstream_error") << true;
    QTest::newRow("400 ordinary") << 400 << QStringLiteral("invalid_request_error") << false;
    QTest::newRow("400 no type") << 400 << QString() << false;
}

void TstRetryPolicy::gatewayErrors()
{
    QFETCH(int, status);
    QFETCH(QString, errorType);
    QFETCH(bool, retryNextEndpoint);
    QCOMPARE(oic::core::looksLikeGatewayOrUpstreamError(status, errorType), retryNextEndpoint);
}

QTEST_GUILESS_MAIN(TstRetryPolicy)
#include "tst_retrypolicy.moc"
