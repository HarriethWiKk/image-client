// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "oic/store/database.h"

namespace oic::app {

class Backend;

// Profile list + the Credential Manager pointer. The API key itself never lives here: it is
// written to Windows Credential Manager under profile/<name>, and the sqlite row stores only
// the credential_target name (SPEC 6.3).
class ProfileController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentProfile READ currentProfile WRITE setCurrentProfile NOTIFY currentProfileChanged)
    // Field maps for every profile, kept in sync by `changed` -- so the settings Repeater
    // binds to a property (single source of truth) instead of a method call that goes stale.
    Q_PROPERTY(QVariantList profiles READ profilesList NOTIFY changed)
public:
    explicit ProfileController(Backend *backend, QObject *parent = nullptr);

    QStringList names() const;
    int count() const;
    oic::store::Profile profile(const QString &name) const;
    QString currentProfile() const;
    void setCurrentProfile(const QString &name);

    // Base fields only; credential_target is derived from the name. The key goes through
    // saveCredential(). These are C++ out-param methods; QML-facing wrappers come with the
    // settings view.
    bool saveProfile(const QString &name, const QString &baseUrl, const QString &protocol,
                     const QString &imageModel, int timeoutSeconds, QString *error);
    bool removeProfile(const QString &name, QString *error);
    bool saveCredential(const QString &name, const QString &key, QString *error);
    bool eraseCredential(const QString &name, QString *error);
    void refresh();

    // QML-facing wrappers (no out-params); failures leave lastError() set.
    Q_INVOKABLE bool addProfile(const QString &name, const QString &baseUrl, const QString &protocol,
                                const QString &imageModel, int timeoutSeconds);
    Q_INVOKABLE bool setCredential(const QString &name, const QString &key);
    Q_INVOKABLE QStringList profileNames() const { return names(); }
    Q_INVOKABLE QString lastError() const { return m_lastError; }
    Q_INVOKABLE QStringList protocols() const;
    Q_INVOKABLE bool removeProfileByName(const QString &name);
    Q_INVOKABLE bool deleteCredential(const QString &name);
    // Per-profile field map for the settings view: name / baseUrl / protocol / imageModel /
    // timeoutSeconds / hasCredential. Editing a profile re-uses addProfile + setCredential.
    Q_INVOKABLE QVariantMap profileDetail(const QString &name) const;
    Q_INVOKABLE QVariantList profilesList() const;
    Q_INVOKABLE bool hasCredential(const QString &name) const;

Q_SIGNALS:
    void changed();
    void currentProfileChanged();

private:
    Backend *m_backend;
    QList<oic::store::Profile> m_profiles;
    QString m_current;
    QString m_lastError;
};

}  // namespace oic::app
