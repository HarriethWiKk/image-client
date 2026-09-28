// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/app/jobcontroller.h"

#include "oic/app/backend.h"
#include "oic/core/limits.h"
#include "oic/jobs/jobmanager.h"
#include "oic/store/database.h"

namespace oic::app {

JobController::JobController(Backend *backend, QObject *parent) : QObject(parent), m_backend(backend)
{
    if (m_backend != nullptr && m_backend->jobs() != nullptr) {
        connect(m_backend->jobs(), &oic::jobs::JobManager::started, this, &JobController::jobStarted);
        connect(m_backend->jobs(), &oic::jobs::JobManager::finished, this,
                [this](const QString &jobId, const oic::jobs::JobOutcome &outcome) {
                    QStringList relPaths;
                    for (const oic::jobs::AssetRef &asset : outcome.assets)
                        relPaths.append(asset.relPath);
                    emit jobFinished(jobId, outcome.status, outcome.error, relPaths);
                });
    }
}

QString JobController::generate(const QString &profileName, const QString &model, const QString &prompt,
                                const QString &size, int n, const QString &protocolHint, QString *error)
{
    if (m_backend == nullptr || m_backend->database() == nullptr || m_backend->jobs() == nullptr) {
        if (error != nullptr)
            *error = QStringLiteral("后端未就绪");
        return {};
    }

    QString profileError;
    const QList<oic::store::Profile> profiles = m_backend->database()->profiles(&profileError);
    if (!profileError.isEmpty()) {  // a store failure must not be mis-reported as "no such profile"
        if (error != nullptr)
            *error = profileError;
        return {};
    }
    oic::store::Profile profile;
    bool found = false;
    for (const oic::store::Profile &candidate : profiles) {
        if (candidate.name == profileName) {
            profile = candidate;
            found = true;
            break;
        }
    }
    if (!found) {
        if (error != nullptr)
            *error = QStringLiteral("未找到 profile「%1」").arg(profileName);
        return {};
    }

    oic::jobs::JobSpec spec;
    spec.profileName = profileName;
    spec.baseUrl = profile.baseUrl;
    spec.credentialTarget = profile.credentialTarget;
    spec.protocolHint = protocolHint.isEmpty() ? profile.protocol : protocolHint;
    spec.timeoutSeconds = profile.timeoutSeconds > 0 ? profile.timeoutSeconds : oic::limits::kDefaultTimeoutSeconds;
    spec.image.model = model;
    spec.image.prompt = prompt;
    spec.image.size = size;
    spec.image.n = n;
    spec.image.editMode = false;

    QString submitError;
    const QString jobId = m_backend->jobs()->submit(spec, &submitError);
    if (jobId.isEmpty() && error != nullptr)
        *error = submitError;
    return jobId;
}

QString JobController::generateJob(const QString &profileName, const QString &model, const QString &prompt,
                                   const QString &size, int n, const QString &protocolHint)
{
    QString error;
    const QString jobId = generate(profileName, model, prompt, size, n, protocolHint, &error);
    m_lastError = error;
    return jobId;
}

bool JobController::cancel(const QString &jobId)
{
    return m_backend != nullptr && m_backend->jobs() != nullptr && m_backend->jobs()->cancel(jobId);
}

}  // namespace oic::app
