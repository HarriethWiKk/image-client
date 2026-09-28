// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

#include "oic/store/database.h"

namespace oic::app {

class Backend;

// Read model over the persisted history (SPEC 6.2). Newest first; soft-deleted rows hidden.
// Thumbnails resolve to the first result asset's stored relPath, which QML feeds to
// image://asset/<relPath> via the AssetImageProvider.
class HistoryModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY countChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        StatusRole,
        ModeRole,
        ModelRole,
        PromptRole,
        CreatedAtRole,
        UpdatedAtRole,
        DurationMsRole,
        PinnedRole,
        ErrorRole,
        EndpointRole,
        ThumbnailRole,
    };

    explicit HistoryModel(Backend *backend, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_jobs.size()); }
    bool hasMore() const { return m_hasMore; }

    // Load (or reload) the newest page. `limit` is the page size; hasMore stays true while a full
    // page came back, so the view can pull the next page with loadMore().
    Q_INVOKABLE void refresh(int limit = 60, int offset = 0);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE QString jobIdAt(int row) const;
    Q_INVOKABLE bool pin(int row, bool pinned);
    Q_INVOKABLE bool removeAt(int row);  // soft delete
    // relPaths of a job's result assets (in ordinal order), for the lightbox / detail view.
    Q_INVOKABLE QStringList resultRelPaths(int row) const;

Q_SIGNALS:
    void countChanged();

private:
    void reloadThumbnails();

    Backend *m_backend;
    QList<oic::store::Job> m_jobs;
    QList<QString> m_thumbnails;
    int m_limit = 60;   // page size used by the last refresh/loadMore
    int m_offset = 0;   // rows fetched so far (next page starts here)
    bool m_hasMore = false;
};

}  // namespace oic::app
