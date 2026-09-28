// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/app/modelscontroller.h"

#include <QtConcurrent>
#include <QtGlobal>
#include <QUrl>
#include <QVariantMap>

#include "oic/app/backend.h"
#include "oic/core/limits.h"
#include "oic/jobs/executor.h"
#include "oic/net/transport.h"
#include "oic/protocol/imageprotocol.h"
#include "oic/store/database.h"

namespace oic::app {

namespace {
// A model catalog is metadata: a bounded, shorter timeout than an image job, so a dead
// endpoint does not leave the picker spinning for the full (possibly 300 s) job timeout.
constexpr int kModelsTimeoutSeconds = 30;
}  // namespace

ModelsController::ModelsController(Backend *backend, QObject *parent) : QObject(parent), m_backend(backend) {}

void ModelsController::refresh(const QString &profileName)
{
    m_provider = profileName;
    m_error.clear();

    oic::store::Profile profile;
    bool found = false;
    if (m_backend != nullptr && m_backend->database() != nullptr) {
        QString storeError;
        const QList<oic::store::Profile> profiles = m_backend->database()->profiles(&storeError);
        for (const oic::store::Profile &candidate : profiles) {
            if (candidate.name == profileName) {
                profile = candidate;
                found = true;
                break;
            }
        }
    }

    // Offline seed first: the picker is usable before (and even if) the network call resolves.
    QStringList seed = profile.allowedModels;
    if (seed.isEmpty() && !profile.imageModel.trimmed().isEmpty())
        seed.append(profile.imageModel.trimmed());
    m_models = seed;

    if (!found) {
        m_loading = false;
        m_error = QStringLiteral("未找到 Provider「%1」").arg(profileName);
        emit changed();
        return;
    }

    const oic::jobs::JobDeps &deps = m_backend->deps();
    QString secretError;
    bool absent = true;
    const QByteArray keyBytes = deps.secrets ? deps.secrets(profile.credentialTarget, &secretError, &absent)
                                             : QByteArray();
    if (absent || keyBytes.isEmpty()) {
        m_loading = false;
        m_error = QStringLiteral("未配置密钥：使用上次已知模型");
        emit changed();
        return;
    }

    QString prepError;
    const oic::protocol::OutgoingRequest out =
        oic::protocol::prepareModelList(profile.baseUrl, QString::fromUtf8(keyBytes), profile.protocol, &prepError);
    if (!out.ok) {
        m_loading = false;
        m_error = prepError.isEmpty() ? QStringLiteral("无法构造模型列表请求") : prepError;
        emit changed();
        return;
    }

    oic::net::Request request;
    request.method = out.method;
    request.url = QUrl(out.absoluteUrl);
    request.headers = out.headers;
    request.timeoutSeconds =
        profile.timeoutSeconds > 0 ? qMin(profile.timeoutSeconds, kModelsTimeoutSeconds) : kModelsTimeoutSeconds;

    const oic::jobs::Sender sender = deps.sender;  // copy the seam into the worker
    const QString protocol = profile.protocol;
    m_loading = true;
    ++m_serial;
    const quint64 serial = m_serial;

    // Blocking send runs on the concurrent pool; the continuation is delivered on this object's
    // (the GUI) thread and is skipped if the controller is gone -- so m_* are touched safely.
    QtConcurrent::run([sender, request, protocol]() -> QVariantMap {
                            QVariantMap result;
                            QStringList models;
                            QString error;
                            int status = 0;
                            if (sender) {
                                const oic::net::Reply reply = sender(request);
                                status = reply.status;
                                if (!reply.error.isEmpty()) {
                                    error = reply.error;
                                } else {
                                    models = oic::protocol::parseModelList(protocol, reply.body, &error);
                                    if (models.isEmpty() && error.isEmpty() && reply.status < 200)
                                        error = QStringLiteral("上游返回状态 %1").arg(reply.status);
                                }
                            } else {
                                error = QStringLiteral("后端未就绪");
                            }
                            result.insert(QStringLiteral("models"), models);
                            result.insert(QStringLiteral("error"), error);
                            result.insert(QStringLiteral("status"), status);
                            return result;
                        })
            .then(this, [this, serial](const QVariantMap &result) {
                if (serial != m_serial)  // provider switched while in flight: drop the stale list
                    return;
                m_loading = false;
                const QStringList models = result.value(QStringLiteral("models")).toStringList();
                const QString error = result.value(QStringLiteral("error")).toString();
                if (!models.isEmpty()) {
                    m_models = models;
                    m_error.clear();
                    if (m_backend != nullptr && m_backend->database() != nullptr) {
                        QString storeError;
                        for (oic::store::Profile stored : m_backend->database()->profiles(&storeError)) {
                            if (stored.name == m_provider) {
                                stored.allowedModels = models;
                                QString writeError;
                                m_backend->database()->writeProfile(stored, &writeError);  // best-effort last-known cache
                                break;
                            }
                        }
                    }
                } else if (!error.isEmpty()) {
                    m_error = error;  // keep the offline seed already published
                }
                emit changed();
            });
}

}  // namespace oic::app
