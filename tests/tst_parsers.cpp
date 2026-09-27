// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
//
// Response normalization (SPEC 7.1 / 7.2). The interesting cases are the ones where
// a plausible-looking body must not produce a saved image, and where an error path
// must not smuggle megabytes of image data into the diagnostics.

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QTest>

#include "oic/core/limits.h"
#include "oic/protocol/imageprotocol.h"
#include "oic/protocol/parsers.h"

using namespace oic::protocol;

namespace {

QByteArray json(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray fakePng(int size)
{
    QByteArray payload = QByteArray::fromHex("89504E470D0A1A0A0000000D494844520000000800000004");
    payload += QByteArray(qMax(0, size - payload.size()), 'p');
    return payload;
}

QString b64(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toBase64());
}

ResponseContext testContext()
{
    ResponseContext context;
    context.clientRequestId = QStringLiteral("req-1");
    context.endpoint = QStringLiteral("https://api.example.com/v1/images/generations");
    return context;
}

MediaLimits smallLimits(int perImage = 1024 * 1024, int total = 4 * 1024 * 1024, int maxItems = 16)
{
    MediaLimits limits;
    limits.perImageBytes = perImage;
    limits.totalBytes = total;
    limits.maxItems = maxItems;
    return limits;
}

QByteArray decodedFromDataUrl(const QString &dataUrl)
{
    return QByteArray::fromBase64(dataUrl.section(QLatin1Char(','), 1).toLatin1());
}

}  // namespace

class TstParsers : public QObject
{
    Q_OBJECT

private slots:
    void malformedBodies_data();
    void malformedBodies();

    void missingDataArray();
    void itemFieldPriorityAndMime();
    void unusableItemsAreSkippedAndReported();
    void urlBranch_data();
    void urlBranch();
    void resultBranch_data();
    void resultBranch();
    void itemCountIsCappedNotRejected();
    void totalSizeCapAbortsTheWholeResult();
    void chineseErrorMessageSurvivesDiagnostics();

    void geminiHappyPathAndSpellingVariants();
    void geminiEmptyResultUsesFrozenMessage();

    void compactRawKeepsSmallBodies();
    void compactRawDropsImageBytes();
};

void TstParsers::malformedBodies_data()
{
    QTest::addColumn<QByteArray>("body");

    QTest::newRow("empty") << QByteArray();
    QTest::newRow("html error page")
        << QByteArray("<!DOCTYPE html><html><head><title>504 Gateway Time-out</title></head><body>x</body></html>");
    QTest::newRow("json array top level") << QByteArray("[1, 2, 3]");
    QTest::newRow("json scalar") << QByteArray("\"just a string\"");
}

void TstParsers::malformedBodies()
{
    QFETCH(const QByteArray, body);
    const ParseResult result = parseImageResponse(body, QStringLiteral("application/json"), testContext());
    QVERIFY2(!result.ok(), "a body with no data array must not parse successfully");
    QVERIFY(result.images.isEmpty());
    QVERIFY(!result.error.isEmpty());
    QVERIFY2(!result.diagnosticJson.isEmpty(), "the failure has to carry something diagnosable");
}

void TstParsers::missingDataArray()
{
    struct Row
    {
        const char *label;
        QJsonObject body;
    };
    const QList<Row> rows{
        { "no data key", QJsonObject{} },
        { "empty array", QJsonObject{ { QStringLiteral("data"), QJsonArray{} } } },
        { "not an array", QJsonObject{ { QStringLiteral("data"), QStringLiteral("none") } } },
        { "object instead", QJsonObject{ { QStringLiteral("data"), QJsonObject{} } } },
    };

    for (const Row &row : rows) {
        const ParseResult result = parseImageResponse(json(row.body), QStringLiteral("application/json"), testContext());
        QVERIFY2(!result.ok(), row.label);
        // Message text is frozen: users quote it in bug reports and the MCP error
        // path forwards it verbatim.
        QCOMPARE(result.error, QStringLiteral("接口返回异常，未找到 data"));
        QVERIFY2(result.diagnosticJson.contains("req-1"), row.label);
        QVERIFY2(result.diagnosticJson.contains("api.example.com"), row.label);
    }
}

void TstParsers::itemFieldPriorityAndMime()
{
    const QByteArray png = fakePng(30);
    QJsonObject item;
    item.insert(QStringLiteral("b64_json"), b64(png));
    item.insert(QStringLiteral("output_format"), QStringLiteral("jpeg"));
    // Present but must be ignored: b64_json has priority (SPEC 7.1).
    item.insert(QStringLiteral("url"), QStringLiteral("https://ignored.example/x.png"));
    item.insert(QStringLiteral("result"), b64(QByteArray(40, 'z')));

    QJsonArray data;
    data.append(item);
    const QJsonObject body = QJsonObject{ { QStringLiteral("data"), data } };

    ResponseContext context = testContext();
    context.limits = smallLimits();
    bool fetched = false;
    context.fetcher = [&fetched](const QUrl &, QByteArray *bytes, QString *, QString *) {
        fetched = true;
        *bytes = QByteArray("nope");
        return true;
    };

    const ParseResult result = parseImageResponse(json(body), QStringLiteral("application/json"), context);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.images.size(), 1);
    QVERIFY(result.images.at(0).startsWith(QStringLiteral("data:image/jpeg;base64,")));
    QCOMPARE(decodedFromDataUrl(result.images.at(0)), png);
    QVERIFY2(!fetched, "b64_json must win before any download is attempted");
}

void TstParsers::unusableItemsAreSkippedAndReported()
{
    QJsonObject broken;
    // Valid base64 that is not an image: SPEC 7.1 does not gate content here (the
    // store probe does on save), so the case worth pinning is an item that cannot
    // even decode.
    broken.insert(QStringLiteral("b64_json"), QStringLiteral("not!base64!at!all"));
    QJsonObject empty;
    empty.insert(QStringLiteral("revised_prompt"), QStringLiteral("only metadata"));
    QJsonArray data;
    data.append(broken);
    data.append(empty);
    const QJsonObject body = QJsonObject{ { QStringLiteral("data"), data } };

    ResponseContext context = testContext();
    context.limits = smallLimits();
    const ParseResult result = parseImageResponse(json(body), QStringLiteral("application/json"), context);
    QVERIFY2(!result.ok(), "no usable item may look like success");
    QCOMPARE(result.error, QStringLiteral("返回里没有可保存的 b64_json、url 或 result"));
    QCOMPARE(result.skippedItems, 2);
    QVERIFY(result.images.isEmpty());
}

void TstParsers::urlBranch_data()
{
    QTest::addColumn<bool>("fetchSucceeds");
    QTest::addColumn<bool>("hasFetcher");

    QTest::newRow("fetch ok") << true << true;
    QTest::newRow("fetch fails") << false << true;
    QTest::newRow("no fetcher wired") << false << false;
}

void TstParsers::urlBranch()
{
    QFETCH(const bool, fetchSucceeds);
    QFETCH(const bool, hasFetcher);

    const QByteArray png = fakePng(24);
    const QString remote = QStringLiteral("https://cdn.example.com/img.png");
    QJsonObject item;
    item.insert(QStringLiteral("url"), remote);
    QJsonArray data;
    data.append(item);
    const QJsonObject body = QJsonObject{ { QStringLiteral("data"), data } };

    ResponseContext context = testContext();
    context.limits = smallLimits();
    QStringList seen;
    if (hasFetcher) {
        context.fetcher = [&remote, &png, &seen, fetchSucceeds](const QUrl &url, QByteArray *bytes,
                                                                QString *contentType, QString *) {
            seen.append(url.toString());
            if (!fetchSucceeds) {
                return false;
            }
            *bytes = png;
            *contentType = QStringLiteral("image/png");
            return true;
        };
    }

    const ParseResult result = parseImageResponse(json(body), QStringLiteral("application/json"), context);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.images.size(), 1);

    if (hasFetcher && fetchSucceeds) {
        QCOMPARE(result.images.at(0), dataUrlFromBytes(png, QStringLiteral("image/png")));
        QCOMPARE(seen, QStringList{ remote });
    } else {
        // Frozen fallback: keep the link rather than fail the job outright.
        QCOMPARE(result.images.at(0), remote);
    }
}

void TstParsers::resultBranch_data()
{
    QTest::addColumn<QString>("result");
    QTest::addColumn<QString>("expectPrefix");
    QTest::addColumn<bool>("fetchSucceeds");

    QTest::newRow("data url passes through") << QStringLiteral("data:image/png;base64,") + b64(fakePng(20))
                                            << "data:image/png;base64," << true;
    QTest::newRow("bare base64 uses output_format") << b64(fakePng(20)) << "data:image/png;base64," << true;
    // A reachable remote result is inlined.
    QTest::newRow("remote result fetched") << QStringLiteral("https://cdn.example.com/r.png")
                                          << "data:image/png;base64," << true;
    // Only a failed download keeps the link (SPEC 7.1 frozen fallback).
    QTest::newRow("remote result unreachable") << QStringLiteral("https://cdn.example.com/r.png")
                                              << "https://cdn.example.com/r.png" << false;
    // A data URL that is not really base64 must not be handed back as an image.
    QTest::newRow("malformed data url") << QStringLiteral("data:image/png,raw") << "" << true;
}

void TstParsers::resultBranch()
{
    QFETCH(const QString, result);
    QFETCH(const QString, expectPrefix);
    QFETCH(const bool, fetchSucceeds);

    QJsonObject item;
    item.insert(QStringLiteral("result"), result);
    QJsonArray data;
    data.append(item);
    const QJsonObject body = QJsonObject{ { QStringLiteral("data"), data } };

    ResponseContext context = testContext();
    context.limits = smallLimits();
    context.fetcher = [fetchSucceeds](const QUrl &, QByteArray *bytes, QString *contentType, QString *) {
        if (!fetchSucceeds) {
            return false;
        }
        *bytes = fakePng(20);
        *contentType = QStringLiteral("image/png");
        return true;
    };

    const ParseResult parsed = parseImageResponse(json(body), QStringLiteral("application/json"), context);
    if (expectPrefix.isEmpty()) {
        QVERIFY2(!parsed.ok(), "an unusable result field must not produce an image");
        QVERIFY(parsed.images.isEmpty());
        return;
    }
    QVERIFY2(parsed.ok(), qPrintable(parsed.error));
    QVERIFY2(parsed.images.at(0).startsWith(expectPrefix), qPrintable(parsed.images.at(0)));
}

void TstParsers::itemCountIsCappedNotRejected()
{
    QJsonArray data;
    for (int i = 0; i < 20; ++i) {
        QJsonObject item;
        item.insert(QStringLiteral("b64_json"), b64(fakePng(24)));
        data.append(item);
    }
    const QJsonObject body = QJsonObject{ { QStringLiteral("data"), data } };

    ResponseContext context = testContext();
    context.limits = smallLimits(1024 * 1024, 1024 * 1024 * 8, 3);
    const ParseResult result = parseImageResponse(json(body), QStringLiteral("application/json"), context);
    QVERIFY2(result.ok(), qPrintable(result.error));
    // Truncated adoption, mirroring the previous behaviour: the extras are dropped
    // rather than failing a run that already produced images.
    QCOMPARE(result.images.size(), 3);
}

void TstParsers::totalSizeCapAbortsTheWholeResult()
{
    QJsonArray data;
    for (int i = 0; i < 4; ++i) {
        QJsonObject item;
        item.insert(QStringLiteral("b64_json"), b64(fakePng(1000)));
        data.append(item);
    }
    const QJsonObject body = QJsonObject{ { QStringLiteral("data"), data } };

    ResponseContext context = testContext();
    context.limits = smallLimits(1024 * 1024, 2500, 16);
    const ParseResult result = parseImageResponse(json(body), QStringLiteral("application/json"), context);
    QVERIFY2(!result.ok(), qPrintable(QStringLiteral("images=%1").arg(result.images.size())));
    QVERIFY2(result.error.contains(QString::fromUtf8("总大小")), qPrintable(result.error));
    // No partial gallery: a truncated set would look like a completed job.
    QVERIFY2(result.images.isEmpty(), "the oversized result must be discarded whole");
}

void TstParsers::chineseErrorMessageSurvivesDiagnostics()
{
    QJsonObject error;
    error.insert(QStringLiteral("message"), QStringLiteral("上游模型通道异常，请稍后重试"));
    const QJsonObject body = QJsonObject{ { QStringLiteral("error"), error } };

    const ParseResult result = parseImageResponse(json(body), QStringLiteral("application/json"), testContext());
    QVERIFY(!result.ok());
    QVERIFY2(result.diagnosticJson.contains(QString::fromUtf8("上游模型通道异常")),
             qPrintable(result.diagnosticJson));
    QVERIFY2(!result.diagnosticJson.contains("\\u4e0a"), "non-ASCII must stay readable, not escaped or mojibaked");
}

void TstParsers::geminiHappyPathAndSpellingVariants()
{
    const QByteArray first = fakePng(40);
    const QByteArray second = fakePng(60);

    QJsonObject inlineOne;
    inlineOne.insert(QStringLiteral("mimeType"), QStringLiteral("image/png"));
    inlineOne.insert(QStringLiteral("data"), b64(first));
    QJsonObject partOne;
    partOne.insert(QStringLiteral("inlineData"), inlineOne);

    QJsonObject textPart;
    textPart.insert(QStringLiteral("text"), QStringLiteral("here is your image"));

    // snake_case spelling and mime_type also occur in real responses.
    QJsonObject inlineTwo;
    inlineTwo.insert(QStringLiteral("mime_type"), QStringLiteral("image/png"));
    inlineTwo.insert(QStringLiteral("data"), b64(second));
    QJsonObject partTwo;
    partTwo.insert(QStringLiteral("inline_data"), inlineTwo);

    QJsonObject notAnImage;
    notAnImage.insert(QStringLiteral("mimeType"), QStringLiteral("text/plain"));
    notAnImage.insert(QStringLiteral("data"), b64(QByteArray("notes")));
    QJsonObject textInline;
    textInline.insert(QStringLiteral("inlineData"), notAnImage);

    QJsonArray parts;
    parts.append(textPart);
    parts.append(partOne);
    parts.append(partTwo);
    parts.append(textInline);
    QJsonObject content;
    content.insert(QStringLiteral("parts"), parts);
    QJsonObject candidate;
    candidate.insert(QStringLiteral("content"), content);
    QJsonArray candidates;
    candidates.append(candidate);
    const QJsonObject body = QJsonObject{ { QStringLiteral("candidates"), candidates } };

    ResponseContext context = testContext();
    context.limits = smallLimits();
    const ParseResult result = parseGeminiResponse(json(body), QStringLiteral("application/json"), context);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.images.size(), 2);
    QCOMPARE(decodedFromDataUrl(result.images.at(0)), first);
    QCOMPARE(decodedFromDataUrl(result.images.at(1)), second);
    QVERIFY(result.images.at(0).startsWith(QStringLiteral("data:image/png;base64,")));
}

void TstParsers::geminiEmptyResultUsesFrozenMessage()
{
    // A safety-blocked response: candidates exist but carry no image part, and one
    // candidate carries a huge inline blob that must not reach the diagnostic.
    const QByteArray huge = fakePng(200 * 1024);
    QJsonObject inlineData;
    inlineData.insert(QStringLiteral("mimeType"), QStringLiteral("application/octet-stream"));
    inlineData.insert(QStringLiteral("data"), b64(huge));
    QJsonObject blobPart;
    blobPart.insert(QStringLiteral("inlineData"), inlineData);

    QJsonObject textPart;
    textPart.insert(QStringLiteral("text"), QStringLiteral("I can't help with that"));
    QJsonArray parts;
    parts.append(textPart);
    parts.append(blobPart);
    QJsonObject content;
    content.insert(QStringLiteral("parts"), parts);
    QJsonObject candidate;
    candidate.insert(QStringLiteral("content"), content);
    candidate.insert(QStringLiteral("finishReason"), QStringLiteral("SAFETY"));
    QJsonArray candidates;
    candidates.append(candidate);
    const QJsonObject body = QJsonObject{ { QStringLiteral("candidates"), candidates },
                                         { QStringLiteral("modelVersion"), QStringLiteral("gemini-3-pro") } };

    ResponseContext context = testContext();
    context.limits = smallLimits();
    const ParseResult result = parseGeminiResponse(json(body), QStringLiteral("application/json"), context);
    QVERIFY(!result.ok());
    QCOMPARE(result.error,
             QStringLiteral("Gemini 返回中没有可用的图片，可能被安全策略拦截或模型不支持生图"));
    QVERIFY2(result.diagnosticJson.contains("<omitted>"), "inline payload must be replaced");
    QVERIFY2(!result.diagnosticJson.contains(QString::fromLatin1(huge.toBase64()).left(64)),
             "the blob must not be present even partially");
    // Everything needed to diagnose the block stays readable.
    QVERIFY2(result.diagnosticJson.contains("SAFETY"), qPrintable(result.diagnosticJson));
    QVERIFY2(result.diagnosticJson.contains("gemini-3-pro"), qPrintable(result.diagnosticJson));
    QVERIFY(result.diagnosticJson.size() < huge.size());
}

void TstParsers::compactRawKeepsSmallBodies()
{
    QJsonObject item;
    item.insert(QStringLiteral("b64_json"), b64(fakePng(20)));
    item.insert(QStringLiteral("revised_prompt"), QStringLiteral("short"));
    QJsonArray data;
    data.append(item);
    const QJsonObject body = QJsonObject{ { QStringLiteral("data"), data },
                                         { QStringLiteral("created"), 1700000000 } };

    const QJsonObject compact = compactRawResponse(body);
    // Under the retention budget the body is kept verbatim, images included.
    QCOMPARE(compact, body);
}

void TstParsers::compactRawDropsImageBytes()
{
    const QByteArray huge = fakePng(120 * 1024);
    QJsonObject item;
    item.insert(QStringLiteral("b64_json"), b64(huge));
    item.insert(QStringLiteral("url"), QStringLiteral("https://cdn.example/x.png"));
    QJsonArray data;
    data.append(item);
    QJsonObject body;
    body.insert(QStringLiteral("data"), data);
    body.insert(QStringLiteral("created"), 1700000000);
    QJsonObject nested;
    nested.insert(QStringLiteral("blob"), QString::fromLatin1(huge.toBase64()));
    body.insert(QStringLiteral("debug"), nested);

    const QJsonObject compact = compactRawResponse(body, 4096);
    QVERIFY2(!compact.contains(QStringLiteral("debug")), "an oversized value must be dropped wholesale");
    QVERIFY2(compact.value(QStringLiteral("created")).toInt() == 1700000000, "small fields survive");
    QCOMPARE(compact.value(QStringLiteral("_omitted_large_fields")).toBool(), true);

    const QJsonArray kept = compact.value(QStringLiteral("data")).toArray();
    QCOMPARE(kept.size(), 1);
    const QJsonObject keptItem = kept.at(0).toObject();
    QVERIFY2(!keptItem.contains(QStringLiteral("b64_json")), "image bytes must never be retained");
    QCOMPARE(keptItem.value(QStringLiteral("url")).toString(), QStringLiteral("https://cdn.example/x.png"));

    const QByteArray serialized = QJsonDocument(compact).toJson(QJsonDocument::Compact);
    QVERIFY2(!serialized.contains(huge.toBase64().left(64)), qPrintable(QStringLiteral("kept %1 bytes")
                                                                            .arg(serialized.size())));
}

QTEST_GUILESS_MAIN(TstParsers)
#include "tst_parsers.moc"
