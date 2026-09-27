// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
//
// Request-side protocol rules (SPEC 5). These bodies are contracts with third
// parties: a wrong field name or a dropped reference is an upstream 400 that the
// user cannot interpret, so each frozen rule gets a direct assertion.

#include <QBuffer>
#include <QImage>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "oic/core/limits.h"
#include "oic/protocol/imageprotocol.h"

using namespace oic::protocol;

namespace {

// Encodes a real image with Qt's own writer and pads to the requested size. The
// reference normalizer checks that the payload really is an image, so fixtures have
// to be real images too; padding after the encoded blob is invisible to a header
// probe and keeps the multi-megabyte cap tests cheap.
QByteArray fakeImagePayload(const QString &mime, int size, char fill = 'a')
{
    const char *format = "PNG";
    if (mime.contains(QLatin1String("jpeg"))) {
        format = "JPEG";
    } else if (mime.contains(QLatin1String("gif"))) {
        format = "GIF";
    }
    QImage image(8, 4, QImage::Format_ARGB32);
    image.fill(QColor(1, 2, 3, 255));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, format);
    if (!writer.write(image)) {
        // Callers assert on the normalized result, so an empty fixture surfaces as a
        // rejected reference rather than silently testing nothing.
        return {};
    }
    if (bytes.size() < size) {
        bytes += QByteArray(size - bytes.size(), fill);
    }
    return bytes;
}

ReferenceImage dataRef(const QString &mime, int payloadBytes, char fill = 'a')
{
    ReferenceImage ref;
    ref.dataUrl = QStringLiteral("data:%1;base64,").arg(mime)
                  + QString::fromLatin1(fakeImagePayload(mime, payloadBytes, fill).toBase64());
    return ref;
}

ReferenceImage urlRef(const QString &url)
{
    ReferenceImage ref;
    ref.url = url;
    return ref;
}

ImageRequest baseRequest()
{
    ImageRequest request;
    request.model = QStringLiteral("gpt-image-2");
    request.prompt = QStringLiteral("a cat");
    request.size = QStringLiteral("1024x1024");
    request.n = 1;
    return request;
}

QString headerValue(const QList<oic::net::Header> &headers, const char *name)
{
    for (const oic::net::Header &header : headers) {
        if (header.name.compare(name, Qt::CaseInsensitive) == 0) {
            return QString::fromUtf8(header.value);
        }
    }
    return QString();
}

}  // namespace

class TstProtocol : public QObject
{
    Q_OBJECT

private slots:
    void resolveProtocolExplicitAndAuto_data();
    void resolveProtocolExplicitAndAuto();

    void editFileFieldName_data();
    void editFileFieldName();

    void openAiGenerationBody();
    void openAiOptionsRejected_data();
    void openAiOptionsRejected();
    void openAiEditFieldsAreStringsInOrder();

    void grokGenerationBody();
    void grokEditBodyShapes();
    void grokOptionsRejected_data();
    void grokOptionsRejected();

    void geminiBodyDefaultsAndEdits();
    void geminiOptionsRejected_data();
    void geminiOptionsRejected();
    void geminiEndpointConstruction_data();
    void geminiEndpointConstruction();

    void dataUrlHelpers();
    void mimeFromFormat_data();
    void mimeFromFormat();

    void referenceValidation_data();
    void referenceValidation();
    void referenceCumulativeCap();

    void multipartBytes();
    void multipartAvoidsBoundaryCollision();

    void prepareWiresProtocolAndAuth_data();
    void prepareWiresProtocolAndAuth();
};

void TstProtocol::resolveProtocolExplicitAndAuto_data()
{
    QTest::addColumn<QString>("hint");
    QTest::addColumn<QString>("model");
    QTest::addColumn<QString>("base");
    QTest::addColumn<QString>("expected");
    QTest::addColumn<bool>("errorExpected");

    QTest::newRow("explicit openai wins over grok model") << "openai" << "grok-imagine-image" << "https://x"
                                                          << "openai" << false;
    QTest::newRow("explicit grok wins over gemini model") << "GROK" << "gemini-3-pro" << "https://x" << "grok" << false;
    QTest::newRow("explicit gemin i") << " gemini " << "anything" << "https://x" << "gemini" << false;
    QTest::newRow("invalid hint") << "anthropic" << "x" << "https://x" << "" << true;
    QTest::newRow("auto grok by model") << "auto" << "GROK-IMAGINE-IMAGE-PRO" << "https://api.openai.com/v1"
                                        << "grok" << false;
    QTest::newRow("auto grok by host") << "auto" << "some-image-model" << "https://api.x.ai/v1" << "grok" << false;
    QTest::newRow("auto gemini by model") << "auto" << "gemini-3-pro-image-preview" << "https://x" << "gemini" << false;
    QTest::newRow("auto gemini by host") << "auto" << "" << "https://generativelanguage.googleapis.com/v1beta"
                                        << "gemini" << false;
    QTest::newRow("auto openai fallback") << "auto" << "dall-e-3" << "https://api.openai.com/v1" << "openai" << false;
    QTest::newRow("empty hint means auto") << "" << "grok-imagine" << "https://x" << "grok" << false;
}

void TstProtocol::resolveProtocolExplicitAndAuto()
{
    QFETCH(const QString, hint);
    QFETCH(const QString, model);
    QFETCH(const QString, base);
    QFETCH(const QString, expected);
    QFETCH(const bool, errorExpected);

    QString error;
    const Protocol protocol = resolveProtocol(hint, model, base, &error);
    if (errorExpected) {
        QVERIFY2(!error.isEmpty(), qPrintable(error));
        return;
    }
    QCOMPARE(protocolName(protocol), expected);
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

void TstProtocol::editFileFieldName_data()
{
    QTest::addColumn<QString>("model");
    QTest::addColumn<int>("count");
    QTest::addColumn<QString>("field");

    // The old CLI compared the model verbatim, so GPT-Image-2 sent `image` and the
    // provider silently used only one of two references.
    QTest::newRow("gpt-image-2 single") << "gpt-image-2" << 1 << "image[]";
    QTest::newRow("GPT-Image-2 uppercase single") << "GPT-Image-2" << 1 << "image[]";
    QTest::newRow("  gpt-image-2 padded") << "  gpt-image-2  " << 1 << "image[]";
    QTest::newRow("gpt-image-2-mini") << "gpt-image-2-mini" << 1 << "image[]";
    QTest::newRow("gpt-image-2.5-flare") << "GPT-Image-2.5-Flare" << 1 << "image[]";
    QTest::newRow("gpt-image-3 not the same family") << "gpt-image-3" << 1 << "image";
    QTest::newRow("other model single") << "dall-e-3" << 1 << "image";
    QTest::newRow("other model two images") << "dall-e-3" << 2 << "image[]";
    QTest::newRow("gpt-image-25 no separator") << "gpt-image-25" << 1 << "image";
}

void TstProtocol::editFileFieldName()
{
    QFETCH(const QString, model);
    QFETCH(const int, count);
    QFETCH(const QString, field);
    QCOMPARE(openAiEditFileField(model, count), field);
}

void TstProtocol::openAiGenerationBody()
{
    ImageRequest request = baseRequest();
    request.quality = QStringLiteral("HIGH");
    request.n = 3;
    QString error;
    const QJsonObject body = buildOpenAiGenerationBody(request, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("gpt-image-2"));
    QCOMPARE(body.value(QStringLiteral("prompt")).toString(), QStringLiteral("a cat"));
    QCOMPARE(body.value(QStringLiteral("size")).toString(), QStringLiteral("1024x1024"));
    QCOMPARE(body.value(QStringLiteral("n")).toInt(), 3);
    // Options are lower-cased before sending, matching the old payload_text_lower.
    QCOMPARE(body.value(QStringLiteral("quality")).toString(), QStringLiteral("high"));
    // SPEC 5.5: the OpenAI path never sends response_format; results are parsed
    // tolerantly instead (SPEC 7.1).
    QVERIFY(!body.contains(QStringLiteral("response_format")));
    QVERIFY(!body.contains(QStringLiteral("background")));
    QVERIFY(!body.contains(QStringLiteral("output_compression")));
}

void TstProtocol::openAiOptionsRejected_data()
{
    QTest::addColumn<QString>("model");
    QTest::addColumn<QString>("background");
    QTest::addColumn<QString>("outputFormat");
    QTest::addColumn<int>("compression");
    QTest::addColumn<bool>("ok");

    QTest::newRow("transparent on gpt-image-2") << "gpt-image-2" << "transparent" << "" << -1 << false;
    QTest::newRow("transparent on other model") << "dall-e-3" << "transparent" << "" << -1 << true;
    QTest::newRow("opaque on gpt-image-2") << "gpt-image-2" << "opaque" << "" << -1 << true;
    QTest::newRow("compression with jpeg") << "gpt-image-2" << "" << "jpeg" << 80 << true;
    QTest::newRow("compression with webp") << "gpt-image-2" << "" << "WEBP" << 0 << true;
    QTest::newRow("compression with png") << "gpt-image-2" << "" << "png" << 80 << false;
    QTest::newRow("compression above range") << "gpt-image-2" << "" << "jpeg" << 101 << false;
}

void TstProtocol::openAiOptionsRejected()
{
    QFETCH(const QString, model);
    QFETCH(const QString, background);
    QFETCH(const QString, outputFormat);
    QFETCH(const int, compression);
    QFETCH(const bool, ok);

    ImageRequest request = baseRequest();
    request.model = model;
    request.background = background;
    request.outputFormat = outputFormat;
    request.outputCompression = compression;

    QString error;
    const QJsonObject body = buildOpenAiGenerationBody(request, &error);
    if (ok) {
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!body.isEmpty());
        if (compression >= 0) {
            QCOMPARE(body.value(QStringLiteral("output_compression")).toInt(), compression);
        }
    } else {
        QVERIFY2(!error.isEmpty(), "expected the request to be refused");
        QVERIFY(body.isEmpty());
    }
}

void TstProtocol::openAiEditFieldsAreStringsInOrder()
{
    ImageRequest request = baseRequest();
    request.editMode = true;
    request.n = 2;
    request.quality = QStringLiteral("high");

    QString error;
    const QList<FormPair> fields = buildOpenAiEditFields(request, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QStringList names;
    for (const FormPair &field : fields) {
        names.append(field.name);
    }
    // Multipart order is visible to the provider: model/prompt/size/n come first,
    // exactly as the previous implementation built its dict.
    QCOMPARE(names, QStringList({ QStringLiteral("model"), QStringLiteral("prompt"), QStringLiteral("size"),
                                 QStringLiteral("n"), QStringLiteral("quality") }));

    for (const FormPair &field : fields) {
        // n must be text here; a numeric form field is how the old code produced a
        // provider-side parse error.
        QVERIFY2(!field.value.contains(QLatin1Char('\n')), field.name.toUtf8().constData());
    }
    QVERIFY(fields.at(3).value == QStringLiteral("2"));
}

void TstProtocol::grokGenerationBody()
{
    ImageRequest request = baseRequest();
    request.model = QStringLiteral("grok-imagine-image");
    request.size = QStringLiteral("1920x1072");
    QString error;
    QJsonObject body = buildGrokGenerationBody(request, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    // Derived from size: 1920x1072 is 16:9 and the long edge is above 1536.
    QCOMPARE(body.value(QStringLiteral("aspect_ratio")).toString(), QStringLiteral("16:9"));
    QCOMPARE(body.value(QStringLiteral("resolution")).toString(), QStringLiteral("2k"));
    QCOMPARE(body.value(QStringLiteral("n")).toInt(), 1);
    QVERIFY(!body.contains(QStringLiteral("response_format")));

    request.responseFormat = QStringLiteral("b64_json");
    body = buildGrokGenerationBody(request, &error);
    QCOMPARE(body.value(QStringLiteral("response_format")).toString(), QStringLiteral("b64_json"));

    // Explicit values beat derivation.
    request.aspectRatio = QStringLiteral("1:1");
    request.resolution = QStringLiteral("1k");
    body = buildGrokGenerationBody(request, &error);
    QCOMPARE(body.value(QStringLiteral("aspect_ratio")).toString(), QStringLiteral("1:1"));
    QCOMPARE(body.value(QStringLiteral("resolution")).toString(), QStringLiteral("1k"));
}

void TstProtocol::grokEditBodyShapes()
{
    ImageRequest request = baseRequest();
    request.model = QStringLiteral("grok-imagine-image");
    request.editMode = true;
    request.size = QStringLiteral("1024x1024");

    const QList<ReferenceImage> one{ dataRef(QStringLiteral("image/png"), 12) };
    const QList<ReferenceImage> two{ dataRef(QStringLiteral("image/png"), 12), urlRef(QStringLiteral("https://x/y.png")) };

    QString error;
    QJsonObject single = buildGrokEditBody(request, one, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(single.contains(QStringLiteral("image")));
    QVERIFY(!single.contains(QStringLiteral("images")));
    // One reference and no explicit choice: framing is left to the provider.
    QVERIFY(!single.contains(QStringLiteral("aspect_ratio")));
    QVERIFY(!single.contains(QStringLiteral("resolution")));
    QCOMPARE(single.value(QStringLiteral("image")).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("image_url"));

    QJsonObject multi = buildGrokEditBody(request, two, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!multi.contains(QStringLiteral("image")));
    QCOMPARE(multi.value(QStringLiteral("images")).toArray().size(), 2);
    // Two or more references forces the derived values into the body.
    QVERIFY(multi.contains(QStringLiteral("aspect_ratio")));
    QVERIFY(multi.contains(QStringLiteral("resolution")));

    const QList<ReferenceImage> four{ one.at(0), one.at(0), one.at(0), one.at(0) };
    error.clear();
    QVERIFY(buildGrokEditBody(request, four, &error).isEmpty());
    QVERIFY2(error.contains(QString::fromUtf8("3 张")), qPrintable(error));

    ReferenceImage broken;
    error.clear();
    QVERIFY(buildGrokEditBody(request, QList<ReferenceImage>{ broken }, &error).isEmpty());
    QVERIFY2(error.contains(QStringLiteral("1")), qPrintable(error));
}

void TstProtocol::grokOptionsRejected_data()
{
    QTest::addColumn<QString>("aspectRatio");
    QTest::addColumn<QString>("resolution");
    QTest::addColumn<QString>("responseFormat");

    QTest::newRow("bad ratio") << "7:3" << "" << "";
    QTest::newRow("bad resolution") << "" << "4k" << "";
    QTest::newRow("bad response format") << "" << "" << "data_uri";
}

void TstProtocol::grokOptionsRejected()
{
    QFETCH(const QString, aspectRatio);
    QFETCH(const QString, resolution);
    QFETCH(const QString, responseFormat);

    ImageRequest request = baseRequest();
    request.model = QStringLiteral("grok-imagine-image");
    request.aspectRatio = aspectRatio;
    request.resolution = resolution;
    request.responseFormat = responseFormat;

    QString error;
    QVERIFY(buildGrokGenerationBody(request, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    if (!aspectRatio.isEmpty()) {
        // The message has to list the accepted values; users hit this by pasting a
        // ratio from another provider's docs.
        QVERIFY2(error.contains(QStringLiteral("16:9")) && error.contains(QStringLiteral("9:16")), qPrintable(error));
    }
}

void TstProtocol::geminiBodyDefaultsAndEdits()
{
    ImageRequest request = baseRequest();
    request.model = QStringLiteral("gemini-3-pro-image-preview");
    QString error;
    const QJsonObject body = buildGeminiBody(request, {}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const QJsonObject config = body.value(QStringLiteral("generationConfig")).toObject();
    const QJsonArray expectedModalities{ QStringLiteral("TEXT"), QStringLiteral("IMAGE") };
    QCOMPARE(config.value(QStringLiteral("responseModalities")).toArray(), expectedModalities);
    const QJsonObject imageConfig = config.value(QStringLiteral("imageConfig")).toObject();
    QCOMPARE(imageConfig.value(QStringLiteral("aspectRatio")).toString(), QStringLiteral("1:1"));
    QCOMPARE(imageConfig.value(QStringLiteral("imageSize")).toString(), QStringLiteral("1K"));

    const QJsonArray parts = body.value(QStringLiteral("contents")).toArray().at(0).toObject()
                                 .value(QStringLiteral("parts")).toArray();
    QCOMPARE(parts.size(), 1);
    QCOMPARE(parts.at(0).toObject().value(QStringLiteral("text")).toString(), QStringLiteral("a cat"));

    // resolution doubles as imageSize and is upper-cased on the way in.
    request.resolution = QStringLiteral("2k");
    QCOMPARE(buildGeminiBody(request, {}, &error)
                 .value(QStringLiteral("generationConfig"))
                 .toObject()
                 .value(QStringLiteral("imageConfig"))
                 .toObject()
                 .value(QStringLiteral("imageSize"))
                 .toString(),
             QStringLiteral("2K"));

    // Edit: references become inlineData parts in the same call -- Gemini has no
    // separate edits endpoint.
    request.editMode = true;
    const QByteArray jpegBytes = fakeImagePayload(QStringLiteral("image/jpeg"), 9);
    ReferenceImage inlineSource;
    inlineSource.dataUrl = dataUrlFromBytes(jpegBytes, QStringLiteral("image/jpeg"));
    const QList<ReferenceImage> refs = normalizeReferences({ inlineSource }, 16, &error);
    QVERIFY2(!refs.isEmpty(), qPrintable(error));
    const QJsonObject edited = buildGeminiBody(request, refs, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QJsonArray editedParts = edited.value(QStringLiteral("contents")).toArray().at(0).toObject()
                                       .value(QStringLiteral("parts")).toArray();
    QCOMPARE(editedParts.size(), 2);
    const QJsonObject inlineData = editedParts.at(1).toObject().value(QStringLiteral("inlineData")).toObject();
    QCOMPARE(inlineData.value(QStringLiteral("mimeType")).toString(), QStringLiteral("image/jpeg"));
    QCOMPARE(QByteArray::fromBase64(inlineData.value(QStringLiteral("data")).toString().toLatin1()), jpegBytes);

    // A url-only reference has no bytes to inline.
    error.clear();
    QVERIFY(buildGeminiBody(request, { urlRef(QStringLiteral("https://x/y.png")) }, &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void TstProtocol::geminiOptionsRejected_data()
{
    QTest::addColumn<QString>("aspectRatio");
    QTest::addColumn<QString>("resolution");

    QTest::newRow("ratio not offered") << "5:4" << "";
    QTest::newRow("size not offered") << "" << "8K";
    QTest::newRow("21:9 is offered") << "21:9" << "4K";
}

void TstProtocol::geminiOptionsRejected()
{
    QFETCH(const QString, aspectRatio);
    QFETCH(const QString, resolution);

    ImageRequest request = baseRequest();
    request.model = QStringLiteral("gemini-3-pro-image-preview");
    request.aspectRatio = aspectRatio;
    request.resolution = resolution;

    QString error;
    const QJsonObject body = buildGeminiBody(request, {}, &error);
    const bool offered = aspectRatio.isEmpty() || geminiAspectRatios().contains(aspectRatio);
    const bool sizeOk = resolution.isEmpty() || resolution.toUpper() == QStringLiteral("1K")
                        || resolution.toUpper() == QStringLiteral("2K") || resolution.toUpper() == QStringLiteral("4K");
    if (offered && sizeOk) {
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!body.isEmpty());
    } else {
        QVERIFY2(!error.isEmpty(), "unsupported Gemini option must be refused locally");
        QVERIFY(body.isEmpty());
    }
}

void TstProtocol::geminiEndpointConstruction_data()
{
    QTest::addColumn<QString>("base");
    QTest::addColumn<QString>("model");
    QTest::addColumn<QString>("expected");

    QTest::newRow("bare host gets v1beta") << "https://generativelanguage.googleapis.com" << "gemini-3-pro"
        << "https://generativelanguage.googleapis.com/v1beta/models/gemini-3-pro:generateContent";
    QTest::newRow("already versioned") << "https://x.dev/v1" << "gemini-3"
        << "https://x.dev/v1/models/gemini-3:generateContent";
    QTest::newRow("v1alpha kept") << "https://x.dev/v1alpha/" << "gemini-3"
        << "https://x.dev/v1alpha/models/gemini-3:generateContent";
    QTest::newRow("models/ prefix stripped") << "https://x.dev/v1beta" << "models/gemini-3"
        << "https://x.dev/v1beta/models/gemini-3:generateContent";
    QTest::newRow("full endpoint passthrough") << "https://x.dev/v1/models/gemini-3:generateContent" << ""
        << "https://x.dev/v1/models/gemini-3:generateContent";
    QTest::newRow("unknown last segment appends") << "https://x.dev/proxy" << "gemini-3"
        << "https://x.dev/proxy/v1beta/models/gemini-3:generateContent";
    QTest::newRow("tilde and dot kept unescaped") << "https://x.dev/v1" << "gem~ini.3"
        << "https://x.dev/v1/models/gem~ini.3:generateContent";
    QTest::newRow("space escaped") << "https://x.dev/v1" << "gem ini"
        << "https://x.dev/v1/models/gem%20ini:generateContent";
    // A slash inside the model name must not be able to address another resource.
    QTest::newRow("slash escaped") << "https://x.dev/v1" << "a/b/../c"
        << "https://x.dev/v1/models/a%2Fb%2F..%2Fc:generateContent";
}

void TstProtocol::geminiEndpointConstruction()
{
    QFETCH(const QString, base);
    QFETCH(const QString, model);
    QFETCH(const QString, expected);

    QString error;
    const QString built = geminiGenerateEndpoint(base, model, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(built, expected);
}

void TstProtocol::dataUrlHelpers()
{
    const QByteArray payload("hello world");
    const QString url = dataUrlFromBytes(payload, QStringLiteral("image/png"));
    QVERIFY(url.startsWith(QStringLiteral("data:image/png;base64,")));

    QString mime;
    QString encoded;
    QString error;
    QVERIFY(parseDataUrl(url, &mime, &encoded, &error));
    QCOMPARE(mime, QStringLiteral("image/png"));
    QCOMPARE(QByteArray::fromBase64(encoded.toLatin1()), payload);
    QCOMPARE(dataUrlPayloadSize(url), static_cast<qint64>(payload.size()));

    // Not base64: the old code used these to carry inline video, which is descoped.
    error.clear();
    QVERIFY(!parseDataUrl(QStringLiteral("data:image png,abc"), &mime, &encoded, &error));
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(!parseDataUrl(QStringLiteral("https://x/y.png"), &mime, &encoded, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(dataUrlPayloadSize(QStringLiteral("not a data url at all")), 0LL);
}

void TstProtocol::mimeFromFormat_data()
{
    QTest::addColumn<QString>("format");
    QTest::addColumn<QString>("mime");

    QTest::newRow("jpeg") << "jpeg" << "image/jpeg";
    QTest::newRow("JPG") << "JPG" << "image/jpeg";
    QTest::newRow("webp") << " webp " << "image/webp";
    QTest::newRow("gif") << "gif" << "image/gif";
    QTest::newRow("png") << "png" << "image/png";
    // Unknown values fall back to png, matching the previous implementation.
    QTest::newRow("empty") << "" << "image/png";
    QTest::newRow("nonsense") << "tiff" << "image/png";
}

void TstProtocol::mimeFromFormat()
{
    QFETCH(const QString, format);
    QFETCH(const QString, mime);
    QCOMPARE(imageMimeFromFormat(format), mime);
}

void TstProtocol::referenceValidation_data()
{
    QTest::addColumn<QList<ReferenceImage>>("input");
    QTest::addColumn<QString>("needle");

    QList<ReferenceImage> empty;
    QTest::newRow("no references") << empty << QString::fromUtf8("参考图");

    QList<ReferenceImage> nonImageMime;
    nonImageMime.append(dataRef(QStringLiteral("image/png"), 4));
    nonImageMime[0].dataUrl = QStringLiteral("data:text/html;base64,") + QString::fromLatin1(QByteArray(4, 'a').toBase64());
    QTest::newRow("html mime in data url") << nonImageMime << QString::fromUtf8("image/*");

    // Payload is valid base64, so only a content check can catch this one: without
    // it the bytes get filed as a .png nothing can open.
    QList<ReferenceImage> htmlAsBase64;
    ReferenceImage sneaky;
    sneaky.dataUrl = QStringLiteral("data:image/png;base64,")
                     + QString::fromLatin1(QByteArray("<!DOCTYPE html><html>504 Gateway Time-out</html>").toBase64());
    htmlAsBase64.append(sneaky);
    QTest::newRow("html bytes claim to be png") << htmlAsBase64 << QString::fromUtf8("图片");

    // Raw HTML where base64 was expected: strict decoding rejects it outright
    // (SPEC 7.3 step 3), where a lax decoder would have silently produced bytes.
    QList<ReferenceImage> rawHtml;
    ReferenceImage lax;
    lax.dataUrl = QStringLiteral("data:image/png;base64,<!DOCTYPE html><html>x</html>");
    rawHtml.append(lax);
    QTest::newRow("unencoded html in base64 slot") << rawHtml << QString();

    QList<ReferenceImage> notBase64;
    ReferenceImage plain;
    plain.dataUrl = QStringLiteral("data:image/png,rawtext");
    notBase64.append(plain);
    QTest::newRow("missing base64 marker") << notBase64 << "base64";

    QList<ReferenceImage> blank;
    blank.append(ReferenceImage{});
    QTest::newRow("empty reference") << blank << QString::fromUtf8("不能为空");
}

void TstProtocol::referenceValidation()
{
    QFETCH(const QList<ReferenceImage>, input);
    QFETCH(const QString, needle);

    QString error;
    const QList<ReferenceImage> out = normalizeReferences(input, oic::limits::kMaxEditReferenceImages, &error);
    QVERIFY2(out.isEmpty(), "invalid references must not normalize");
    QVERIFY(!error.isEmpty());
    if (!needle.isEmpty()) {
        QVERIFY2(error.contains(needle), qPrintable(QStringLiteral("wanted '%1' in '%2'").arg(needle, error)));
    }

    // Too many images has its own message; check it with a valid payload count.
    if (input.size() <= 1 && needle == QString::fromUtf8("参考图")) {
        QList<ReferenceImage> tooMany;
        for (int i = 0; i <= oic::limits::kMaxEditReferenceImages; ++i) {
            tooMany.append(dataRef(QStringLiteral("image/png"), 4));
        }
        QString manyError;
        QVERIFY(normalizeReferences(tooMany, oic::limits::kMaxEditReferenceImages, &manyError).isEmpty());
        QVERIFY2(manyError.contains(QString::fromUtf8("最多支持")), qPrintable(manyError));
    }
}

void TstProtocol::referenceCumulativeCap()
{
    // Per image stays under 10 MiB; four of them blow through the 30 MiB total. This
    // is why the check accumulates rather than testing each one.
    QList<ReferenceImage> many;
    const int each = 9 * 1024 * 1024;
    for (int i = 0; i < 4; ++i) {
        many.append(dataRef(QStringLiteral("image/png"), each));
    }
    QString error;
    QVERIFY(normalizeReferences(many, oic::limits::kMaxEditReferenceImages, &error).isEmpty());
    QVERIFY2(error.contains(QString::fromUtf8("总大小")), qPrintable(error));

    // A single oversized image hits the per-image cap instead.
    QString singleError;
    QVERIFY(normalizeReferences({ dataRef(QStringLiteral("image/png"), 11 * 1024 * 1024) },
                                oic::limits::kMaxEditReferenceImages, &singleError)
                .isEmpty());
    QVERIFY2(singleError.contains(QString::fromUtf8("参考图")), qPrintable(singleError));
}

void TstProtocol::multipartBytes()
{
    ImageRequest request = baseRequest();
    request.editMode = true;
    request.n = 1;
    QString error;
    // One payload, shared by the request and the expectation: encoding twice and
    // comparing would only test that the encoder is deterministic.
    const QByteArray payloadBytes = fakeImagePayload(QStringLiteral("image/png"), 40, 'x');
    ReferenceImage source;
    source.dataUrl = dataUrlFromBytes(payloadBytes, QStringLiteral("image/png"));
    const QList<ReferenceImage> refs = normalizeReferences({ source }, 16, &error);
    QVERIFY2(refs.size() == 1, qPrintable(error));
    const QList<FormPair> fields = buildOpenAiEditFields(request, &error);

    QString contentType;
    const QByteArray body = buildMultipartBody(fields, openAiEditFileField(request.model, 1), refs, &contentType, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(contentType.startsWith(QStringLiteral("multipart/form-data; boundary=")), qPrintable(contentType));
    const QByteArray boundary = contentType.section(QLatin1Char('='), 1).toLatin1();

    const QByteArray expected = QByteArray("--") + boundary + "\r\n"
        + "Content-Disposition: form-data; name=\"model\"\r\n\r\ngpt-image-2\r\n"
          "--" + boundary + "\r\n"
          "Content-Disposition: form-data; name=\"prompt\"\r\n\r\na cat\r\n"
          "--" + boundary + "\r\n"
          "Content-Disposition: form-data; name=\"size\"\r\n\r\n1024x1024\r\n"
          "--" + boundary + "\r\n"
          "Content-Disposition: form-data; name=\"n\"\r\n\r\n1\r\n"
          "--" + boundary + "\r\n"
          "Content-Disposition: form-data; name=\"image[]\"; filename=\"reference-image-1.png\"\r\n"
          "Content-Type: image/png\r\n\r\n"
          + payloadBytes + "\r\n"
          + "--" + boundary + "--\r\n";
    QCOMPARE(body, expected);
}

void TstProtocol::multipartAvoidsBoundaryCollision()
{
    // Embed the first candidate boundary in a file name so a fixed-string
    // implementation would produce a body the provider truncates at the fake part.
    QString error;
    ReferenceImage hostile;
    hostile.name = QStringLiteral("x__oic_boundary_0.png");
    hostile.dataUrl = dataUrlFromBytes(fakeImagePayload(QStringLiteral("image/png"), 40, 'z'),
                                    QStringLiteral("image/png"));
    const QList<ReferenceImage> refs = normalizeReferences({ hostile }, 16, &error);
    QVERIFY2(refs.size() == 1, qPrintable(error));

    QString contentType;
    const QByteArray body = buildMultipartBody({ { QStringLiteral("model"), QStringLiteral("gpt-image-2") } },
                                              QStringLiteral("image"), refs, &contentType, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QByteArray boundary = contentType.section(QLatin1Char('='), 1).toLatin1();
    QVERIFY2(boundary != QByteArrayLiteral("__oic_boundary_0"), "boundary collided with payload content");
    QVERIFY(body.contains(boundary));
}

void TstProtocol::prepareWiresProtocolAndAuth_data()
{
    QTest::addColumn<QString>("base");
    QTest::addColumn<QString>("model");
    QTest::addColumn<QString>("protocolHint");
    QTest::addColumn<QString>("expectedRoute");
    QTest::addColumn<QString>("expectedAuth");
    QTest::addColumn<QString>("expectedContentType");

    QTest::newRow("openai generate") << "https://api.openai.com/v1" << "gpt-image-2" << "" << "images/generations"
                                     << "Bearer k" << "application/json";
    QTest::newRow("grok generate") << "https://api.x.ai/v1" << "grok-imagine-image" << "" << "images/generations"
                                   << "Bearer k" << "application/json";
    QTest::newRow("gemini generate") << "https://generativelanguage.googleapis.com" << "gemini-3-pro" << ""
                                     << "images/generations" << "k" << "application/json";
    QTest::newRow("openai edit is multipart") << "https://api.openai.com/v1" << "gpt-image-2" << "openai"
                                              << "images/edits" << "Bearer k" << "multipart";
}

void TstProtocol::prepareWiresProtocolAndAuth()
{
    QFETCH(const QString, base);
    QFETCH(const QString, model);
    QFETCH(const QString, protocolHint);
    QFETCH(const QString, expectedRoute);
    QFETCH(const QString, expectedAuth);
    QFETCH(const QString, expectedContentType);

    ImageRequest request;
    request.model = model;
    request.prompt = QStringLiteral("a cat");
    request.size = QStringLiteral("1024x1024");
    request.editMode = expectedContentType == QStringLiteral("multipart");
    if (request.editMode) {
        QString refError;
        request.references = normalizeReferences({ dataRef(QStringLiteral("image/png"), 5) }, 16, &refError);
        QVERIFY2(!request.references.isEmpty(), qPrintable(refError));
    }

    QString error;
    const OutgoingRequest out = prepare(base, QStringLiteral("  k  "), protocolHint, request, &error);
    QVERIFY2(out.ok, qPrintable(error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(oic::core::routePath(out.route), expectedRoute);
    QCOMPARE(headerValue(out.headers, out.absoluteUrl.isEmpty() ? "Authorization" : "x-goog-api-key"), expectedAuth);
    const QString contentType = headerValue(out.headers, "Content-Type");
    if (expectedContentType == QStringLiteral("multipart")) {
        QVERIFY2(contentType.startsWith(QStringLiteral("multipart/form-data; boundary=")), qPrintable(contentType));
        // The API key must never appear in a URL for the gemini path -- that is the
        // whole reason x-goog-api-key is frozen in SPEC 5.6.
    } else {
        QCOMPARE(contentType, QStringLiteral("application/json"));
    }
    if (!out.absoluteUrl.isEmpty()) {
        QVERIFY2(!out.absoluteUrl.contains(QStringLiteral("k"), Qt::CaseSensitive)
                     || !out.absoluteUrl.contains(QStringLiteral("?key=")),
                 qPrintable(out.absoluteUrl));
        QVERIFY2(!out.absoluteUrl.contains(QStringLiteral("?key=")), qPrintable(out.absoluteUrl));
    }
}

QTEST_GUILESS_MAIN(TstProtocol)
#include "tst_protocol.moc"
