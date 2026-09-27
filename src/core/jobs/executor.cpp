// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/jobs/executor.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrl>
#include <QUuid>

#include <memory>

#include "oic/core/decode.h"
#include "oic/core/endpoints.h"
#include "oic/core/retrypolicy.h"
#include "oic/secret/credentials.h"
#include "oic/store/assets.h"
#include "oic/store/database.h"
#include "oic/store/imageprobe.h"

namespace oic::jobs {
namespace {

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

qint64 nowMs(const Clock &clock)
{
    return clock ? clock() : QDateTime::currentMSecsSinceEpoch();
}

QString toJsonString(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

// The row persisted to jobs.request_json. Excludes the API key and every reference-image
// byte: this row is read back verbatim by the history UI and by get_job.
QJsonObject requestRecord(const JobSpec &spec)
{
    const protocol::ImageRequest &r = spec.image;
    QJsonObject o;
    o.insert(QStringLiteral("mode"), r.editMode ? QStringLiteral("edit") : QStringLiteral("generate"));
    o.insert(QStringLiteral("model"), r.model);
    o.insert(QStringLiteral("prompt"), r.prompt);
    o.insert(QStringLiteral("size"), r.size);
    o.insert(QStringLiteral("n"), r.n);
    if (!r.quality.isEmpty())
        o.insert(QStringLiteral("quality"), r.quality);
    if (!r.background.isEmpty())
        o.insert(QStringLiteral("background"), r.background);
    if (!r.moderation.isEmpty())
        o.insert(QStringLiteral("moderation"), r.moderation);
    if (!r.outputFormat.isEmpty())
        o.insert(QStringLiteral("output_format"), r.outputFormat);
    if (!r.aspectRatio.isEmpty())
        o.insert(QStringLiteral("aspect_ratio"), r.aspectRatio);
    if (!r.resolution.isEmpty())
        o.insert(QStringLiteral("resolution"), r.resolution);
    if (r.editMode) {
        QJsonArray names;
        for (const protocol::ReferenceImage &ref : r.references)
            names.append(ref.name.isEmpty() ? QStringLiteral("(inline)") : ref.name);
        o.insert(QStringLiteral("reference_count"), r.references.size());
        o.insert(QStringLiteral("references"), names);
    }
    return o;
}

net::Request toNetRequest(const protocol::OutgoingRequest &out, const QUrl &url, int timeoutSeconds)
{
    net::Request req;
    req.method = out.method;
    req.url = url;
    req.headers = out.headers;  // prepare() already attached auth + Content-Type
    req.body = out.body;
    req.timeoutSeconds = timeoutSeconds;
    req.maxResponseBytes = limits::kMaxUpstreamJsonBytes;
    return req;
}

// Diagnostic for a reply that never parsed (transport error or non-2xx). The key is only
// ever in the request headers, so a body-derived blob cannot carry it; image bytes are
// stripped by compactRawResponse.
QString transportDiagnostic(const QString &message, const QString &clientRequestId, const QString &endpoint,
                            const net::Reply &reply)
{
    QJsonObject raw;
    raw.insert(QStringLiteral("status"), reply.status);
    if (!reply.contentType.isEmpty())
        raw.insert(QStringLiteral("content_type"), reply.contentType);
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(reply.body, &perr);
    if (perr.error == QJsonParseError::NoError && doc.isObject())
        raw.insert(QStringLiteral("body"), protocol::compactRawResponse(doc.object()));
    else
        raw.insert(QStringLiteral("body_head"), QString::fromUtf8(reply.body.left(2048)));
    if (!reply.error.isEmpty())
        raw.insert(QStringLiteral("transport_error"), reply.error);
    return protocol::diagnosticBlob(message, clientRequestId, endpoint, raw);
}

struct RequestOutcome {
    bool ok = false;
    bool cancelled = false;
    QStringList images;
    QString error;
    QString diagnosticJson;
    QString endpoint;
};

// Sends one logical request and, on a 2xx, parses it. Gemini has a single fixed endpoint;
// OpenAI-compatible and Grok probe candidateEndpoints in order and stop early on a
// client-error refusal so a large multipart body is never re-POSTed (SPEC 4).
RequestOutcome sendAndParse(const protocol::OutgoingRequest &out, const JobSpec &spec, const JobDeps &deps,
                            const QString &clientRequestId, const CancelToken *cancel, qint64 deadlineMs)
{
    RequestOutcome ro;

    protocol::ResponseContext ctx;
    ctx.clientRequestId = clientRequestId;
    ctx.timeoutSeconds = spec.timeoutSeconds;
    ctx.fetcher = deps.fetcher;
    ctx.limits = deps.mediaLimits;  // frozen SPEC 3 caps by default; a test seam

    const auto overDeadline = [&]() { return nowMs(deps.clock) > deadlineMs; };
    const auto stopped = [&]() { return (cancel && cancel->cancelled()) || overDeadline(); };

    if (!out.absoluteUrl.isEmpty()) {  // Gemini
        if (stopped()) {
            ro.cancelled = true;
            ro.endpoint = out.absoluteUrl;
            return ro;
        }
        ro.endpoint = out.absoluteUrl;
        const net::Reply reply = deps.sender(toNetRequest(out, QUrl(out.absoluteUrl), spec.timeoutSeconds));
        ctx.endpoint = out.absoluteUrl;
        if (reply.status >= 200 && reply.status < 300) {
            const protocol::ParseResult pr = protocol::parseGeminiResponse(reply.body, reply.contentType, ctx);
            ro.ok = pr.ok();
            ro.images = pr.images;
            ro.error = pr.error;
            ro.diagnosticJson = pr.diagnosticJson;
            return ro;
        }
        ro.error = reply.error.isEmpty() ? QStringLiteral("上游返回错误状态 %1").arg(reply.status) : reply.error;
        ro.diagnosticJson = transportDiagnostic(ro.error, clientRequestId, out.absoluteUrl, reply);
        return ro;
    }

    // OpenAI-compatible / Grok: probe candidates in order.
    const QStringList candidates = core::candidateEndpoints(spec.baseUrl, out.route);
    QString lastError;
    QString lastDiag;
    QString lastEndpoint;
    for (const QString &candidate : candidates) {
        if (stopped()) {
            ro.cancelled = true;
            ro.endpoint = lastEndpoint;
            return ro;
        }
        const net::Reply reply = deps.sender(toNetRequest(out, QUrl(candidate), spec.timeoutSeconds));
        lastEndpoint = candidate;
        if (reply.status >= 200 && reply.status < 300) {
            ctx.endpoint = candidate;
            const protocol::ParseResult pr = protocol::parseImageResponse(reply.body, reply.contentType, ctx);
            ro.endpoint = candidate;
            ro.ok = pr.ok();
            ro.images = pr.images;
            ro.error = pr.error;
            ro.diagnosticJson = pr.diagnosticJson;
            return ro;  // 2xx is a real provider answer; do not probe further
        }
        if (reply.status > 0) {
            if (core::refusesRetryAsClientError(reply.status, reply.contentType)) {
                ro.error = QStringLiteral("上游拒绝请求（%1）").arg(reply.status);
                ro.diagnosticJson = transportDiagnostic(ro.error, clientRequestId, candidate, reply);
                ro.endpoint = candidate;
                return ro;
            }
            lastError = QStringLiteral("上游返回错误状态 %1").arg(reply.status);
            lastDiag = transportDiagnostic(lastError, clientRequestId, candidate, reply);
            continue;  // 5xx / upstream / 404 / 405 / html → next candidate (SPEC 4)
        }
        // No usable response: policy refusal, connect failure, timeout, oversized body.
        lastError = reply.error.isEmpty() ? reply.detail : reply.error;
        lastDiag = transportDiagnostic(lastError, clientRequestId, candidate, reply);
        if (!reply.retryable) {
            ro.error = lastError;
            ro.diagnosticJson = lastDiag;
            ro.endpoint = candidate;
            return ro;
        }
    }
    ro.error = lastError.isEmpty() ? QStringLiteral("没有可用的端点候选") : lastError;
    ro.diagnosticJson = lastDiag;
    ro.endpoint = lastEndpoint;
    return ro;
}

}  // namespace

JobOutcome runJob(const JobSpec &spec, const JobDeps &deps, CancelToken *cancel, QString *error)
{
    JobOutcome outcome;
    outcome.jobId = newId();
    outcome.clientRequestId = newId();
    outcome.status = QStringLiteral("failed");

    const auto setError = [&](const QString &message) {
        outcome.error = message;
        if (error)
            *error = message;
    };

    QString dbErr;
    store::Database db(deps.databasePath);
    if (!db.open(&dbErr)) {
        setError(db.lastError().isEmpty() ? dbErr : db.lastError());
        return outcome;
    }
    store::AssetStore assets(deps.assetsRoot, deps.assetQuotaBytes, deps.assetLockTimeoutMs);

    const qint64 startMs = nowMs(deps.clock);
    const qint64 deadlineMs = startMs + qint64(limits::kMaxJobRuntimeSeconds) * 1000;

    // Resolve the protocol up front: it selects the parser and whether n means serial calls.
    QString protoErr;
    const protocol::Protocol proto =
        protocol::resolveProtocol(spec.protocolHint, spec.image.model, spec.baseUrl, &protoErr);
    if (!protoErr.isEmpty()) {
        setError(protoErr);
        return outcome;
    }

    store::Job row;
    row.id = outcome.jobId;
    row.createdAt = startMs;
    row.updatedAt = startMs;
    row.mode = spec.image.editMode ? QStringLiteral("edit") : QStringLiteral("generate");
    row.protocol = protocol::protocolName(proto);
    row.profile = spec.profileName;
    row.model = spec.image.model;
    row.prompt = spec.image.prompt;
    row.size = spec.image.size;
    row.n = spec.image.n;
    row.status = QStringLiteral("queued");
    row.clientRequestId = outcome.clientRequestId;
    row.requestJson = toJsonString(requestRecord(spec));
    if (!db.createJob(row, &dbErr)) {
        setError(dbErr);
        return outcome;
    }

    const auto finalize = [&](const QString &status, const QString &resultJson) {
        row.status = status;
        row.updatedAt = nowMs(deps.clock);
        row.durationMs = row.updatedAt - startMs;
        row.endpoint = outcome.endpoint;
        row.error = outcome.error;
        row.resultJson = resultJson;
        outcome.status = status;
        outcome.durationMs = row.durationMs;
        db.updateJob(row, &dbErr);
    };

    // Read the key just-in-time; it never enters a persisted struct or a diagnostic.
    bool absent = false;
    QString secErr;
    QByteArray keyBytes = deps.secrets(spec.credentialTarget, &secErr, &absent);
    if (keyBytes.isEmpty()) {
        setError(absent ? QStringLiteral("没有为 profile「%1」保存的凭据").arg(spec.profileName) : secErr);
        finalize(QStringLiteral("failed"), QString());
        return outcome;
    }

    QString prepErr;
    const protocol::OutgoingRequest out =
        protocol::prepare(spec.baseUrl, QString::fromUtf8(keyBytes), spec.protocolHint, spec.image, &prepErr);
    keyBytes.fill(0);  // best-effort scrub of the local copy
    if (!out.ok) {
        setError(out.error.isEmpty() ? prepErr : out.error);
        finalize(QStringLiteral("failed"), QString());
        return outcome;
    }

    row.status = QStringLiteral("running");
    row.updatedAt = nowMs(deps.clock);
    db.updateJob(row, &dbErr);

    // Gemini simulates n with serial requests (SPEC 5.6); other providers take n in one.
    const int logicalRequests = (proto == protocol::Protocol::Gemini) ? qMax(1, spec.image.n) : 1;
    QStringList images;
    for (int i = 0; i < logicalRequests; ++i) {
        const RequestOutcome r =
            sendAndParse(out, spec, deps, outcome.clientRequestId, cancel, deadlineMs);
        if (!r.endpoint.isEmpty())
            outcome.endpoint = r.endpoint;
        if (r.cancelled || (cancel && cancel->cancelled()) || nowMs(deps.clock) > deadlineMs) {
            setError(nowMs(deps.clock) > deadlineMs ? QStringLiteral("任务超过最长运行时间被中止")
                                                    : QStringLiteral("任务已取消"));
            finalize(QStringLiteral("cancelled"), QString());
            return outcome;  // zero-partial: nothing is written unless the whole job succeeds
        }
        if (!r.ok) {
            setError(r.error);
            outcome.diagnosticJson = r.diagnosticJson;
            finalize(QStringLiteral("failed"), QString());
            return outcome;
        }
        images.append(r.images);
    }

    // Decode → probe → write file → insert row. Any failure rolls back the files already
    // written, so a failed or cancelled job never leaves a partial gallery behind.
    QList<AssetRef> written;
    QStringList writtenPaths;
    const auto rollback = [&]() {
        QString ignore;
        for (const QString &path : writtenPaths)
            assets.remove(path, &ignore);
    };

    int ordinal = 0;
    for (const QString &image : images) {
        if (!image.startsWith(QStringLiteral("data:"))) {
            outcome.remoteOnly.append(image);  // SPEC 7.1 download-failed fallback link
            continue;
        }
        QString mime;
        QString encoded;
        QString duErr;
        if (!protocol::parseDataUrl(image, &mime, &encoded, &duErr)) {
            setError(duErr);
            rollback();
            finalize(QStringLiteral("failed"), QString());
            return outcome;
        }
        bool decoded = false;
        QString decErr;
        const QByteArray bytes =
            core::decodeBase64Limited(encoded, limits::kMaxRemoteMediaBytes, QStringLiteral("image"), &decoded, &decErr);
        if (!decoded) {
            setError(decErr);
            rollback();
            finalize(QStringLiteral("failed"), QString());
            return outcome;
        }
        const store::ImageInfo info = store::probeImage(bytes);
        if (!info.recognized) {
            setError(QStringLiteral("结果不是可识别的图片，已拒绝保存"));
            rollback();
            finalize(QStringLiteral("failed"), QString());
            return outcome;
        }
        const QString assetId = newId();
        const store::AssetWriteResult wr = assets.write(outcome.jobId, assetId, info.mime, bytes);
        if (!wr.ok()) {
            setError(wr.error);
            rollback();
            finalize(QStringLiteral("failed"), QString());
            return outcome;
        }
        store::Asset assetRow;
        assetRow.id = assetId;
        assetRow.jobId = outcome.jobId;
        assetRow.ordinal = ordinal++;
        assetRow.filename = wr.relPath.section(QLatin1Char('/'), -1);
        assetRow.relPath = wr.relPath;
        assetRow.bytes = wr.bytes;
        assetRow.mime = info.mime;
        assetRow.width = info.width;
        assetRow.height = info.height;
        assetRow.sha256 = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        assetRow.createdAt = nowMs(deps.clock);
        if (!db.addAsset(assetRow, &dbErr)) {
            setError(dbErr);
            writtenPaths.append(wr.relPath);
            rollback();
            finalize(QStringLiteral("failed"), QString());
            return outcome;
        }
        writtenPaths.append(wr.relPath);
        AssetRef ref;
        ref.id = assetId;
        ref.relPath = wr.relPath;
        ref.mime = info.mime;
        ref.width = info.width;
        ref.height = info.height;
        ref.bytes = wr.bytes;
        written.append(ref);
    }

    outcome.assets = written;

    QJsonObject result;
    QJsonArray assetArray;
    for (const AssetRef &ref : written) {
        QJsonObject a;
        a.insert(QStringLiteral("id"), ref.id);
        a.insert(QStringLiteral("rel_path"), ref.relPath);
        a.insert(QStringLiteral("mime"), ref.mime);
        a.insert(QStringLiteral("width"), ref.width);
        a.insert(QStringLiteral("height"), ref.height);
        a.insert(QStringLiteral("bytes"), double(ref.bytes));
        assetArray.append(a);
    }
    result.insert(QStringLiteral("assets"), assetArray);
    result.insert(QStringLiteral("remote_only"), QJsonArray::fromStringList(outcome.remoteOnly));
    result.insert(QStringLiteral("endpoint"), outcome.endpoint);
    result.insert(QStringLiteral("client_request_id"), outcome.clientRequestId);

    finalize(QStringLiteral("succeeded"), toJsonString(result));
    return outcome;
}

JobDeps makeDefaultDeps(const QString &databasePath, const QString &assetsRoot, qint64 quotaBytes,
                        const net::Transport::Settings &transport)
{
    JobDeps deps;
    deps.databasePath = databasePath;
    deps.assetsRoot = assetsRoot;
    deps.assetQuotaBytes = quotaBytes;

    auto shared = std::make_shared<net::Transport>(transport);
    deps.sender = [shared](const net::Request &request) { return shared->send(request); };
    deps.fetcher = [shared](const QUrl &url, QByteArray *bytes, QString *contentType, QString *error) {
        net::Request request;
        request.method = QByteArrayLiteral("GET");
        request.url = url;
        request.timeoutSeconds = limits::kDefaultTimeoutSeconds;
        request.maxResponseBytes = limits::kMaxRemoteMediaBytes;
        const net::Reply reply = shared->send(request);
        if (reply.status >= 200 && reply.status < 300 && !reply.body.isEmpty()) {
            if (bytes)
                *bytes = reply.body;
            if (contentType)
                *contentType = reply.contentType;
            return true;
        }
        if (error)
            *error = reply.error.isEmpty() ? reply.detail : reply.error;
        return false;
    };
    deps.secrets = [](const QString &target, QString *error, bool *absent) {
        return secret::readSecret(target, error, absent);
    };
    deps.clock = []() { return QDateTime::currentMSecsSinceEpoch(); };
    return deps;
}

}  // namespace oic::jobs
