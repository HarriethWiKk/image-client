// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

#include "oic/core/limits.h"

namespace oic::protocol {

// Fetches a remote result image for SPEC 7.1's `url` branch. Returning false means
// the download failed, and the parser then keeps the original URL -- that fallback
// is deliberate: an unreachable CDN link is still something the user can open
// later, whereas discarding the result loses the generation entirely.
using MediaFetcher = std::function<bool(const QUrl &url, QByteArray *bytes, QString *contentType, QString *error)>;

// Caps the parser applies. Defaults are the frozen SPEC 3 values; they are
// parameters only so the cumulative-size path can be tested without a hundred
// megabytes of fixture.
struct MediaLimits {
    qint64 perImageBytes = limits::kMaxRemoteMediaBytes;
    qint64 totalBytes = limits::kMaxGeneratedMediaBytes;
    int maxItems = limits::kMaxUpstreamMediaItems;
};

struct ResponseContext {
    QString clientRequestId;
    QString endpoint;  // original-host form; never the pinned IP
    int timeoutSeconds = limits::kDefaultTimeoutSeconds;
    MediaFetcher fetcher;
    MediaLimits limits;
};

struct ParseResult {
    QStringList images;   // data: URLs, or the provider URL when a download failed
    qint64 totalBytes = 0;
    int skippedItems = 0;

    QString error;          // user-facing message, empty on success
    QString diagnosticJson; // the structured blob the old code raised with

    bool ok() const { return error.isEmpty(); }
};

// SPEC 7.1 (OpenAI-compatible and Grok share it) and SPEC 7.2 (Gemini).
ParseResult parseImageResponse(const QByteArray &body, const QString &contentType, const ResponseContext &context);
ParseResult parseGeminiResponse(const QByteArray &body, const QString &contentType, const ResponseContext &context);

// Diagnostics must never carry image bytes: a 32 MB inline image in an error string
// ends up in the job row, the log and the UI panel.
QJsonObject compactRawResponse(const QJsonObject &data, qint64 retainBudget = limits::kMaxRetainedRawBytes);
QJsonValue compactGeminiRawResponse(const QJsonValue &value);

// Wraps message/client_request_id/endpoint/raw the way the previous implementation
// did, because those strings are parsed by users and by the MCP error path.
QString diagnosticBlob(const QString &message, const QString &clientRequestId, const QString &endpoint,
                       const QJsonObject &raw);

}  // namespace oic::protocol
