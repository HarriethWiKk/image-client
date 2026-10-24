// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/mcp/config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace oic::mcp {

McpConfig loadConfig(const QString &path, QString *error)
{
    McpConfig config;
    if (error != nullptr) {
        error->clear();
    }

    QFile file(path);
    if (!file.exists()) {
        return config;  // no config file is the normal case
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot read %1").arg(path);
        }
        return config;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) {
            *error = QStringLiteral("%1 is not valid JSON: %2")
                         .arg(path, parseError.errorString());
        }
        return config;
    }

    const QJsonObject root = document.object();

    const QJsonValue roots = root.value(QStringLiteral("allowed_input_roots"));
    if (!roots.isUndefined()) {
        if (!roots.isArray()) {
            if (error != nullptr) {
                *error = QStringLiteral("allowed_input_roots must be an array of paths");
            }
            return config;
        }
        QStringList rejected;
        for (const QJsonValue &value : roots.toArray()) {
            if (!value.isString()) {
                rejected.append(QStringLiteral("<not a string>"));
                continue;
            }
            const QString canonical = QFileInfo(value.toString()).canonicalFilePath();
            if (canonical.isEmpty() || !QFileInfo(canonical).isDir()) {
                rejected.append(value.toString());
                continue;
            }
            if (!config.allowedInputRoots.contains(canonical)) {
                config.allowedInputRoots.append(canonical);
            }
        }
        if (!rejected.isEmpty() && error != nullptr) {
            *error = QStringLiteral("ignored non-existent input roots: %1")
                         .arg(rejected.join(QStringLiteral(", ")));
        }
    }

    const QJsonValue hosts = root.value(QStringLiteral("trusted_hosts"));
    if (!hosts.isUndefined()) {
        if (!hosts.isArray()) {
            if (error != nullptr) {
                *error = QStringLiteral("trusted_hosts must be an array of host names");
            }
            return config;
        }
        for (const QJsonValue &value : hosts.toArray()) {
            if (value.isString() && !value.toString().trimmed().isEmpty()) {
                config.trustedHosts.append(value.toString().trimmed().toLower());
            }
        }
    }

    return config;
}

}  // namespace oic::mcp
