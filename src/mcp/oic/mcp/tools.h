// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QJsonArray>
#include <QString>

namespace oic::mcp {

// The frozen tool surface from SPEC 8.2. Advertised in tools/list; handlers land in a later
// milestone, so tools/call currently answers with isError until they are wired.
QJsonArray toolDefinitions();

bool isKnownTool(const QString &name);

}  // namespace oic::mcp
