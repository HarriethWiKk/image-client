// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QString>
#include <QStringList>

namespace oic::mcp {

// SPEC 8.2/8.3 settings that live in a config file rather than the environment:
// `allowed_input_roots` gates import_reference_image, `trusted_hosts` is the SSRF exemption
// list for a local gateway. An absent file means "no roots, no trusted hosts".
struct McpConfig {
    QStringList allowedInputRoots;  // canonical absolute directories
    QStringList trustedHosts;
};

// Never throws. A missing file is not an error; a malformed file yields empty settings plus
// a message so the caller can log it and keep running (fail closed: no roots).
McpConfig loadConfig(const QString &path, QString *error);

}  // namespace oic::mcp
