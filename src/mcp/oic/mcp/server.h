// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>

#include <optional>

namespace oic::mcp {

inline constexpr const char *kServerName = "image-client-mcp";
inline constexpr const char *kServerVersion = "0.1.0";

// Protocol versions from SPEC 8.1: 2026-07-28 is the target, 2025-06-18 the floor.
inline constexpr const char *kProtocolLatest = "2026-07-28";
inline constexpr const char *kProtocolMinimum = "2025-06-18";

// Line-delimited JSON-RPC 2.0 over stdio: one UTF-8 object per line, one response line per
// request, and notifications produce no output. stdout is the protocol channel only; every
// diagnostic belongs on stderr (SPEC 8.1).
class Server
{
public:
    Server() = default;

    // Handles one line without its trailing newline. Returns the serialized response, or an
    // empty array for blank lines and notifications.
    QByteArray handleLine(const QByteArray &line);

    // Blocking loop over stdin/stdout; returns the process exit code. Uses QFile rather than
    // <iostream> so the protocol path does not depend on the C++ stream locale.
    int run();

private:
    std::optional<QJsonObject> handle(const QJsonObject &request);
    QJsonObject initialize(const QJsonValue &id, const QJsonObject &params) const;
    QJsonObject toolsCall(const QJsonValue &id, const QJsonObject &params) const;
};

}  // namespace oic::mcp
