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

private:
    bool m_ready = false;
    QString m_error;
    oic::store::Paths m_paths;
    qint64 m_assetQuotaBytes = 0;
    std::unique_ptr<oic::store::Database> m_database;
    std::unique_ptr<oic::store::AssetStore> m_assets;
    std::unique_ptr<oic::jobs::JobManager> m_jobs;
};

}  // namespace oic::app
