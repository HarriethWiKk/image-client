// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <functional>

#include "oic/jobs/executor.h"
#include "oic/store/database.h"
#include "oic/store/paths.h"

namespace oic::mcp {

inline constexpr const char *kServerName = "image-client-mcp";
inline constexpr const char *kServerVersion = "0.1.0";

// Protocol versions from SPEC 8.1: 2026-07-28 is the target, 2025-06-18 the floor.
inline constexpr const char *kProtocolLatest = "2026-07-28";
inline constexpr const char *kProtocolMinimum = "2025-06-18";

// The frozen tool surface from SPEC 8.2. Advertised in tools/list.
QJsonArray toolDefinitions();

bool isKnownTool(const QString &name);

// Seams so the tool layer is testable without a database, a network, or Credential Manager.
// The production wiring is makeDefaultToolHost().
struct ToolHost {
    std::function<QList<store::Profile>(QString *error)> profiles;
    std::function<jobs::JobOutcome(const jobs::JobSpec &, jobs::CancelToken *, QString *)> runJob;
    std::function<store::Job(const QString &, bool *, QString *)> job;
    std::function<QList<store::Job>(int limit, QString *error)> jobs;
    std::function<store::Asset(const QString &, bool *, QString *)> asset;
    std::function<QList<store::Asset>(const QString &, QString *)> assets;
    std::function<QList<store::Asset>(QString *error)> allAssets;
    std::function<QByteArray(const QString &relPath, QString *error)> readAsset;
};

ToolHost makeDefaultToolHost(const store::Paths &paths);

struct ToolReply {
    // true maps to a JSON-RPC invalid-params error; false carries a CallToolResult (which may
    // still be isError for a runtime failure).
    bool protocolError = false;
    QString error;
    QJsonObject result;
};

// Arguments + the resolved profile -> the job the executor runs. Exposed for tests: every
// bound in SPEC 3/8.2 is enforced here, not in the wire layer.
jobs::JobSpec buildJobSpec(const QJsonObject &args, const store::Profile &profile, bool editMode,
                           QString *error);

// JobOutcome -> MCP CallToolResult. Default is resource links (SPEC 8.2); inline base64 only
// when the caller explicitly asks for it.
QJsonObject outcomeToResult(const jobs::JobOutcome &outcome, bool inlineImages,
                            const std::function<QByteArray(const QString &, QString *)> &readAsset);

// Runs one tool synchronously. get_job is served here; cancel_job needs the server's running
// registry and is handled there.
ToolReply executeTool(const QString &name, const QJsonObject &args, const QString &jobId,
                      jobs::CancelToken *cancel, const ToolHost &host);

// Resources (SPEC 8.2): the four roots, plus the templated job/asset reads. readResource
// returns the resources/read result object; found=false means the URI is unknown.
QJsonArray resourceDefinitions();
QJsonObject readResource(const QString &uri, const ToolHost &host, bool *found, QString *error);

}  // namespace oic::mcp
