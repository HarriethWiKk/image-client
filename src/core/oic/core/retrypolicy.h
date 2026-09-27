// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QString>

namespace oic::core {

// When probing candidate endpoints, whether to give up rather than send the same
// request to the next candidate.
//
// A 4xx that is not 404/405 and not an HTML page is an explicit refusal of the
// payload itself. Re-POSTing it would resend a multipart body that can carry
// 50 MB of reference images to a second endpoint, which wastes bandwidth at best
// and double-bills at worst when the first request did reach the provider.
bool refusesRetryAsClientError(int status, const QString &contentType);

// Whether the failure looks like a gateway or upstream problem, i.e. worth
// trying the next candidate endpoint.
bool looksLikeGatewayOrUpstreamError(int status, const QString &jsonErrorType);

// Extracts error.type (or a top-level "type") from a JSON error body, returning
// an empty string when the body is not JSON or has no such field.
QString jsonErrorType(const QByteArray &body, const QString &contentType);

}  // namespace oic::core
