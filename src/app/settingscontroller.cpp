// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/app/settingscontroller.h"

#include "oic/app/backend.h"
#include "oic/store/assets.h"
#include "oic/store/database.h"

namespace oic::app {

SettingsController::SettingsController(Backend *backend, QObject *parent) : QObject(parent), m_backend(backend) {}

int SettingsController::retentionRows() const
{
    return m_retentionRows;
}

void SettingsController::setRetentionRows(int rows)
{
    if (m_retentionRows == rows)
        return;
    m_retentionRows = rows;
    emit changed();
}

qint64 SettingsController::retentionBytes() const
{
    return m_retentionBytes;
}

void SettingsController::setRetentionBytes(qint64 bytes)
{
    if (m_retentionBytes == bytes)
        return;
    m_retentionBytes = bytes;
    emit changed();
}

qint64 SettingsController::usedBytes() const
{
    if (m_backend == nullptr || m_backend->assetStore() == nullptr)
        return 0;
    QString error;
    return m_backend->assetStore()->usedBytes(&error);
}

QString SettingsController::themeName() const
{
    return m_themeName;
}

void SettingsController::setThemeName(const QString &name)
{
    if (m_themeName == name)
        return;
    m_themeName = name;
    emit changed();
}

int SettingsController::applyRetention()
{
    if (m_backend == nullptr || m_backend->database() == nullptr || m_backend->assetStore() == nullptr)
        return 0;
    QString error;
    const oic::store::PruneReport report = m_backend->database()->pruneToLimits(m_retentionRows, m_retentionBytes, &error);
    // pruneToLimits removed the DB rows; deleting the files on disk is the caller's job.
    QString ignored;
    for (const oic::store::Asset &asset : report.removedAssets)
        m_backend->assetStore()->remove(asset.relPath, &ignored);
    emit changed();
    return static_cast<int>(report.removedJobs.size());
}

}  // namespace oic::app
