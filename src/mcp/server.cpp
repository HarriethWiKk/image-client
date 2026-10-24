// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/mcp/server.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QUuid>

#include <cstdio>
#include <utility>

namespace oic::mcp {
namespace {

// JSON-RPC 2.0 error codes.
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kInternalError = -32603;
constexpr int kResourceNotFound = -32002;

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

QJsonObject toolText(const QString &text, bool isError)
{
    QJsonObject content;
    content.insert(QStringLiteral("type"), QStringLiteral("text"));
    content.insert(QStringLiteral("text"), text);

    QJsonObject result;
    result.insert(QStringLiteral("content"), QJsonArray{content});
    result.insert(QStringLiteral("isError"), isError);
    return result;
}

struct ParsedLine {
    bool blank = false;
    bool ok = false;
    QJsonObject request;
    QJsonObject errorResponse;
};

ParsedLine parseLine(const QByteArray &line)
{
    ParsedLine parsed;
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        parsed.blank = true;
        return parsed;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(trimmed, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        parsed.errorResponse = makeError(
            QJsonValue(QJsonValue::Null), kParseError,
            QStringLiteral("Parse error: %1").arg(parseError.errorString()));
        return parsed;
    }
    if (!document.isObject()) {
        parsed.errorResponse =
            makeError(QJsonValue(QJsonValue::Null), kInvalidRequest,
                      QStringLiteral("Invalid Request: expected a JSON object"));
        return parsed;
    }

    parsed.ok = true;
    parsed.request = document.object();
    return parsed;
}

}  // namespace

Server::Server(ToolHost host) : m_host(std::move(host)) {}

Server::~Server()
{
    cancelAll();
    joinWorkers();
}

QByteArray Server::handleLine(const QByteArray &line)
{
    const ParsedLine parsed = parseLine(line);
    if (parsed.blank) {
        return {};
    }
    if (!parsed.ok) {
        return serialize(parsed.errorResponse);
    }

    const std::optional<QJsonObject> response = handle(parsed.request);
    return response ? serialize(*response) : QByteArray{};
}

std::optional<QJsonObject> Server::handle(const QJsonObject &request)
{
    const QJsonValue id = request.value(QStringLiteral("id"));
    const bool notification = id.isUndefined();

    const QJsonValue methodValue = request.value(QStringLiteral("method"));
    if (!methodValue.isString()) {
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

    // Cancellation is the one notification with an effect: it flips the token of the matching
    // in-flight call (SPEC 8.1).
    if (method == QLatin1String("notifications/cancelled")) {
        const QString key = idKey(request.value(QStringLiteral("params"))
                                      .toObject()
                                      .value(QStringLiteral("requestId")));
        if (!key.isEmpty()) {
            std::lock_guard<std::mutex> lock(m_tokensMutex);
            const auto it = m_byRequest.find(key);
            if (it != m_byRequest.end()) {
                it->second->cancel();
            }
        }
        return std::nullopt;
    }
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
        return dispatchToolCall(id, request.value(QStringLiteral("params")).toObject());
    }
    if (method == QLatin1String("resources/list")) {
        QJsonObject result;
        result.insert(QStringLiteral("resources"), resourceDefinitions());
        return makeResult(id, result);
    }
    if (method == QLatin1String("resources/read")) {
        const QString uri =
            request.value(QStringLiteral("params")).toObject().value(QStringLiteral("uri")).toString();
        if (uri.isEmpty()) {
            return makeError(id, kInvalidParams, QStringLiteral("Invalid params: uri is required"));
        }
        bool found = false;
        QString error;
        const QJsonObject contents = readResource(uri, m_host, &found, &error);
        if (!found) {
            return makeError(id, kResourceNotFound,
                             error.isEmpty() ? QStringLiteral("resource not found: %1").arg(uri)
                                             : error);
        }
        if (!error.isEmpty()) {
            return makeError(id, kInternalError, error);
        }
        return makeResult(id, contents);
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

    QJsonObject resourcesCapability;
    resourcesCapability.insert(QStringLiteral("subscribe"), false);
    resourcesCapability.insert(QStringLiteral("listChanged"), false);

    QJsonObject capabilities;
    capabilities.insert(QStringLiteral("tools"), toolsCapability);
    capabilities.insert(QStringLiteral("resources"), resourcesCapability);

    QJsonObject serverInfo;
    serverInfo.insert(QStringLiteral("name"), QString::fromLatin1(kServerName));
    serverInfo.insert(QStringLiteral("version"), QString::fromLatin1(kServerVersion));

    QJsonObject result;
    result.insert(QStringLiteral("protocolVersion"), version);
    result.insert(QStringLiteral("capabilities"), capabilities);
    result.insert(QStringLiteral("serverInfo"), serverInfo);
    return makeResult(id, result);
}

QJsonObject Server::dispatchToolCall(const QJsonValue &id, const QJsonObject &params)
{
    const QString name = params.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
        return makeError(id, kInvalidParams, QStringLiteral("Invalid params: missing tool name"));
    }
    if (!isKnownTool(name)) {
        return makeError(id, kInvalidParams, QStringLiteral("Unknown tool: %1").arg(name));
    }

    const QJsonObject args = params.value(QStringLiteral("arguments")).toObject();
    if (name == QLatin1String("cancel_job")) {
        return cancelJob(id, args);
    }

    const QString jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const ToolReply reply = executeTool(name, args, jobId, nullptr, m_host);
    if (reply.protocolError) {
        return makeError(id, kInvalidParams, reply.error);
    }
    return makeResult(id, reply.result);
}

QJsonObject Server::cancelJob(const QJsonValue &id, const QJsonObject &args)
{
    const QString jobId = args.value(QStringLiteral("job_id")).toString().trimmed();
    if (jobId.isEmpty()) {
        return makeError(id, kInvalidParams, QStringLiteral("Invalid params: job_id is required"));
    }

    {
        std::lock_guard<std::mutex> lock(m_tokensMutex);
        const auto it = m_byJobId.find(jobId);
        if (it != m_byJobId.end()) {
            it->second->cancel();
            return makeResult(id, toolText(QStringLiteral("cancellation requested"), false));
        }
    }

    if (!m_host.job) {
        return makeResult(id, toolText(QStringLiteral("tool host is not configured"), true));
    }
    QString error;
    bool found = false;
    const store::Job job = m_host.job(jobId, &found, &error);
    if (!error.isEmpty()) {
        return makeResult(id, toolText(error, true));
    }
    if (!found) {
        return makeResult(id, toolText(QStringLiteral("job not found: %1").arg(jobId), true));
    }
    return makeResult(
        id, toolText(QStringLiteral("job is not running (status: %1)").arg(job.status), false));
}

void Server::startJob(const QJsonObject &request, const QString &tool)
{
    const QJsonValue id = request.value(QStringLiteral("id"));
    const QString requestKey = idKey(id);
    const QString jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto token = std::make_shared<jobs::CancelToken>();

    {
        std::lock_guard<std::mutex> lock(m_tokensMutex);
        m_byJobId[jobId] = token;
        m_byRequest[requestKey] = token;
    }

    const QJsonObject args =
        request.value(QStringLiteral("params")).toObject().value(QStringLiteral("arguments")).toObject();

    std::lock_guard<std::mutex> lock(m_workersMutex);
    m_workers.emplace_back([this, id, requestKey, jobId, token, tool, args]() {
        const ToolReply reply = executeTool(tool, args, jobId, token.get(), m_host);
        {
            std::lock_guard<std::mutex> guard(m_tokensMutex);
            m_byJobId.erase(jobId);
            m_byRequest.erase(requestKey);
        }
        writeFrame(reply.protocolError ? makeError(id, kInvalidParams, reply.error)
                                       : makeResult(id, reply.result));
    });
}

int Server::run()
{
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) {
        return 1;
    }
    return run(&input, [](const QByteArray &frame) {
        std::fwrite(frame.constData(), 1, static_cast<size_t>(frame.size()), stdout);
        std::fflush(stdout);
    });
}

int Server::run(QIODevice *input, std::function<void(const QByteArray &)> write)
{
    if (input == nullptr) {
        return 1;
    }
    m_write = std::move(write);

    while (!input->atEnd()) {
        const QByteArray line = input->readLine();
        const ParsedLine parsed = parseLine(line);
        if (parsed.blank) {
            continue;
        }
        if (!parsed.ok) {
            writeFrame(parsed.errorResponse);
            continue;
        }

        const QJsonObject request = parsed.request;
        const QString method = request.value(QStringLiteral("method")).toString();
        const QJsonObject params = request.value(QStringLiteral("params")).toObject();
        const QString tool = params.value(QStringLiteral("name")).toString();
        const bool hasId = !request.value(QStringLiteral("id")).isUndefined();

        // Long-running tools get a worker so the reader can still see notifications/cancelled
        // and cancel_job while the job is in flight.
        if (hasId && method == QLatin1String("tools/call")
            && (tool == QLatin1String("generate_image") || tool == QLatin1String("edit_image"))) {
            startJob(request, tool);
            continue;
        }

        const std::optional<QJsonObject> response = handle(request);
        if (response) {
            writeFrame(*response);
        }
    }

    cancelAll();
    joinWorkers();
    return 0;
}

void Server::writeFrame(const QJsonObject &response)
{
    const QByteArray frame = serialize(response) + '\n';
    std::lock_guard<std::mutex> lock(m_writeMutex);
    if (m_write) {
        m_write(frame);
    }
}

void Server::cancelAll()
{
    std::lock_guard<std::mutex> lock(m_tokensMutex);
    for (auto &entry : m_byJobId) {
        entry.second->cancel();
    }
}

void Server::joinWorkers()
{
    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lock(m_workersMutex);
        workers.swap(m_workers);
    }
    for (std::thread &worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

QString Server::idKey(const QJsonValue &id)
{
    if (id.isString()) {
        return QStringLiteral("s:") + id.toString();
    }
    if (id.isDouble()) {
        return QStringLiteral("n:") + QString::number(id.toDouble(), 'g', 17);
    }
    return {};
}

}  // namespace oic::mcp
