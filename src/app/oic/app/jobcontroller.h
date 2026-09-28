// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include "oic/store/database.h"

namespace oic::app {

class Backend;

// Bridges the generate form to the job pool. It resolves the named profile (base URL,
// protocol, credential target) from the store, builds a JobSpec, and hands it to
// JobManager; the pool's started/finished signals are re-emitted in a QML-friendly shape
// (status string + error + asset relPaths). The GUI thread never touches the network --
// runJob does, on a worker.
class JobController : public QObject {
    Q_OBJECT
    // Staged reference sources (file:// or data: urls), in order. Exposed as a NOTIFY property
    // so the QML Repeater binds directly to it and repaints on every controller mutation -- the
    // earlier imperative "clear+append a mirror ListModel" left the view stale until some later
    // action forced a redraw. A paste can stage several at once, which no single return value fits.
    Q_PROPERTY(QStringList referenceSources READ referenceSources NOTIFY referencesChanged)

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

    // Reference images staged for an edit (image-to-image) job. The payloads live here, not
    // in QML, so a 10 MB image never becomes a JS string. addReferencePath reads + probes +
    // caps the file and returns its source url (empty + lastError on rejection);
    // addReferenceFromClipboard turns a clipboard image or file list into references. Every
    // mutation emits referencesChanged(). generateEdit submits with editMode set and clears
    // the staged references on success.
    Q_INVOKABLE QString addReferencePath(const QString &urlOrPath);
    Q_INVOKABLE bool addReferenceFromClipboard();
    Q_INVOKABLE bool removeReference(const QString &sourceUrl);
    Q_INVOKABLE void clearReferences();
    Q_INVOKABLE int referenceCount() const { return m_references.size(); }
    QStringList referenceSources() const;
    Q_INVOKABLE QString generateEdit(const QString &profileName, const QString &model, const QString &prompt,
                                     const QString &size, int n, const QString &protocolHint = QString());

Q_SIGNALS:
    void jobStarted(const QString &jobId);
    void jobFinished(const QString &jobId, const QString &status, const QString &error,
                     const QStringList &assetRelPaths);
    void referencesChanged();

private:
    // A staged reference image, already read + probe-validated on the GUI thread.
    struct PendingReference {
        QString source;   // url the UI shows: file:// for a path, data: for a paste
        QString name;     // multipart filename handed to the provider
        QString dataUrl;  // canonical data: URL used to build the request
        QString mime;
    };

    bool resolveProfile(const QString &profileName, oic::store::Profile *out, QString *error);
    // Shared submit path for generate() and generateEdit(). An empty references list with
    // editMode=false is text-to-image; a populated list with editMode=true is image-to-image.
    QString submitImage(const QString &profileName, const QString &model, const QString &prompt, const QString &size,
                        int n, const QString &protocolHint, const QList<PendingReference> &references, bool editMode,
                        QString *error);

    Backend *m_backend;
    QList<PendingReference> m_references;
    QString m_lastError;
};

}  // namespace oic::app
