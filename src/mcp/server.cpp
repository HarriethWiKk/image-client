// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/mcp/server.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QFile>

#include <cstdio>

#include "oic/mcp/tools.h"

namespace oic::mcp {
namespace {

// JSON-RPC 2.0 error codes.
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;

QJsonObject makeResult(const QJsonValue &id, const QJsonObject &result)
{
    QJsonObject response;
    response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    response.insert(QStringLiteral("id"), id);
    response.insert(QStringLiteral("result"), result);
    return response;
}

QJsonObject makeError(const QJsonValue &id, int code, const QString &message)
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("message"), message);

    QJsonObject response;
    response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    response.insert(QStringLiteral("id"), id);
    response.insert(QStringLiteral("error"), error);
    return response;
}

QByteArray serialize(const QJsonObject &response)
{
    return QJsonDocument(response).toJson(QJsonDocument::Compact);
}

}  // namespace

QByteArray Server::handleLine(const QByteArray &line)
{
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(trimmed, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return serialize(makeError(QJsonValue(QJsonValue::Null), kParseError,
                                   QStringLiteral("Parse error: %1").arg(parseError.errorString())));
    }
    if (!document.isObject()) {
        return serialize(makeError(QJsonValue(QJsonValue::Null), kInvalidRequest,
                                   QStringLiteral("Invalid Request: expected a JSON object")));
    }

    const std::optional<QJsonObject> response = handle(document.object());
    return response ? serialize(*response) : QByteArray{};
}

std::optional<QJsonObject> Server::handle(const QJsonObject &request)
{
    const QJsonValue id = request.value(QStringLiteral("id"));
    const bool notification = id.isUndefined();

    const QJsonValue methodValue = request.value(QStringLiteral("method"));
    if (!methodValue.isString()) {
        // A malformed notification gets no reply; a request does.
        if (notification) {
            return std::nullopt;
        }
        return makeError(id, kInvalidRequest, QStringLiteral("Invalid Request: method must be a string"));
    }
    const QString method = methodValue.toString();

    if (method == QLatin1String("initialize")) {
        if (notification) {
            return std::nullopt;
        }
        return initialize(id, request.value(QStringLiteral("params")).toObject());
    }
    // initialized / cancelled are notifications by definition: never answer, even if a client
    // mistakenly sends an id.
    if (method.startsWith(QLatin1String("notifications/"))) {
        return std::nullopt;
    }
    if (notification) {
        return std::nullopt;
    }

    if (method == QLatin1String("ping")) {
        return makeResult(id, QJsonObject{});
    }
    if (method == QLatin1String("tools/list")) {
        QJsonObject result;
        result.insert(QStringLiteral("tools"), toolDefinitions());
        return makeResult(id, result);
    }
    if (method == QLatin1String("tools/call")) {
        return toolsCall(id, request.value(QStringLiteral("params")).toObject());
    }
    return makeError(id, kMethodNotFound, QStringLiteral("Method not found: %1").arg(method));
}

QJsonObject Server::initialize(const QJsonValue &id, const QJsonObject &params) const
{
    // Echo a supported version; otherwise announce the latest one (SPEC 8.1).
    const QString requested = params.value(QStringLiteral("protocolVersion")).toString();
    const QString version =
        (requested == QLatin1String(kProtocolLatest) || requested == QLatin1String(kProtocolMinimum))
            ? requested
            : QString::fromLatin1(kProtocolLatest);

    QJsonObject toolsCapability;
    toolsCapability.insert(QStringLiteral("listChanged"), false);

    QJsonObject capabilities;
    capabilities.insert(QStringLiteral("tools"), toolsCapability);

    QJsonObject serverInfo;
    serverInfo.insert(QStringLiteral("name"), QString::fromLatin1(kServerName));
    serverInfo.insert(QStringLiteral("version"), QString::fromLatin1(kServerVersion));

    QJsonObject result;
    result.insert(QStringLiteral("protocolVersion"), version);
    result.insert(QStringLiteral("capabilities"), capabilities);
    result.insert(QStringLiteral("serverInfo"), serverInfo);
    return makeResult(id, result);
}

QJsonObject Server::toolsCall(const QJsonValue &id, const QJsonObject &params) const
{
    const QString name = params.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
        return makeError(id, kInvalidParams, QStringLiteral("Invalid params: missing tool name"));
    }
    if (!isKnownTool(name)) {
        return makeError(id, kInvalidParams, QStringLiteral("Unknown tool: %1").arg(name));
    }

    // Milestone 1 is the protocol surface only. Report the missing handler as a tool error
    // (isError), not as a protocol error: the tool exists and the request was well formed.
    QJsonObject text;
    text.insert(QStringLiteral("type"), QStringLiteral("text"));
    text.insert(QStringLiteral("text"),
                QStringLiteral("tool '%1' is not implemented in this build yet").arg(name));

    QJsonObject result;
    result.insert(QStringLiteral("content"), QJsonArray{text});
    result.insert(QStringLiteral("isError"), true);
    return makeResult(id, result);
}

int Server::run()
{
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) {
        return 1;
    }

    while (!input.atEnd()) {
        const QByteArray line = input.readLine();
        const QByteArray response = handleLine(line);
        if (response.isEmpty()) {
            continue;
        }
        // Write through the CRT handle whose binary mode main() sets. QFile would reopen the
        // descriptor and turn every LF into CRLF, which breaks the line-delimited framing.
        const QByteArray frame = response + '\n';
        if (std::fwrite(frame.constData(), 1, static_cast<size_t>(frame.size()), stdout)
            != static_cast<size_t>(frame.size())) {
            return 1;
        }
        std::fflush(stdout);
    }
    return 0;
}

}  // namespace oic::mcp
