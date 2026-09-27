// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/store/paths.h"

#include <QDir>

namespace oic::store {

Paths resolvePaths()
{
    Paths paths;
    // LOCALAPPDATA is the documented per-user location whose default ACL grants
    // only that user and SYSTEM. If it is missing (some service accounts) there is
    // no safe substitute: falling back to temp or to the profile root would put
    // generated images and the history database somewhere the freeze forbids.
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (localAppData.isEmpty()) {
        paths.error = QStringLiteral("LOCALAPPDATA 未设置，无法确定安全的用户数据目录");
        return paths;
    }
    if (!QDir(localAppData).isAbsolute()) {
        paths.error = QStringLiteral("LOCALAPPDATA 不是绝对路径：%1").arg(localAppData);
        return paths;
    }

    paths.root = QDir(localAppData + QStringLiteral("/image-client")).absolutePath();
    paths.database = paths.root + QStringLiteral("/library.sqlite3");
    paths.assets = paths.root + QStringLiteral("/assets");
    return paths;
}

bool ensureDirectories(Paths *paths)
{
    if (paths == nullptr) {
        return false;
    }
    if (!paths->ok()) {
        *paths = resolvePaths();
        if (!paths->ok()) {
            return false;
        }
    }

    QDir dir;
    if (!dir.mkpath(paths->root)) {
        paths->error = QStringLiteral("无法创建目录 %1").arg(paths->root);
        return false;
    }
    if (!dir.mkpath(paths->assets)) {
        paths->error = QStringLiteral("无法创建目录 %1").arg(paths->assets);
        return false;
    }
    return true;
}

}  // namespace oic::store
