// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/protocol/parsers.h"

#include <QJsonDocument>

#include "oic/core/decode.h"
#include "oic/protocol/imageprotocol.h"

namespace oic::protocol {
namespace {

constexpr qint64 kSmallValueBytes = 4 * 1024;

QByteArray serialize(const QJsonValue &value)
{
    if (value.isObject()) {
        return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    }
    if (value.isArray()) {
        return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
    }
    // Scalars have no container to live in; wrap, serialize, unwrap the brackets.
    QJsonArray wrapper;
    wrapper.append(value);
    QByteArray text = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
    if (text.startsWith('[') && text.endsWith(']')) {
        text = text.mid(1, text.size() - 2);
    }
    return text;
}

bool isSmallValue(const QJsonValue &value)
{
    return serialize(value).size() <= kSmallValueBytes;
}

bool parseJsonObject(const QByteArray &body, QJsonObject *out, QString *error)
{
    if (body.isEmpty()) {
        *error = QStringLiteral("上游返回为空");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        // The text is decoded as UTF-8 explicitly; a charset-less body must not fall
        // back to ISO-8859-1 or upstream Chinese messages arrive as mojibake (SPEC 7.4).
        *error = QStringLiteral("上游响应不是合法 JSON：%1").arg(parseError.errorString());
        return false;
    }
    if (!document.isObject()) {
        *error = QStringLiteral("上游响应顶层必须是 JSON 对象");
        return false;
    }
    *out = document.object();
    return true;
}

QString previewBody(const QByteArray &body, qint64 budget)
{
    return QString::fromUtf8(body.left(static_cast<qsizetype>(budget)));
}

// SPEC 7.1 item rules. Returns an empty string when this item yields nothing usable;
// `failed` distinguishes "no field" from "field present but unusable".
QString itemToDataUrl(const QJsonObject &item, const ResponseContext &context, bool *failed)
{
    const QString b64 = item.value(QStringLiteral("b64_json")).toString();
    if (!b64.trimmed().isEmpty()) {
        bool ok = false;
        QString error;
        const QByteArray bytes = core::decodeBase64Limited(b64, context.limits.perImageBytes,
                                                           QStringLiteral("上游图像"), &ok, &error);
        if (!ok) {
            if (failed) {
                *failed = true;
            }
            return QString();
        }
        return dataUrlFromBytes(bytes, imageMimeFromFormat(item.value(QStringLiteral("output_format")).toString()));
    }

    const QString url = item.value(QStringLiteral("url")).toString().trimmed();
    if (!url.isEmpty()) {
        // Frozen fallback: a network error keeps the URL rather than failing the job.
        QUrl parsed(url);
        QByteArray bytes;
        QString contentType;
        QString fetchError;
        if (context.fetcher && context.fetcher(parsed, &bytes, &contentType, &fetchError) && !bytes.isEmpty()) {
            return dataUrlFromBytes(bytes, contentType.isEmpty() ? QStringLiteral("image/png") : contentType);
        }
        return url;
    }

    const QString result = item.value(QStringLiteral("result")).toString().trimmed();
    if (!result.isEmpty()) {
        if (result.startsWith(QLatin1String("data:"))) {
            QString mime;
            QString encoded;
            QString parseError;
            if (!parseDataUrl(result, &mime, &encoded, &parseError) || !mime.startsWith(QLatin1String("image/"))) {
                if (failed) {
                    *failed = true;
                }
                return QString();
            }
            bool ok = false;
            QString decodeError;
            core::decodeBase64Limited(encoded, context.limits.perImageBytes, QStringLiteral("图像数据"), &ok,
                                      &decodeError);
            return ok ? result : QString();
        }
        if (result.startsWith(QLatin1String("http://")) || result.startsWith(QLatin1String("https://"))) {
            QUrl parsed(result);
            QByteArray bytes;
            QString contentType;
            QString fetchError;
            if (context.fetcher && context.fetcher(parsed, &bytes, &contentType, &fetchError) && !bytes.isEmpty()) {
                return dataUrlFromBytes(bytes, contentType.isEmpty() ? QStringLiteral("image/png") : contentType);
            }
            return result;
        }
        bool ok = false;
        QString decodeError;
        const QByteArray bytes = core::decodeBase64Limited(result, context.limits.perImageBytes,
                                                           QStringLiteral("图像数据"), &ok, &decodeError);
        if (!ok) {
            if (failed) {
                *failed = true;
            }
            return QString();
        }
        return dataUrlFromBytes(bytes, imageMimeFromFormat(item.value(QStringLiteral("output_format")).toString()));
    }

    return QString();
}

}  // namespace

QString diagnosticBlob(const QString &message, const QString &clientRequestId, const QString &endpoint,
                       const QJsonObject &raw)
{
    QJsonObject blob;
    blob.insert(QStringLiteral("message"), message);
    blob.insert(QStringLiteral("client_request_id"), clientRequestId);
    blob.insert(QStringLiteral("endpoint"), endpoint);
    blob.insert(QStringLiteral("raw"), raw);
    return QString::fromUtf8(QJsonDocument(blob).toJson(QJsonDocument::Compact));
}

QJsonObject compactRawResponse(const QJsonObject &data, qint64 retainBudget)
{
    if (QJsonDocument(data).toJson(QJsonDocument::Compact).size() <= retainBudget) {
        return data;
    }

    QJsonObject compact;
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        const QString key = it.key();
        if (key == QLatin1String("data") || key == QLatin1String("b64_json") || key == QLatin1String("result")) {
            continue;
        }
        if (isSmallValue(it.value())) {
            compact.insert(key, it.value());
        }
    }

    const QJsonArray items = data.value(QStringLiteral("data")).toArray();
    QJsonArray kept;
    const int limit = qMin(items.size(), limits::kMaxUpstreamMediaItems);
    for (int i = 0; i < limit; ++i) {
        if (!items.at(i).isObject()) {
            continue;
        }
        QJsonObject keptItem;
        const QJsonObject item = items.at(i).toObject();
        for (auto it = item.constBegin(); it != item.constEnd(); ++it) {
            if (it.key() == QLatin1String("b64_json") || it.key() == QLatin1String("result")) {
                continue;
            }
            if (isSmallValue(it.value())) {
                keptItem.insert(it.key(), it.value());
            }
        }
        kept.append(keptItem);
    }
    if (!kept.isEmpty()) {
        compact.insert(QStringLiteral("data"), kept);
    }
    compact.insert(QStringLiteral("_omitted_large_fields"), true);
    return compact;
}

static QJsonValue redactGemini(const QJsonValue &value, const QString &parentKey)
{
    if (value.isObject()) {
        QJsonObject out;
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            const bool isInlinePayload = it.key() == QLatin1String("data")
                                         && (parentKey == QLatin1String("inlineData")
                                             || parentKey == QLatin1String("inline_data"));
            out.insert(it.key(), isInlinePayload ? QJsonValue(QStringLiteral("<omitted>"))
                                                 : redactGemini(it.value(), it.key()));
        }
        return out;
    }
    if (value.isArray()) {
        QJsonArray out;
        for (const QJsonValue &entry : value.toArray()) {
            out.append(redactGemini(entry, parentKey));
        }
        return out;
    }
    return value;
}

QJsonValue compactGeminiRawResponse(const QJsonValue &value)
{
    // Gemini diagnostics stay readable, but inlineData.data must never reach the job
    // row -- one 4K frame is tens of megabytes. Only that key, in exactly that
    // position, is replaced; everything else survives so the failure is diagnosable.
    return redactGemini(value, QString());
}

ParseResult parseImageResponse(const QByteArray &body, const QString &contentType, const ResponseContext &context)
{
    Q_UNUSED(contentType);
    ParseResult result;
    QJsonObject document;
    QString parseError;
    if (!parseJsonObject(body, &document, &parseError)) {
        result.error = parseError;
        QJsonObject raw;
        raw.insert(QStringLiteral("preview"), previewBody(body, limits::kDiagnosticBodyBytes));
        result.diagnosticJson = diagnosticBlob(parseError, context.clientRequestId, context.endpoint, raw);
        return result;
    }

    const QJsonValue dataValue = document.value(QStringLiteral("data"));
    if (!dataValue.isArray() || dataValue.toArray().isEmpty()) {
        result.error = QStringLiteral("接口返回异常，未找到 data");
        result.diagnosticJson =
            diagnosticBlob(result.error, context.clientRequestId, context.endpoint, compactRawResponse(document));
        return result;
    }

    const QJsonArray items = dataValue.toArray();
    const int limit = qMin(items.size(), context.limits.maxItems);
    qint64 total = 0;
    for (int i = 0; i < limit; ++i) {
        if (!items.at(i).isObject()) {
            ++result.skippedItems;
            continue;
        }
        bool failed = false;
        const QString image = itemToDataUrl(items.at(i).toObject(), context, &failed);
        if (image.isEmpty()) {
            ++result.skippedItems;
            continue;
        }
        total += dataUrlPayloadSize(image);
        if (total > context.limits.totalBytes) {
            // Refuse outright: a truncated gallery would look like a successful job.
            result.error = QStringLiteral("生成结果总大小超过 %1 字节限制").arg(context.limits.totalBytes);
            result.diagnosticJson = diagnosticBlob(result.error, context.clientRequestId, context.endpoint,
                                                   compactRawResponse(document));
            result.images.clear();
            return result;
        }
        result.images.append(image);
    }

    if (result.images.isEmpty()) {
        result.error = QStringLiteral("返回里没有可保存的 b64_json、url 或 result");
        result.diagnosticJson =
            diagnosticBlob(result.error, context.clientRequestId, context.endpoint, compactRawResponse(document));
        return result;
    }

    result.totalBytes = total;
    return result;
}

ParseResult parseGeminiResponse(const QByteArray &body, const QString &contentType, const ResponseContext &context)
{
    Q_UNUSED(contentType);
    ParseResult result;
    QJsonObject document;
    QString parseError;
    if (!parseJsonObject(body, &document, &parseError)) {
        result.error = parseError;
        QJsonObject raw;
        raw.insert(QStringLiteral("preview"), previewBody(body, limits::kDiagnosticBodyBytes));
        result.diagnosticJson = diagnosticBlob(parseError, context.clientRequestId, context.endpoint, raw);
        return result;
    }

    qint64 total = 0;
    const QJsonArray candidates = document.value(QStringLiteral("candidates")).toArray();
    for (const QJsonValue &candidateValue : candidates) {
        if (!candidateValue.isObject()) {
            continue;
        }
        const QJsonArray parts = candidateValue.toObject().value(QStringLiteral("content")).toObject()
                                     .value(QStringLiteral("parts")).toArray();
        for (const QJsonValue &partValue : parts) {
            if (!partValue.isObject()) {
                continue;
            }
            const QJsonObject part = partValue.toObject();
            QJsonObject inlineData = part.value(QStringLiteral("inlineData")).toObject();
            if (inlineData.isEmpty()) {
                // Both spellings appear in the wild.
                inlineData = part.value(QStringLiteral("inline_data")).toObject();
            }
            if (inlineData.isEmpty()) {
                continue;
            }
            QString mime = inlineData.value(QStringLiteral("mimeType")).toString();
            if (mime.isEmpty()) {
                mime = inlineData.value(QStringLiteral("mime_type")).toString();
            }
            mime = mime.trimmed().toLower();
            if (!mime.startsWith(QLatin1String("image/"))) {
                continue;
            }
            bool ok = false;
            QString decodeError;
            const QByteArray bytes = core::decodeBase64Limited(inlineData.value(QStringLiteral("data")).toString(),
                                                               context.limits.perImageBytes,
                                                               QStringLiteral("Gemini 上游图像"), &ok, &decodeError);
            if (!ok) {
                ++result.skippedItems;
                continue;
            }
            total += bytes.size();
            if (total > context.limits.totalBytes) {
                result.error = QStringLiteral("生成结果总大小超过 %1 字节限制").arg(context.limits.totalBytes);
                result.images.clear();
                return result;
            }
            result.images.append(dataUrlFromBytes(bytes, mime));
            if (result.images.size() >= context.limits.maxItems) {
                result.totalBytes = total;
                return result;
            }
        }
    }

    if (result.images.isEmpty()) {
        result.error = QStringLiteral("Gemini 返回中没有可用的图片，可能被安全策略拦截或模型不支持生图");
        QJsonValue redacted = compactGeminiRawResponse(QJsonValue(document));
        result.diagnosticJson = diagnosticBlob(result.error, context.clientRequestId, context.endpoint,
                                               redacted.toObject());
        return result;
    }

    result.totalBytes = total;
    return result;
}

}  // namespace oic::protocol
