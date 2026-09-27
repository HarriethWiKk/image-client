// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include <QtTest>

#include "oic/core/models.h"

class TstModels : public QObject
{
    Q_OBJECT

private slots:
    void isGptImage2_data();
    void isGptImage2();

    void isGrokImageModel_data();
    void isGrokImageModel();

    void parseSize_data();
    void parseSize();

    void grokDerivationFromSize_data();
    void grokDerivationFromSize();

    void closestGrokRatioStaysWithinSupportedSet();
};

void TstModels::isGptImage2_data()
{
    QTest::addColumn<QString>("model");
    QTest::addColumn<bool>("expected");

    // Case and padding must not change the branch: the reference CLI compared
    // verbatim, so "GPT-Image-2" silently took the non-gpt-image-2 edit path.
    QTest::newRow("exact") << QStringLiteral("gpt-image-2") << true;
    QTest::newRow("upper") << QStringLiteral("GPT-Image-2") << true;
    QTest::newRow("padded") << QStringLiteral("  gpt-image-2  ") << true;
    QTest::newRow("dash suffix") << QStringLiteral("gpt-image-2-mini") << true;
    QTest::newRow("2.5 flare") << QStringLiteral("gpt-image-2.5-flare") << true;
    QTest::newRow("2.5 sunburst") << QStringLiteral("gpt-image-2.5-sunburst") << true;
    QTest::newRow("2.5 upper") << QStringLiteral("GPT-Image-2.5-Flare") << true;

    QTest::newRow("other model") << QStringLiteral("dall-e-3") << false;
    QTest::newRow("next gen") << QStringLiteral("gpt-image-3") << false;
    QTest::newRow("no separator") << QStringLiteral("gpt-image-25") << false;
    QTest::newRow("prefix only") << QStringLiteral("gpt-image") << false;
    QTest::newRow("empty") << QString() << false;
}

void TstModels::isGptImage2()
{
    QFETCH(QString, model);
    QFETCH(bool, expected);
    QCOMPARE(oic::core::isGptImage2(model), expected);
}

void TstModels::isGrokImageModel_data()
{
    QTest::addColumn<QString>("model");
    QTest::addColumn<bool>("expected");

    QTest::newRow("bare family") << QStringLiteral("GROK-IMAGINE") << true;
    QTest::newRow("pro") << QStringLiteral("grok-imagine-image-pro") << true;
    QTest::newRow("lite") << QStringLiteral("grok-imagine-image-lite") << true;
    QTest::newRow("edit") << QStringLiteral("grok-imagine-edit") << true;
    QTest::newRow("quality padded") << QStringLiteral("  Grok-Imagine-Image-Quality  ") << true;

    // Video models were descoped; classification must still not claim them,
    // or they would be routed down the image path.
    QTest::newRow("video model") << QStringLiteral("grok-imagine-video-1.5") << false;
    QTest::newRow("video bare") << QStringLiteral("grok-imagine-video") << false;
    QTest::newRow("missing dash") << QStringLiteral("grok-imagine-imagequality") << false;
    QTest::newRow("unrelated suffix") << QStringLiteral("grok-imagine-x") << false;
    QTest::newRow("other provider") << QStringLiteral("gpt-image-2") << false;
    QTest::newRow("empty") << QString() << false;
}

void TstModels::isGrokImageModel()
{
    QFETCH(QString, model);
    QFETCH(bool, expected);
    QCOMPARE(oic::core::isGrokImageModel(model), expected);
}

void TstModels::parseSize_data()
{
    QTest::addColumn<QString>("size");
    QTest::addColumn<bool>("ok");
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("height");

    QTest::newRow("plain") << QStringLiteral("1024x1024") << true << 1024 << 1024;
    QTest::newRow("upper X") << QStringLiteral("1920X1080") << true << 1920 << 1080;
    QTest::newRow("inner spaces") << QStringLiteral(" 768 x 1344 ") << true << 768 << 1344;

    QTest::newRow("no separator") << QStringLiteral("1024") << false << 0 << 0;
    QTest::newRow("garbage") << QStringLiteral("garbage") << false << 0 << 0;
    QTest::newRow("empty") << QString() << false << 0 << 0;
    QTest::newRow("zero edge") << QStringLiteral("0x100") << false << 0 << 0;
    QTest::newRow("negative edge") << QStringLiteral("-5x100") << false << 0 << 0;
    QTest::newRow("three parts") << QStringLiteral("1024x1024x512") << false << 0 << 0;
    QTest::newRow("exponent form") << QStringLiteral("1e3x1024") << false << 0 << 0;
}

void TstModels::parseSize()
{
    QFETCH(QString, size);
    QFETCH(bool, ok);
    QFETCH(int, width);
    QFETCH(int, height);

    int parsedWidth = 0;
    int parsedHeight = 0;
    QCOMPARE(oic::core::parseSize(size, &parsedWidth, &parsedHeight), ok);
    if (ok) {
        QCOMPARE(parsedWidth, width);
        QCOMPARE(parsedHeight, height);
    }
}

void TstModels::grokDerivationFromSize_data()
{
    QTest::addColumn<QString>("size");
    QTest::addColumn<QString>("ratio");
    QTest::addColumn<QString>("resolution");

    QTest::newRow("square") << QStringLiteral("1024x1024") << QStringLiteral("1:1") << QStringLiteral("1k");
    QTest::newRow("1080p-ish") << QStringLiteral("1920x1072") << QStringLiteral("16:9") << QStringLiteral("2k");
    QTest::newRow("portrait") << QStringLiteral("720x1280") << QStringLiteral("9:16") << QStringLiteral("1k");
    QTest::newRow("unparsable falls back") << QStringLiteral("garbage") << QStringLiteral("1:1") << QStringLiteral("1k");
    QTest::newRow("empty falls back") << QString() << QStringLiteral("1:1") << QStringLiteral("1k");

    // The 1536 threshold was previously untested on either side.
    QTest::newRow("threshold below") << QStringLiteral("1536x1536") << QStringLiteral("1:1") << QStringLiteral("1k");
    QTest::newRow("threshold above") << QStringLiteral("1537x1537") << QStringLiteral("1:1") << QStringLiteral("2k");
    // Only the long edge decides the tier.
    QTest::newRow("long edge decides") << QStringLiteral("1600x400") << QStringLiteral("20:9") << QStringLiteral("2k");

    QTest::newRow("4k landscape") << QStringLiteral("3840x2160") << QStringLiteral("16:9") << QStringLiteral("2k");
    QTest::newRow("4k portrait") << QStringLiteral("2160x3840") << QStringLiteral("9:16") << QStringLiteral("2k");
    // 1920/880 = 2.182 sits between 2:1 and 19.5:9; the latter is closer.
    QTest::newRow("nearest odd ratio") << QStringLiteral("1920x880") << QStringLiteral("19.5:9") << QStringLiteral("2k");
}

void TstModels::grokDerivationFromSize()
{
    QFETCH(QString, size);
    QFETCH(QString, ratio);
    QFETCH(QString, resolution);

    QCOMPARE(oic::core::grokAspectRatioFromSize(size), ratio);
    QCOMPARE(oic::core::grokResolutionFromSize(size), resolution);
}

void TstModels::closestGrokRatioStaysWithinSupportedSet()
{
    const QStringList supported = oic::core::grokSupportedRatios();
    QVERIFY(supported.contains(QLatin1String("auto")));
    QVERIFY(!supported.contains(QLatin1String("4:1")));

    static const int edges[] = {1, 7, 100, 640, 1920, 3840, 9999};
    for (const int width : edges) {
        for (const int height : edges) {
            const QString ratio = oic::core::closestGrokRatio(width, height);
            const QByteArray context = QStringLiteral("%1x%2 -> %3").arg(width).arg(height).arg(ratio).toUtf8();
            QVERIFY2(supported.contains(ratio), context.constData());
            QVERIFY2(ratio != QLatin1String("auto"), context.constData());
        }
    }
}

QTEST_GUILESS_MAIN(TstModels)
#include "tst_models.moc"
