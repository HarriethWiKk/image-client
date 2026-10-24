// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "oic/jobs/executor.h"
#include "oic/mcp/tools.h"

class QIODevice;

namespace oic::mcp {

inline constexpr const char *kServerName = "image-client-mcp";
inline constexpr const char *kServerVersion = "0.1.0";

// Protocol versions from SPEC 8.1: 2026-07-28 is the target, 2025-06-18 the floor.
inline constexpr const char *kProtocolLatest = "2026-07-28";
inline constexpr const char *kProtocolMinimum = "2025-06-18";

// Line-delimited JSON-RPC 2.0 over stdio: one UTF-8 object per line, one response line per
// request, and notifications produce no output. stdout is the protocol channel only; every
// diagnostic belongs on stderr (SPEC 8.1).
//
// generate_image / edit_image run on a worker so the reader keeps consuming lines: that is
// what makes notifications/cancelled and cancel_job effective while a job is in flight.
class Server
{
public:
    explicit Server(ToolHost host = {});
    ~Server();

    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    // Handles one line without its trailing newline. Returns the serialized response, or an
    // empty array for blank lines and notifications. Tool calls run synchronously here; run()
    // is the asynchronous path.
    QByteArray handleLine(const QByteArray &line);

    // Blocking loop over stdin/stdout; returns the process exit code.
    int run();

    // Same loop over an arbitrary device, with a frame sink instead of stdout. Tests drive
    // the asynchronous path (worker + cancellation) through this overload.
    int run(QIODevice *input, std::function<void(const QByteArray &)> write);

private:
    std::optional<QJsonObject> handle(const QJsonObject &request);
    QJsonObject initialize(const QJsonValue &id, const QJsonObject &params) const;
    QJsonObject dispatchToolCall(const QJsonValue &id, const QJsonObject &params);
    QJsonObject cancelJob(const QJsonValue &id, const QJsonObject &args);
    void startJob(const QJsonObject &request, const QString &tool);

    void writeFrame(const QJsonObject &response);
    void cancelAll();
    void joinWorkers();

    static QString idKey(const QJsonValue &id);

    ToolHost m_host;

    std::function<void(const QByteArray &)> m_write;
    std::mutex m_writeMutex;
    std::mutex m_tokensMutex;
    std::map<QString, std::shared_ptr<jobs::CancelToken>> m_byJobId;
    std::map<QString, std::shared_ptr<jobs::CancelToken>> m_byRequest;

    std::mutex m_workersMutex;
    std::vector<std::thread> m_workers;
};

}  // namespace oic::mcp
