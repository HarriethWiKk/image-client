// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/core/retrypolicy.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace oic::core {

bool refusesRetryAsClientError(int status, const QString &contentType)
{
    if (status < 400 || status >= 500)
        return false;
    // 404 and 405 are how endpoint probing discovers the right path, so they must
    // stay probeable.
    if (status == 404 || status == 405)
        return false;
    // An HTML body usually means we hit a captive portal or an error page in front
    // of the real API; the next candidate may be fine.
    return !contentType.toLower().contains(QLatin1String("text/html"));
}

bool looksLikeGatewayOrUpstreamError(int status, const QString &jsonErrorType)
{
    return status >= 500 || jsonErrorType.contains(QLatin1String("upstream"));
}

QString jsonErrorType(const QByteArray &body, const QString &contentType)
{
    if (!contentType.toLower().contains(QLatin1String("application/json")))
        return {};

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return {};

    const QJsonObject root = document.object();
    const QJsonValue error = root.value(QLatin1String("error"));
    if (error.isObject())
        return error.toObject().value(QLatin1String("type")).toString();
    return root.value(QLatin1String("type")).toString();
}

}  // namespace oic::core
