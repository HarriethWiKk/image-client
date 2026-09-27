// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/protocol/imageprotocol.h"

#include <QJsonArray>
#include <QJsonDocument>

#include "oic/core/decode.h"
#include "oic/core/limits.h"
#include "oic/core/models.h"
#include "oic/store/imageprobe.h"

namespace oic::protocol {
namespace {

QString trimmedLower(const QString &value)
{
    return value.trimmed().toLower();
}

// Error texts are part of the contract: the UI shows them verbatim and users quote
// them in issue reports, so they mirror the previous implementation.
void fail(QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
}

QString choices(const QStringList &values)
{
    QStringList sorted = values;
    sorted.sort();
    return sorted.join(QStringLiteral(", "));
}

}  // namespace

QString protocolName(Protocol protocol)
{
    switch (protocol) {
    case Protocol::Grok:
        return QStringLiteral("grok");
    case Protocol::Gemini:
        return QStringLiteral("gemini");
    case Protocol::OpenAI:
        break;
    }
    return QStringLiteral("openai");
}

QStringList protocolValues()
{
    return QStringList{ QStringLiteral("auto"), QStringLiteral("openai"), QStringLiteral("grok"),
                        QStringLiteral("gemini") };
}

QStringList geminiAspectRatios()
{
    return QStringList{ QStringLiteral("1:1"), QStringLiteral("2:3"), QStringLiteral("3:2"),
                        QStringLiteral("3:4"), QStringLiteral("4:3"), QStringLiteral("9:16"),
                        QStringLiteral("16:9"), QStringLiteral("21:9") };
}

QStringList geminiImageSizes()
{
    return QStringList{ QStringLiteral("1K"), QStringLiteral("2K"), QStringLiteral("4K") };
}

Protocol resolveProtocol(const QString &raw, const QString &model, const QString &baseUrl, QString *error)
{
    const QString value = trimmedLower(raw.isEmpty() ? QStringLiteral("auto") : raw);
    if (!protocolValues().contains(value)) {
        fail(error, QStringLiteral("protocol 必须是 auto、openai、grok 或 gemini"));
        return Protocol::OpenAI;
    }
    if (value == QLatin1String("openai")) {
        return Protocol::OpenAI;
    }
    if (value == QLatin1String("grok")) {
        return Protocol::Grok;
    }
    if (value == QLatin1String("gemini")) {
        return Protocol::Gemini;
    }

    // auto: model identity first, then the host, then the vendor's own domain.
    const QString base = trimmedLower(baseUrl);
    if (core::isGrokImageModel(model) || base.contains(QLatin1String("api.x.ai"))
        || base.contains(QLatin1String("x.ai"))) {
        return Protocol::Grok;
    }
    if (trimmedLower(model).startsWith(QLatin1String("gemini-"))
        || base.contains(QLatin1String("generativelanguage.googleapis.com"))) {
        return Protocol::Gemini;
    }
    return Protocol::OpenAI;
}

// ---------------------------------------------------------------- data URLs

bool parseDataUrl(const QString &value, QString *mime, QString *encoded, QString *error)
{
    const QString trimmed = value.trimmed();
    if (!trimmed.startsWith(QLatin1String("data:"))) {
        fail(error, QStringLiteral("不是 data URL"));
        return false;
    }
    const int comma = trimmed.indexOf(QLatin1Char(','));
    if (comma < 0) {
        fail(error, QStringLiteral("data URL 缺少数据部分"));
        return false;
    }
    const QString header = trimmed.mid(5, comma - 5);
    const QString payload = trimmed.mid(comma + 1);
    if (payload.isEmpty()) {
        fail(error, QStringLiteral("参考图数据格式不正确"));
        return false;
    }
    if (!header.contains(QLatin1String(";base64"))) {
        // A non-base64 data URL cannot be carried as a payload, and the old code
        // treated one as a video inline -- out of scope here.
        fail(error, QStringLiteral("参考图必须是 base64 data URL"));
        return false;
    }
    if (mime != nullptr) {
        *mime = header.section(QLatin1Char(';'), 0, 0).trimmed();
    }
    if (encoded != nullptr) {
        *encoded = payload;
    }
    return true;
}

QString dataUrlFromBytes(const QByteArray &bytes, const QString &mime)
{
    return QStringLiteral("data:%1;base64,%2").arg(mime, QString::fromLatin1(bytes.toBase64()));
}

qint64 dataUrlPayloadSize(const QString &value)
{
    QString encoded;
    if (!parseDataUrl(value, nullptr, &encoded, nullptr)) {
        return 0;
    }
    int padding = 0;
    while (padding < encoded.size() && encoded.at(encoded.size() - 1 - padding) == QLatin1Char('=')) {
        ++padding;
    }
    return qMax<qint64>(0, (static_cast<qint64>(encoded.size()) * 3) / 4 - padding);
}

QString imageMimeFromFormat(const QString &outputFormat)
{
    const QString value = trimmedLower(outputFormat);
    if (value == QLatin1String("jpg") || value == QLatin1String("jpeg")) {
        return QStringLiteral("image/jpeg");
    }
    if (value == QLatin1String("webp")) {
        return QStringLiteral("image/webp");
    }
    if (value == QLatin1String("gif")) {
        return QStringLiteral("image/gif");
    }
    return QStringLiteral("image/png");
}

// ---------------------------------------------------------------- references

QList<ReferenceImage> normalizeReferences(const QList<ReferenceImage> &input, int maxImages, QString *error)
{
    if (input.isEmpty()) {
        fail(error, QStringLiteral("图生图模式下请先上传参考图"));
        return {};
    }
    if (input.size() > maxImages) {
        fail(error, QStringLiteral("图生图最多支持 %1 张参考图").arg(maxImages));
        return {};
    }

    QList<ReferenceImage> out;
    qint64 totalBytes = 0;
    for (int index = 0; index < input.size(); ++index) {
        ReferenceImage image = input.at(index);
        const QString label = QStringLiteral("第 %1 张参考图").arg(index + 1);
        const bool hasDataUrl = !image.dataUrl.trimmed().isEmpty();
        const bool hasUrl = !image.url.trimmed().isEmpty();

        if (hasDataUrl) {
            QString mime;
            QString encoded;
            QString parseError;
            if (!parseDataUrl(image.dataUrl, &mime, &encoded, &parseError)) {
                fail(error, QStringLiteral("%1：%2").arg(label, parseError));
                return {};
            }
            if (!mime.isEmpty() && !mime.startsWith(QLatin1String("image/"))) {
                fail(error, QStringLiteral("参考图 Content-Type 必须是 image/*"));
                return {};
            }
            bool ok = false;
            QString bytesError;
            const QByteArray bytes = core::decodeBase64Limited(encoded, limits::kMaxReferenceImageBytes,
                                                               QStringLiteral("参考图"), &ok, &bytesError);
            if (!ok) {
                fail(error, QStringLiteral("%1：%2").arg(label, bytesError));
                return {};
            }
            // Strict base64 only proves the payload is base64. Without this check a
            // "data:image/png;base64,<html>" reference would be filed as a .png that
            // no decoder can open -- the same class of bug SPEC 7.3 exists for, one
            // step earlier in the pipeline.
            const store::ImageInfo probe = store::probeImage(bytes);
            if (!probe.recognized) {
                fail(error, QStringLiteral("%1 内容不是可识别的图片格式").arg(label));
                return {};
            }
            image.bytes = bytes;
            image.contentType = !mime.isEmpty()
                                    ? mime
                                    : (image.contentType.isEmpty() ? QStringLiteral("image/png") : image.contentType);
        } else if (hasUrl) {
            // The Grok path can hand a URL straight to the provider. Anything that
            // needs bytes (multipart, Gemini) rejects it further downstream.
            if (image.contentType.isEmpty()) {
                image.contentType = QStringLiteral("image/png");
            }
        } else {
            fail(error, QStringLiteral("参考图不能为空"));
            return {};
        }

        // Cumulative, not per image: three 12 MiB references are 36 MiB of upstream
        // upload even though no single one exceeds the 10 MiB per-image cap.
        totalBytes += image.bytes.size();
        if (totalBytes > limits::kMaxReferenceImagesBytes) {
            fail(error, QStringLiteral("参考图总大小超过 %1 字节限制").arg(limits::kMaxReferenceImagesBytes));
            return {};
        }
        if (image.name.trimmed().isEmpty()) {
            image.name = QStringLiteral("reference-image-%1.png").arg(index + 1);
        }
        out.append(image);
    }
    return out;
}

// ---------------------------------------------------------------- bodies

bool applyOpenAiOptions(QJsonObject *body, const ImageRequest &request, QString *error)
{
    const QString quality = trimmedLower(request.quality);
    const QString outputFormat = trimmedLower(request.outputFormat);
    const QString background = trimmedLower(request.background);
    const QString moderation = trimmedLower(request.moderation);

    if (!quality.isEmpty()) {
        body->insert(QStringLiteral("quality"), quality);
    }
    if (!outputFormat.isEmpty()) {
        body->insert(QStringLiteral("output_format"), outputFormat);
    }
    if (!background.isEmpty()) {
        if (core::isGptImage2(request.model) && background == QLatin1String("transparent")) {
            fail(error, QStringLiteral("gpt-image-2 当前不支持 transparent 背景，请改为 auto 或 opaque"));
            return false;
        }
        body->insert(QStringLiteral("background"), background);
    }
    if (!moderation.isEmpty()) {
        body->insert(QStringLiteral("moderation"), moderation);
    }
    if (request.outputCompression >= 0) {
        if (outputFormat != QLatin1String("jpeg") && outputFormat != QLatin1String("webp")) {
            fail(error, QStringLiteral("output_compression 仅在 output_format 为 jpeg 或 webp 时有效"));
            return false;
        }
        if (request.outputCompression > 100) {
            fail(error, QStringLiteral("output_compression 必须是 0 到 100 的整数"));
            return false;
        }
        body->insert(QStringLiteral("output_compression"), request.outputCompression);
    }
    return true;
}

QJsonObject buildOpenAiGenerationBody(const ImageRequest &request, QString *error)
{
    QJsonObject body;
    body.insert(QStringLiteral("model"), request.model.trimmed());
    body.insert(QStringLiteral("prompt"), request.prompt.trimmed());
    body.insert(QStringLiteral("size"), request.size.trimmed());
    body.insert(QStringLiteral("n"), request.n);
    if (!applyOpenAiOptions(&body, request, error)) {
        return {};
    }
    return body;
}

// Edit fields keep an explicit order: a multipart body is ordered text, and the
// previous implementation sent model/prompt/size/n then the options in that order.
QList<FormPair> buildOpenAiEditFields(const ImageRequest &request, QString *error)
{
    QJsonObject body = buildOpenAiGenerationBody(request, error);
    if (body.isEmpty()) {
        return {};
    }
    // QJsonObject iterates in sorted-key order, so the wire order has to come from
    // this list rather than from the object.
    static const QStringList order{ QStringLiteral("model"),   QStringLiteral("prompt"),
                                    QStringLiteral("size"),    QStringLiteral("n"),
                                    QStringLiteral("quality"), QStringLiteral("output_format"),
                                    QStringLiteral("background"), QStringLiteral("moderation"),
                                    QStringLiteral("output_compression") };
    QList<FormPair> fields;
    for (const QString &key : order) {
        if (!body.contains(key)) {
            continue;
        }
        const QJsonValue value = body.value(key);
        // Every form value is a string, including n: requests would otherwise send
        // the numeric form of a field the provider parses as text.
        fields.append({ key, value.isDouble() ? QString::number(static_cast<qlonglong>(value.toDouble()))
                                              : value.toString() });
    }
    return fields;
}

static bool validateGrokOptions(const ImageRequest &request, const QString &aspectRatio, const QString &resolution,
                                QString *error)
{
    if (!aspectRatio.isEmpty() && !core::grokSupportedRatios().contains(aspectRatio)) {
        fail(error, QStringLiteral("aspect_ratio='%1' 不支持，可选值: %2").arg(aspectRatio, choices(core::grokSupportedRatios())));
        return false;
    }
    static const QStringList resolutions{ QString(), QStringLiteral("1k"), QStringLiteral("2k") };
    if (!resolutions.contains(resolution)) {
        fail(error, QStringLiteral("resolution 必须是 1k 或 2k"));
        return false;
    }
    static const QStringList responseFormats{ QString(), QStringLiteral("url"), QStringLiteral("b64_json") };
    if (!responseFormats.contains(trimmedLower(request.responseFormat))) {
        fail(error, QStringLiteral("response_format 必须是 url 或 b64_json"));
        return false;
    }
    return true;
}

QJsonObject buildGrokGenerationBody(const ImageRequest &request, QString *error)
{
    const QString size = request.size.trimmed();
    const QString aspectRatio = trimmedLower(request.aspectRatio).isEmpty() ? core::grokAspectRatioFromSize(size)
                                                                           : trimmedLower(request.aspectRatio);
    const QString resolution = trimmedLower(request.resolution).isEmpty() ? core::grokResolutionFromSize(size)
                                                                         : trimmedLower(request.resolution);
    if (!validateGrokOptions(request, aspectRatio, resolution, error)) {
        return {};
    }

    QJsonObject body;
    body.insert(QStringLiteral("model"), request.model.trimmed());
    body.insert(QStringLiteral("prompt"), request.prompt.trimmed());
    body.insert(QStringLiteral("n"), request.n);
    body.insert(QStringLiteral("aspect_ratio"), aspectRatio);
    body.insert(QStringLiteral("resolution"), resolution);
    const QString responseFormat = trimmedLower(request.responseFormat);
    if (!responseFormat.isEmpty()) {
        body.insert(QStringLiteral("response_format"), responseFormat);
    }
    return body;
}

QJsonObject buildGrokEditBody(const ImageRequest &request, const QList<ReferenceImage> &refs, QString *error)
{
    if (refs.size() > limits::kMaxGrokEditReferenceImages) {
        fail(error, QStringLiteral("grok-imagine-image 图生图最多支持 %1 张参考图")
                        .arg(limits::kMaxGrokEditReferenceImages));
        return {};
    }

    QJsonArray array;
    for (int index = 0; index < refs.size(); ++index) {
        const ReferenceImage &image = refs.at(index);
        // Grok carries the reference by URL -- a data URL counts -- so this path
        // never needs the decoded bytes.
        const QString value = !image.dataUrl.trimmed().isEmpty() ? image.dataUrl.trimmed() : image.url.trimmed();
        if (value.isEmpty()) {
            fail(error, QStringLiteral("第 %1 张参考图缺少 data_url").arg(index + 1));
            return {};
        }
        QJsonObject ref;
        ref.insert(QStringLiteral("type"), QStringLiteral("image_url"));
        ref.insert(QStringLiteral("url"), value);
        array.append(ref);
    }

    const QString size = request.size.trimmed();
    const QString explicitRatio = trimmedLower(request.aspectRatio);
    const QString explicitResolution = trimmedLower(request.resolution);
    const QString aspectRatio = explicitRatio.isEmpty() ? core::grokAspectRatioFromSize(size) : explicitRatio;
    const QString resolution = explicitResolution.isEmpty() ? core::grokResolutionFromSize(size) : explicitResolution;
    if (!validateGrokOptions(request, aspectRatio, resolution, error)) {
        return {};
    }

    QJsonObject body;
    body.insert(QStringLiteral("model"), request.model.trimmed());
    body.insert(QStringLiteral("prompt"), request.prompt.trimmed());
    body.insert(QStringLiteral("n"), request.n);
    if (array.size() == 1) {
        body.insert(QStringLiteral("image"), array.at(0).toObject());
    } else {
        body.insert(QStringLiteral("images"), array);
    }
    // A single reference with no explicit choice is left to the provider: sending a
    // derived value would change output the user did not ask for.
    if (array.size() > 1 || !explicitRatio.isEmpty()) {
        body.insert(QStringLiteral("aspect_ratio"), aspectRatio);
    }
    if (array.size() > 1 || !explicitResolution.isEmpty()) {
        body.insert(QStringLiteral("resolution"), resolution);
    }
    const QString responseFormat = trimmedLower(request.responseFormat);
    if (!responseFormat.isEmpty()) {
        body.insert(QStringLiteral("response_format"), responseFormat);
    }
    return body;
}

QJsonObject buildGeminiBody(const ImageRequest &request, const QList<ReferenceImage> &refs, QString *error)
{
    const QString aspectRatio = request.aspectRatio.trimmed().isEmpty() ? QStringLiteral("1:1") : request.aspectRatio.trimmed();
    const QString imageSize = request.resolution.trimmed().isEmpty() ? QStringLiteral("1K")
                                                                    : request.resolution.trimmed().toUpper();
    if (!geminiAspectRatios().contains(aspectRatio)) {
        fail(error, QStringLiteral("Gemini aspect_ratio='%1' 不支持，可选值: %2").arg(aspectRatio, choices(geminiAspectRatios())));
        return {};
    }
    if (!geminiImageSizes().contains(imageSize)) {
        fail(error, QStringLiteral("Gemini resolution 必须是 1K、2K 或 4K"));
        return {};
    }

    QJsonArray parts;
    QJsonObject textPart;
    textPart.insert(QStringLiteral("text"), request.prompt.trimmed());
    parts.append(textPart);

    if (request.editMode) {
        for (const ReferenceImage &image : refs) {
            // Gemini has no edits endpoint: the reference rides along in the same
            // generateContent call, which requires actual bytes here.
            if (image.bytes.isEmpty()) {
                fail(error, QStringLiteral("Gemini 图生图需要参考图字节内容，请先导入为 data URL"));
                return {};
            }
            QJsonObject inlineData;
            inlineData.insert(QStringLiteral("mimeType"),
                              image.contentType.isEmpty() ? QStringLiteral("image/png") : image.contentType);
            inlineData.insert(QStringLiteral("data"), QString::fromLatin1(image.bytes.toBase64()));
            QJsonObject part;
            part.insert(QStringLiteral("inlineData"), inlineData);
            parts.append(part);
        }
    }

    QJsonObject content;
    content.insert(QStringLiteral("role"), QStringLiteral("user"));
    content.insert(QStringLiteral("parts"), parts);
    QJsonArray contents;
    contents.append(content);

    QJsonObject imageConfig;
    imageConfig.insert(QStringLiteral("aspectRatio"), aspectRatio);
    imageConfig.insert(QStringLiteral("imageSize"), imageSize);
    QJsonObject generationConfig;
    generationConfig.insert(QStringLiteral("responseModalities"), QJsonArray{ QStringLiteral("TEXT"), QStringLiteral("IMAGE") });
    generationConfig.insert(QStringLiteral("imageConfig"), imageConfig);

    QJsonObject body;
    body.insert(QStringLiteral("contents"), contents);
    body.insert(QStringLiteral("generationConfig"), generationConfig);
    return body;
}

QString geminiGenerateEndpoint(const QString &baseUrl, const QString &model, QString *error)
{
    QString base = baseUrl.trimmed();
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    const QString modelName = model.trimmed();
    if (base.isEmpty()) {
        fail(error, QStringLiteral("Base URL 不能为空"));
        return {};
    }
    // SPEC 5.6 rule 1 comes first: when the caller already supplied a complete
    // endpoint there is no model segment to build, so demanding a name here would
    // reject a valid configuration. prepare() still refuses an empty model for the
    // Gemini path, which is where that requirement is actually enforced.
    if (base.endsWith(QLatin1String(":generateContent"))) {
        return base;
    }
    if (modelName.isEmpty()) {
        fail(error, QStringLiteral("Gemini 模型名称不能为空"));
        return {};
    }

    QString name = modelName;
    if (name.startsWith(QLatin1String("models/"))) {
        name = name.mid(7);
    }
    // Percent-encode everything outside RFC 3986's unreserved set, so a model id
    // cannot smuggle a path segment or a second colon into the request line.
    QString encoded;
    const QByteArray utf8 = name.toUtf8();
    for (int i = 0; i < utf8.size(); ++i) {
        const char byte = utf8.at(i);
        const bool unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z')
                                || (byte >= '0' && byte <= '9') || byte == '-' || byte == '.' || byte == '_'
                                || byte == '~';
        if (unreserved) {
            encoded.append(QChar::fromLatin1(byte));
        } else {
            encoded += QStringLiteral("%%1").arg(static_cast<quint8>(byte), 2, 16, QLatin1Char('0')).toUpper();
        }
    }

    const QString path = QUrl(base).path();
    QString trimmedPath = path;
    while (trimmedPath.endsWith(QLatin1Char('/'))) {
        trimmedPath.chop(1);
    }
    const QString lastSegment = trimmedPath.section(QLatin1Char('/'), -1);
    QString target = base;
    if (lastSegment != QLatin1String("v1") && lastSegment != QLatin1String("v1beta")
        && lastSegment != QLatin1String("v1alpha")) {
        target += QStringLiteral("/v1beta");
    }
    return target + QStringLiteral("/models/%1:generateContent").arg(encoded);
}

// ---------------------------------------------------------------- multipart

QByteArray buildMultipartBody(const QList<FormPair> &fields, const QString &fileFieldName,
                              const QList<ReferenceImage> &files, QString *contentType, QString *error)
{
    if (fileFieldName.isEmpty()) {
        fail(error, QStringLiteral("multipart 缺少文件字段名"));
        return {};
    }
    if (files.isEmpty()) {
        fail(error, QStringLiteral("multipart 请求至少需要一张参考图"));
        return {};
    }

    // A boundary is only safe if it appears nowhere in the parts, so it is chosen
    // against the assembled payload instead of assuming a fixed string is unique.
    QByteArray corpus;
    for (const FormPair &field : fields) {
        corpus += field.name.toUtf8() + field.value.toUtf8();
    }
    for (const ReferenceImage &file : files) {
        corpus += file.name.toUtf8() + file.contentType.toUtf8() + file.bytes;
    }
    QByteArray boundary;
    for (int attempt = 0; attempt < 64 && boundary.isEmpty(); ++attempt) {
        const QByteArray candidate = QByteArrayLiteral("__oic_boundary_") + QByteArray::number(attempt);
        if (!corpus.contains(candidate)) {
            boundary = candidate;
        }
    }
    if (boundary.isEmpty()) {
        fail(error, QStringLiteral("无法为 multipart 选出未冲突的边界"));
        return {};
    }

    QByteArray payload;
    for (const FormPair &field : fields) {
        payload += "--" + boundary + "\r\n";
        payload += "Content-Disposition: form-data; name=\"" + field.name.toUtf8() + "\"\r\n\r\n";
        payload += field.value.toUtf8() + "\r\n";
    }
    for (const ReferenceImage &file : files) {
        const QByteArray type = file.contentType.isEmpty() ? QByteArray("image/png") : file.contentType.toUtf8();
        payload += "--" + boundary + "\r\n";
        payload += "Content-Disposition: form-data; name=\"" + fileFieldName.toUtf8() + "\"; filename=\""
                   + file.name.toUtf8() + "\"\r\n";
        payload += "Content-Type: " + type + "\r\n\r\n";
        payload += file.bytes + "\r\n";
    }
    payload += "--" + boundary + "--\r\n";

    if (contentType != nullptr) {
        *contentType = QStringLiteral("multipart/form-data; boundary=%1").arg(QString::fromLatin1(boundary));
    }
    return payload;
}

QString openAiEditFileField(const QString &model, int referenceCount)
{
    // Frozen (SPEC 5.3): gpt-image-2 always uses the array field, and any provider
    // receiving more than one image must too. Sending `image` for two files silently
    // drops one of them upstream.
    return core::isGptImage2(model) || referenceCount > 1 ? QStringLiteral("image[]") : QStringLiteral("image");
}

// ---------------------------------------------------------------- assembly

OutgoingRequest prepare(const QString &baseUrl, const QString &apiKey, const QString &protocolHint,
                        const ImageRequest &incoming, QString *error)
{
    OutgoingRequest out;
    ImageRequest request = incoming;
    request.size = request.size.trimmed();
    if (request.size.isEmpty()) {
        request.size = QStringLiteral("1024x1024");
    }
    if (request.size.size() > static_cast<int>(limits::kMaxSizeChars)) {
        fail(error, QStringLiteral("size 过长"));
        return out;
    }

    const QString rawModel = request.model.trimmed();
    if (rawModel.size() > limits::kMaxModelChars) {
        fail(error, QStringLiteral("模型名不能超过 %1 个字符").arg(limits::kMaxModelChars));
        return out;
    }
    if (request.prompt.trimmed().isEmpty()) {
        fail(error, request.editMode ? QStringLiteral("图生图模式下提示词不能为空") : QStringLiteral("提示词不能为空"));
        return out;
    }
    if (request.prompt.trimmed().size() > limits::kMaxPromptChars) {
        fail(error, QStringLiteral("提示词不能超过 %1 个字符").arg(limits::kMaxPromptChars));
        return out;
    }

    const Protocol protocol = resolveProtocol(protocolHint, rawModel, baseUrl, error);
    if (error != nullptr && !error->isEmpty()) {
        return out;
    }
    if (rawModel.isEmpty() && protocol == Protocol::Gemini) {
        fail(error, QStringLiteral("Gemini 模型名称不能为空"));
        return out;
    }
    request.model = !rawModel.isEmpty()
                        ? rawModel
                        : (protocol == Protocol::Grok ? QStringLiteral("grok-imagine-image")
                                                      : QString::fromLatin1(limits::kDefaultImageModel));

    QList<ReferenceImage> refs;
    if (request.editMode) {
        refs = normalizeReferences(request.references, limits::kMaxEditReferenceImages, error);
        if (refs.isEmpty()) {
            return out;
        }
    }

    const QByteArray key = apiKey.trimmed().toUtf8();
    if (protocol == Protocol::Gemini) {
        const QString endpoint = geminiGenerateEndpoint(baseUrl, request.model, error);
        if (endpoint.isEmpty()) {
            return out;
        }
        const QJsonObject body = buildGeminiBody(request, refs, error);
        if (body.isEmpty()) {
            return out;
        }
        out.absoluteUrl = endpoint;
        // Frozen (SPEC 5.6): a ?key= query parameter lands in every proxy and
        // access log between here and Google.
        out.headers.append({ QByteArrayLiteral("x-goog-api-key"), key });
        out.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    } else if (protocol == Protocol::Grok) {
        const QJsonObject body = request.editMode ? buildGrokEditBody(request, refs, error)
                                                 : buildGrokGenerationBody(request, error);
        if (body.isEmpty()) {
            return out;
        }
        out.route = request.editMode ? core::Route::Edits : core::Route::Generations;
        out.headers.append({ QByteArrayLiteral("Authorization"), "Bearer " + key });
        out.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    } else if (!request.editMode) {
        const QJsonObject body = buildOpenAiGenerationBody(request, error);
        if (body.isEmpty()) {
            return out;
        }
        out.route = core::Route::Generations;
        out.headers.append({ QByteArrayLiteral("Authorization"), "Bearer " + key });
        out.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    } else {
        const QList<FormPair> fields = buildOpenAiEditFields(request, error);
        if (fields.isEmpty()) {
            return out;
        }
        QString contentType;
        out.body = buildMultipartBody(fields, openAiEditFileField(request.model, refs.size()), refs, &contentType,
                                     error);
        if (out.body.isEmpty()) {
            return out;
        }
        out.route = core::Route::Edits;
        out.headers.append({ QByteArrayLiteral("Authorization"), "Bearer " + key });
        out.headers.append({ QByteArrayLiteral("Content-Type"), contentType.toUtf8() });
    }

    if (!request.editMode || protocol != Protocol::OpenAI) {
        out.headers.append({ QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json") });
    }
    out.ok = true;
    return out;
}

}  // namespace oic::protocol
