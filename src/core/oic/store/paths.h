// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QString>

namespace oic::store {

// Where the application keeps its files. SPEC 6.1 freezes this: the system temp
// directory is not an option, because the old build used it with world-readable
// defaults.
struct Paths {
    QString root;        // %LOCALAPPDATA%\image-client
    QString database;    // <root>\library.sqlite3
    QString assets;      // <root>\assets
    QString error;       // empty when the layout is usable

    bool ok() const { return error.isEmpty(); }
};

// Resolves the layout without touching the disk.
Paths resolvePaths();

// Creates the directories (inheriting the profile's ACL) and reports what failed.
bool ensureDirectories(Paths *paths);

}  // namespace oic::store
