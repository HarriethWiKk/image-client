// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include <QtTest>

#include "oic/core/decode.h"
#include "oic/core/limits.h"

class TstDecode : public QObject
{
    Q_OBJECT

private slots:
    void decodesValidPayloads_data();
    void decodesValidPayloads();

    void rejectsHtmlErrorPages();
    void rejectsTruncatedDecodes();
    void enforcesLimitBeforeAndAfterDecoding();
    void acceptsMimeLineWrapping();
    void toleratesEmptyInput();
};

void TstDecode::decodesValidPayloads_data()
{
    QTest::addColumn<QString>("encoded");
    QTest::addColumn<QByteArray>("expected");

    QTest::newRow("three bytes") << QStringLiteral("YWJj") << QByteArray("abc");
    QTest::newRow("padded") << QStringLiteral("Pz4=") << QByteArray("?>");
    QTest::newRow("alphabet") << QStringLiteral("YWJjZGVmZ2hpamts") << QByteArray("abcdefghijkl");
}

void TstDecode::decodesValidPayloads()
{
    QFETCH(QString, encoded);
    QFETCH(QByteArray, expected);

    bool ok = false;
    QString error;
    QCOMPARE(oic::core::decodeBase64Limited(encoded, 1000, QStringLiteral("媒体文件"), &ok, &error), expected);
    QVERIFY(ok);
    QVERIFY(error.isEmpty());
}

void TstDecode::rejectsHtmlErrorPages()
{
    // A 504 page handed back where an image was expected. A lenient decoder drops
    // every non-alphabet character and returns bytes that look like a corrupt
    // image file, which then gets stored as if it were real output.
    const QString page = QStringLiteral(
        "<!DOCTYPE html><html><head><title>504 Gateway Time-out</title></head>"
        "<body><h1>504 Gateway Time-out</h1></body></html>");

    bool ok = false;
    QString error;
    const QByteArray decoded =
        oic::core::decodeBase64Limited(page, oic::limits::kMaxRemoteMediaBytes, QStringLiteral("上游图像"), &ok, &error);
    QVERIFY(!ok);
    QVERIFY(decoded.isEmpty());
    QVERIFY(error.contains(QStringLiteral("合法")));
}

void TstDecode::rejectsTruncatedDecodes()
{
    // This is the case that makes the difference between Qt's two decoders
    // matter: a payload that starts as valid base64 and then hits garbage.
    // Stopping at the first bad character and reporting success would silently
    // yield the short prefix "abc".
    bool ok = false;
    QString error;
    const QByteArray decoded = oic::core::decodeBase64Limited(
        QStringLiteral("YWJj<html>"), 1000, QStringLiteral("test"), &ok, &error);
    QVERIFY(!ok);
    QVERIFY(decoded.isEmpty());
    QVERIFY(error.contains(QStringLiteral("合法")));
}

void TstDecode::enforcesLimitBeforeAndAfterDecoding()
{
    bool ok = false;
    QString error;

    // Decoded size just over the limit.
    QCOMPARE(oic::core::decodeBase64Limited(QStringLiteral("YWJjZA=="), 3, QStringLiteral("test"), &ok, &error),
             QByteArray());
    QVERIFY(!ok);
    QVERIFY(error.contains(QStringLiteral("超过")));

    // A payload so large it is refused on encoded length, before decoding.
    const QString huge = QString::fromLatin1(QByteArray(10000, 'A'));
    ok = false;
    QCOMPARE(oic::core::decodeBase64Limited(huge, 100, QStringLiteral("test"), &ok, &error), QByteArray());
    QVERIFY(!ok);
    QVERIFY(error.contains(QStringLiteral("超过")));
}

void TstDecode::acceptsMimeLineWrapping()
{
    const QByteArray payload = QByteArray("hello world").repeated(8);
    const QString encoded = QString::fromLatin1(payload.toBase64());

    QString wrapped;
    for (qsizetype offset = 0; offset < encoded.size(); offset += 16) {
        if (!wrapped.isEmpty())
            wrapped.append(QLatin1Char('\n'));
        wrapped.append(encoded.mid(offset, 16));
    }

    bool ok = false;
    QString error;
    QCOMPARE(oic::core::decodeBase64Limited(wrapped, 1000, QStringLiteral("test"), &ok, &error), payload);
    QVERIFY2(ok, qPrintable(error));
}

void TstDecode::toleratesEmptyInput()
{
    bool ok = false;
    QString error;
    QCOMPARE(oic::core::decodeBase64Limited(QString(), 1000, QStringLiteral("test"), &ok, &error), QByteArray());
    QVERIFY(ok);
}

QTEST_GUILESS_MAIN(TstDecode)
#include "tst_decode.moc"
