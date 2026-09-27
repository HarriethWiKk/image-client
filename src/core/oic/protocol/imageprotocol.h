// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "oic/core/endpoints.h"
#include "oic/net/transport.h"

namespace oic::protocol {

// SPEC 5.1. Video was descoped, so there is no video variant here even though the
// old code interleaved the two.
enum class Protocol {
    OpenAI,
    Grok,
    Gemini,
};

QString protocolName(Protocol protocol);

// "auto" derives the protocol from the model name and base URL. An explicit value
// is trusted verbatim -- a wrong explicit value is the user's claim about their own
// endpoint, and second-guessing it produced the old CLI's silent misroutes.
Protocol resolveProtocol(const QString &raw, const QString &model, const QString &baseUrl, QString *error);

QStringList protocolValues();
QStringList geminiAspectRatios();
QStringList geminiImageSizes();

// One reference image. The three sources are mutually exclusive and checked in
// this order (SPEC 5.3): dataUrl, then url. bytes is the decoded form of whichever
// was present, filled in by normalizeReferences().
struct ReferenceImage {
    QString name;         // multipart filename
    QString dataUrl;
    QString url;
    QString contentType;  // defaults to image/png
    QByteArray bytes;     // payload decoded from dataUrl (needed by multipart and Gemini)
    QString error;        // per-image problem; the caller decides whether to abort
};

// The provider-agnostic request the UI and the MCP tool both build. Bounds on
// n / prompt / timeout belong to the payload layer that fills this in; what is
// validated here is the part that changes the wire format.
struct ImageRequest {
    QString model;
    QString prompt;
    QString size = QStringLiteral("1024x1024");
    int n = 1;
    bool editMode = false;

    // Common output options (SPEC 5.5).
    QString quality;
    QString background;
    QString moderation;
    QString outputFormat;
    QString responseFormat;
    int outputCompression = -1;  // -1 means unset

    // Grok / Gemini. Gemini reads resolution as imageSize (1K/2K/4K).
    QString aspectRatio;
    QString resolution;

    // Edit mode inputs; ignored for generation.
    QList<ReferenceImage> references;
};

struct OutgoingRequest {
    bool ok = false;
    QString error;

    QByteArray method = QByteArrayLiteral("POST");
    core::Route route = core::Route::Generations;
    // Empty for openai/grok, whose URL comes from candidateEndpoints probing.
    // Set for Gemini, which has exactly one endpoint shape and no version guessing.
    QString absoluteUrl;

    QList<net::Header> headers;
    QByteArray body;
};

// A form field in wire order. Multipart bodies are ordered text, so the order is
// carried explicitly rather than recovered from a QJsonObject.
struct FormPair {
    QString name;
    QString value;
};

// Data URL helpers (SPEC 7.3 rules for the base64 part).
bool parseDataUrl(const QString &value, QString *mime, QString *encoded, QString *error);
QString dataUrlFromBytes(const QByteArray &bytes, const QString &mime);
qint64 dataUrlPayloadSize(const QString &value);
QString imageMimeFromFormat(const QString &outputFormat);

// Decodes every reference and enforces the per-image and cumulative byte caps.
// Returns an empty list with error set when any image is unusable.
QList<ReferenceImage> normalizeReferences(const QList<ReferenceImage> &input, int maxImages, QString *error);

// Body builders, exposed individually so each rule in SPEC 5.3-5.6 has a direct test.
QJsonObject buildOpenAiGenerationBody(const ImageRequest &request, QString *error);
QList<FormPair> buildOpenAiEditFields(const ImageRequest &request, QString *error);
QJsonObject buildGrokGenerationBody(const ImageRequest &request, QString *error);
QJsonObject buildGrokEditBody(const ImageRequest &request, const QList<ReferenceImage> &refs, QString *error);
QJsonObject buildGeminiBody(const ImageRequest &request, const QList<ReferenceImage> &refs, QString *error);

// Frozen (SPEC 5.3): image[] for gpt-image-2 or several images, image for one.
QString openAiEditFileField(const QString &model, int referenceCount);

// OpenAI-compatible edits go out as multipart/form-data.
QByteArray buildMultipartBody(const QList<FormPair> &fields, const QString &fileFieldName,
                              const QList<ReferenceImage> &files, QString *contentType, QString *error);

QString geminiGenerateEndpoint(const QString &baseUrl, const QString &model, QString *error);

// The single entry point: detects the protocol, builds the right body, and attaches
// the matching credential header (SPEC 5.6: Gemini uses x-goog-api-key, never ?key=).
OutgoingRequest prepare(const QString &baseUrl, const QString &apiKey, const QString &protocolHint,
                        const ImageRequest &request, QString *error = nullptr);

}  // namespace oic::protocol
