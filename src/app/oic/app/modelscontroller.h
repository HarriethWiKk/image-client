// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace oic::app {

class Backend;

// The generate view's model picker. It fetches the selected provider's live catalog from
// /v1/models (SPEC 9.1 model switching) through the SAME transport + credential seams the
// job pool uses, off the GUI thread. The list is provider-scoped: refreshing for another
// provider replaces it, so two providers that expose a same-named model stay distinct --
// the (provider, model) pair is what generation submits. Offline-first: the last-known
// allowed_models (or the profile default) is shown immediately and updated when the fetch
// returns; a successful fetch is written back to allowed_models so the next launch has it.
class ModelsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList models READ models NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString provider READ provider NOTIFY changed)

public:
    explicit ModelsController(Backend *backend, QObject *parent = nullptr);

    QStringList models() const { return m_models; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    QString provider() const { return m_provider; }
    Q_INVOKABLE QString lastError() const { return m_error; }

    // Fetch (or seed) the model list for the named profile. A response that arrives after the
    // provider has since been switched is dropped via a serial guard.
    Q_INVOKABLE void refresh(const QString &profileName);

Q_SIGNALS:
    void changed();

private:
    Backend *m_backend;
    QStringList m_models;
    QString m_error;
    QString m_provider;
    bool m_loading = false;
    quint64 m_serial = 0;
};

}  // namespace oic::app
