// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QObject>
#include <QString>

#include <memory>

#include "oic/jobs/executor.h"
#include "oic/jobs/jobmanager.h"
#include "oic/store/assets.h"
#include "oic/store/database.h"
#include "oic/store/paths.h"

namespace oic::app {

// Owns the GUI-side runtime: resolved paths, the history store, the asset store and the
// job pool. The QML-facing controllers are constructed over the accessors below. Lives in
// the gui-side oic-app library, which -- unlike the Gui-free oic-core -- may use QtGui/Quick.
class Backend : public QObject {
    Q_OBJECT
public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    Backend(const Backend &) = delete;
    Backend &operator=(const Backend &) = delete;

    // Production: resolve the %LOCALAPPDATA% layout and wire everything with the real
    // transport + credential seams (SPEC 6.1 -- resolvePaths refuses to guess a fallback).
    bool init(QString *error);

    // Testable / injectable: caller supplies the paths and the job deps (fake seams in
    // tests, so no real network or Credential Manager is touched).
    bool initWithPaths(const oic::store::Paths &paths, const oic::jobs::JobDeps &deps, QString *error);

    bool isReady() const { return m_ready; }
    QString errorText() const { return m_error; }
    QString assetsRoot() const { return m_paths.assets; }
    QString databasePath() const { return m_paths.database; }
    qint64 assetQuotaBytes() const { return m_assetQuotaBytes; }

    oic::store::Database *database() const { return m_database.get(); }
    oic::store::AssetStore *assetStore() const { return m_assets.get(); }
    oic::jobs::JobManager *jobs() const { return m_jobs.get(); }

    // Resolve a stored asset's relPath to an absolute path, refusing anything that would
    // escape the assets root (the same guard the image provider applies). Empty on rejection.
    Q_INVOKABLE QString assetAbsolutePath(const QString &relPath) const;
    // "Save As": copy a stored asset to a destination file URL. / open it in the system
    // viewer (the fallback for formats this Qt cannot decode, e.g. WebP -- SPEC 6.5).
    Q_INVOKABLE bool copyAssetTo(const QString &relPath, const QString &destUrl);
    Q_INVOKABLE bool openExternally(const QString &relPath);
    QString lastError() const { return m_lastError; }

private:
    bool m_ready = false;
    QString m_error;
    mutable QString m_lastError;
    oic::store::Paths m_paths;
    qint64 m_assetQuotaBytes = 0;
    std::unique_ptr<oic::store::Database> m_database;
    std::unique_ptr<oic::store::AssetStore> m_assets;
    std::unique_ptr<oic::jobs::JobManager> m_jobs;
};

}  // namespace oic::app
