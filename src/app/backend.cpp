// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/app/backend.h"

namespace oic::app {

namespace {
// SPEC 6.2 suggests a 2 GiB starting quota; the settings controller will make this
// user-adjustable and feed Database::pruneToLimits.
constexpr qint64 kDefaultAssetQuotaBytes = 2LL * 1024 * 1024 * 1024;
}  // namespace

Backend::Backend(QObject *parent) : QObject(parent) {}

Backend::~Backend() = default;

bool Backend::init(QString *error)
{
    const oic::store::Paths paths = oic::store::resolvePaths();
    if (!paths.ok()) {
        m_error = paths.error;
        if (error != nullptr)
            *error = paths.error;
        return false;
    }
    const oic::jobs::JobDeps deps =
        oic::jobs::makeDefaultDeps(paths.database, paths.assets, kDefaultAssetQuotaBytes);
    return initWithPaths(paths, deps, error);
}

bool Backend::initWithPaths(const oic::store::Paths &paths, const oic::jobs::JobDeps &deps, QString *error)
{
    const auto fail = [&](const QString &message) {
        m_error = message;
        if (error != nullptr)
            *error = message;
        m_ready = false;
        return false;
    };

    m_paths = paths;
    m_assetQuotaBytes = deps.assetQuotaBytes;

    if (!oic::store::ensureDirectories(&m_paths))
        return fail(m_paths.error.isEmpty() ? QStringLiteral("无法创建数据目录") : m_paths.error);

    m_database = std::make_unique<oic::store::Database>(m_paths.database);
    QString dbError;
    if (!m_database->open(&dbError))
        return fail(dbError.isEmpty() ? m_database->lastError() : dbError);

    m_assets = std::make_unique<oic::store::AssetStore>(m_paths.assets, m_assetQuotaBytes);

    // The JobManager workers each open their own store::Database on deps.databasePath
    // (thread-bound connection) and coexist through WAL; this GUI-thread handle above is
    // used for history/profile queries.
    m_jobs = std::make_unique<oic::jobs::JobManager>(deps);

    m_ready = true;
    return true;
}

}  // namespace oic::app
