// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace oic::app {

class Backend;

// History retention (row + byte budget) and quota display, plus runtime prefs (theme,
// trusted upstream hosts). applyRetention() runs Database::pruneToLimits and deletes the
// files it reports -- prune only touches the database, the caller owns the disk.
class SettingsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int retentionRows READ retentionRows WRITE setRetentionRows NOTIFY changed)
    Q_PROPERTY(qint64 retentionBytes READ retentionBytes WRITE setRetentionBytes NOTIFY changed)
    Q_PROPERTY(qint64 usedBytes READ usedBytes NOTIFY changed)
    Q_PROPERTY(QString themeName READ themeName WRITE setThemeName NOTIFY changed)
public:
    explicit SettingsController(Backend *backend, QObject *parent = nullptr);

    int retentionRows() const;
    void setRetentionRows(int rows);
    qint64 retentionBytes() const;
    void setRetentionBytes(qint64 bytes);
    qint64 usedBytes() const;
    QString themeName() const;
    void setThemeName(const QString &name);

    Q_INVOKABLE int applyRetention();  // returns the number of jobs dropped

Q_SIGNALS:
    void changed();

private:
    Backend *m_backend;
    int m_retentionRows = 500;                                // SPEC 6.2 suggested default
    qint64 m_retentionBytes = 2LL * 1024 * 1024 * 1024;       // 2 GiB
    QString m_themeName = QStringLiteral("dark");
};

}  // namespace oic::app
