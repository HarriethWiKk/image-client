// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/app/settingscontroller.h"

#include <QSettings>

#include "oic/app/backend.h"
#include "oic/store/assets.h"
#include "oic/store/database.h"

namespace oic::app {
namespace {
// One settings scope shared by the controller and Backend::init. Values are plain scalars /
// a string list, so the default QSettings backend (registry on Windows) is fine.
QStringList readTrustedHosts()
{
    return QSettings().value(QStringLiteral("network/trustedHosts")).toStringList();
}
}  // namespace

SettingsController::SettingsController(Backend *backend, QObject *parent) : QObject(parent), m_backend(backend)
{
    load();
}

void SettingsController::load()
{
    QSettings s;
    m_themeName = s.value(QStringLiteral("theme"), m_themeName).toString();
    m_retentionRows = s.value(QStringLiteral("retention/rows"), m_retentionRows).toInt();
    m_retentionBytes = s.value(QStringLiteral("retention/bytes"), m_retentionBytes).toLongLong();
    m_trustedHosts = s.value(QStringLiteral("network/trustedHosts")).toStringList();
}

void SettingsController::save() const
{
    QSettings s;
    s.setValue(QStringLiteral("theme"), m_themeName);
    s.setValue(QStringLiteral("retention/rows"), m_retentionRows);
    s.setValue(QStringLiteral("retention/bytes"), m_retentionBytes);
    s.setValue(QStringLiteral("network/trustedHosts"), m_trustedHosts);
}

QStringList SettingsController::storedTrustedHosts()
{
    return readTrustedHosts();
}

int SettingsController::retentionRows() const
{
    return m_retentionRows;
}

void SettingsController::setRetentionRows(int rows)
{
    if (m_retentionRows == rows)
        return;
    m_retentionRows = rows;
    save();
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
    save();
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
    save();
    emit changed();
}

QStringList SettingsController::trustedHosts() const
{
    return m_trustedHosts;
}

void SettingsController::setTrustedHosts(const QStringList &hosts)
{
    if (m_trustedHosts == hosts)
        return;
    m_trustedHosts = hosts;
    save();
    emit changed();
}

void SettingsController::addTrustedHost(const QString &host)
{
    const QString trimmed = host.trimmed().toLower();
    if (trimmed.isEmpty() || m_trustedHosts.contains(trimmed))
        return;
    m_trustedHosts.append(trimmed);
    save();
    emit changed();
}

void SettingsController::removeTrustedHost(const QString &host)
{
    if (!m_trustedHosts.removeOne(host))
        return;
    save();
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
