// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace oic::app {

class Backend;

// Bridges the generate form to the job pool. It resolves the named profile (base URL,
// protocol, credential target) from the store, builds a JobSpec, and hands it to
// JobManager; the pool's started/finished signals are re-emitted in a QML-friendly shape
// (status string + error + asset relPaths). The GUI thread never touches the network --
// runJob does, on a worker.
class JobController : public QObject {
    Q_OBJECT
public:
    explicit JobController(Backend *backend, QObject *parent = nullptr);

    QString generate(const QString &profileName, const QString &model, const QString &prompt, const QString &size,
                     int n, const QString &protocolHint, QString *error);

    // QML-facing wrapper: no out-param, so QML can call it; a rejected submit (unknown
    // profile, full queue, store error) leaves lastError() set and returns an empty id.
    Q_INVOKABLE QString generateJob(const QString &profileName, const QString &model, const QString &prompt,
                                    const QString &size, int n, const QString &protocolHint = QString());
    Q_INVOKABLE bool cancel(const QString &jobId);
    Q_INVOKABLE QString lastError() const { return m_lastError; }

Q_SIGNALS:
    void jobStarted(const QString &jobId);
    void jobFinished(const QString &jobId, const QString &status, const QString &error,
                     const QStringList &assetRelPaths);

private:
    Backend *m_backend;
    QString m_lastError;
};

}  // namespace oic::app
