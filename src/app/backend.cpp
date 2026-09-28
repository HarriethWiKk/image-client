// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/app/backend.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include "oic/app/settingscontroller.h"
#include "oic/net/transport.h"

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
    oic::net::Transport::Settings transport;
    transport.trustedHosts = SettingsController::storedTrustedHosts();
    const oic::jobs::JobDeps deps =
        oic::jobs::makeDefaultDeps(paths.database, paths.assets, kDefaultAssetQuotaBytes, transport);
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

QString Backend::assetAbsolutePath(const QString &relPath) const
{
    m_lastError.clear();
    if (m_paths.assets.isEmpty()) {
        m_lastError = QStringLiteral("后端未就绪");
        return {};
    }
    const QString rootClean = QDir::cleanPath(m_paths.assets);
    const QString abs = QDir::cleanPath(QDir(m_paths.assets).filePath(relPath));
    if (abs != rootClean && !abs.startsWith(rootClean + QLatin1Char('/'))) {
        m_lastError = QStringLiteral("非法资源路径");
        return {};
    }
    return abs;
}

bool Backend::copyAssetTo(const QString &relPath, const QString &destUrl)
{
    const QString src = assetAbsolutePath(relPath);
    if (src.isEmpty())
        return false;
    const QUrl url(destUrl);
    const QString dest = url.isLocalFile() ? url.toLocalFile() : destUrl;
    if (dest.isEmpty()) {
        m_lastError = QStringLiteral("未选择保存位置");
        return false;
    }
    QFile in(src);
    if (!in.open(QIODevice::ReadOnly)) {
        m_lastError = QStringLiteral("无法打开源文件");
        return false;
    }
    const QByteArray data = in.readAll();
    in.close();
    QFile out(dest);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_lastError = QStringLiteral("无法写入 %1").arg(QFileInfo(dest).fileName());
        return false;
    }
    const bool ok = out.write(data) == data.size();
    out.close();
    if (!ok)
        m_lastError = QStringLiteral("保存未写完");
    return ok;
}

bool Backend::openExternally(const QString &relPath)
{
    const QString abs = assetAbsolutePath(relPath);
    if (abs.isEmpty())
        return false;
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(abs))) {
        m_lastError = QStringLiteral("无法用外部程序打开");
        return false;
    }
    return true;
}

}  // namespace oic::app
