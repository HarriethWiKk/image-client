// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/app/profilecontroller.h"

#include "oic/app/backend.h"
#include "oic/secret/credentials.h"

namespace oic::app {

ProfileController::ProfileController(Backend *backend, QObject *parent)
    : QObject(parent), m_backend(backend)
{
    refresh();
}

void ProfileController::refresh()
{
    QString error;
    m_profiles = m_backend && m_backend->database() ? m_backend->database()->profiles(&error) : QList<oic::store::Profile>();
    emit changed();
}

QStringList ProfileController::names() const
{
    QStringList out;
    for (const oic::store::Profile &profile : m_profiles)
        out.append(profile.name);
    return out;
}

int ProfileController::count() const
{
    return static_cast<int>(m_profiles.size());
}

oic::store::Profile ProfileController::profile(const QString &name) const
{
    for (const oic::store::Profile &p : m_profiles) {
        if (p.name == name)
            return p;
    }
    return oic::store::Profile();
}

QString ProfileController::currentProfile() const
{
    return m_current;
}

void ProfileController::setCurrentProfile(const QString &name)
{
    if (m_current == name)
        return;
    m_current = name;
    emit currentProfileChanged();
}

bool ProfileController::saveProfile(const QString &name, const QString &baseUrl, const QString &protocol,
                                    const QString &imageModel, int timeoutSeconds, QString *error)
{
    QString targetError;
    const QString credentialTarget = oic::secret::targetNameFor(name, &targetError);
    if (!targetError.isEmpty()) {
        if (error != nullptr)
            *error = targetError;
        return false;
    }

    oic::store::Profile profile;
    profile.name = name;
    profile.baseUrl = baseUrl;
    profile.protocol = protocol.isEmpty() ? QStringLiteral("auto") : protocol;
    profile.imageModel = imageModel;
    profile.timeoutSeconds = timeoutSeconds;
    profile.credentialTarget = credentialTarget;

    if (!m_backend->database()->writeProfile(profile, error))
        return false;
    refresh();
    return true;
}

bool ProfileController::removeProfile(const QString &name, QString *error)
{
    if (!m_backend->database()->removeProfile(name, error))
        return false;
    QString ignored;
    oic::secret::eraseSecret(oic::secret::targetNameFor(name, &ignored), &ignored);  // best effort
    refresh();
    return true;
}

bool ProfileController::saveCredential(const QString &name, const QString &key, QString *error)
{
    QString targetError;
    const QString target = oic::secret::targetNameFor(name, &targetError);
    if (!targetError.isEmpty()) {
        if (error != nullptr)
            *error = targetError;
        return false;
    }
    return oic::secret::writeSecret(target, key.toUtf8(), error);
}

bool ProfileController::eraseCredential(const QString &name, QString *error)
{
    QString targetError;
    const QString target = oic::secret::targetNameFor(name, &targetError);
    if (!targetError.isEmpty()) {
        if (error != nullptr)
            *error = targetError;
        return false;
    }
    return oic::secret::eraseSecret(target, error);
}

bool ProfileController::addProfile(const QString &name, const QString &baseUrl, const QString &protocol,
                                   const QString &imageModel, int timeoutSeconds)
{
    QString error;
    const bool ok = saveProfile(name, baseUrl, protocol, imageModel, timeoutSeconds, &error);
    m_lastError = error;
    return ok;
}

bool ProfileController::setCredential(const QString &name, const QString &key)
{
    QString error;
    const bool ok = saveCredential(name, key, &error);
    m_lastError = error;
    return ok;
}

}  // namespace oic::app
