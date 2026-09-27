// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/core/endpoints.h"

namespace oic::core {
namespace {

// Order inherited from the reference list. It is not load-bearing today: a
// base ending in "/v1/images/generations" also ends in "/images/generations",
// and either match reconstructs the same URL because the stripped prefix
// retains the "/v1". Kept as-is so behaviour stays comparable to the original.
const QStringList &knownApiEndpoints() {
  static const QStringList endpoints{
      QLatin1String("/v1/images/generations"),
      QLatin1String("/images/generations"),
      QLatin1String("/v1/images/edits"),
      QLatin1String("/images/edits"),
  };
  return endpoints;
}

QString routeName(Route route) {
  return route == Route::Edits ? QLatin1String("images/edits") : QLatin1String("images/generations");
}

QString trimTrailingSlashes(QString value) {
  while (value.endsWith(QLatin1Char('/'))) {
    value.chop(1);
  }
  return value;
}

QString trimLeadingSlashes(QString value) {
  int offset = 0;
  while (offset < value.size() && value.at(offset) == QLatin1Char('/')) {
    ++offset;
  }
  return value.mid(offset);
}

}  // namespace

QString normalizeBaseUrl(const QString &baseUrl) {
  return trimTrailingSlashes(baseUrl.trimmed());
}

QString joinUrl(const QString &baseUrl, const QString &suffix) {
  return trimTrailingSlashes(baseUrl) + QLatin1Char('/') + trimLeadingSlashes(suffix);
}

QStringList candidateEndpoints(const QString &baseUrl, Route route) {
  const QString value = normalizeBaseUrl(baseUrl);
  if (value.isEmpty()) {
    return {};
  }
  const QString routeSegment = routeName(route);

  for (const QString &endpoint : knownApiEndpoints()) {
    if (!value.endsWith(endpoint)) {
      continue;
    }
    // The base already names a full endpoint, so rewrite that route in place
    // instead of appending to it.
    const QString prefix = value.left(value.size() - endpoint.size());
    const bool versioned = endpoint.startsWith(QLatin1String("/v1/"));
    return {joinUrl(prefix, versioned ? QLatin1String("v1/") + routeSegment : routeSegment)};
  }

  if (value.endsWith(QLatin1String("/v1"))) {
    return {joinUrl(value, routeSegment)};
  }

  return {joinUrl(value, QLatin1String("v1/") + routeSegment), joinUrl(value, routeSegment)};
}

}  // namespace oic::core
