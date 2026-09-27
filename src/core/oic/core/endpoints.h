// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QString>
#include <QStringList>

namespace oic::core {

// Endpoint routes this client knows how to reach. Scoped to images only:
// video and variations were descoped (SPEC §1).
enum class Route { Generations, Edits };

// Path suffix for a route, as it appears after the base URL.
QString routePath(Route route);

QString normalizeBaseUrl(const QString &baseUrl);

QString joinUrl(const QString &baseUrl, const QString &suffix);

// Ordered list of full URLs to try. Probe order matters: the first entry is
// tried, and the second only when the first fails in a retryable way
// (see isRetryableEndpointFailure).
QStringList candidateEndpoints(const QString &baseUrl, Route route);

}  // namespace oic::core
