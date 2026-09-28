// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/app/historymodel.h"

#include "oic/app/backend.h"

namespace oic::app {

HistoryModel::HistoryModel(Backend *backend, QObject *parent)
    : QAbstractListModel(parent), m_backend(backend)
{
    refresh();
}

int HistoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_jobs.size());
}

QHash<int, QByteArray> HistoryModel::roleNames() const
{
    return {
        {IdRole, "jobId"},         {StatusRole, "status"},   {ModeRole, "mode"},
        {ModelRole, "model"},      {PromptRole, "prompt"},   {CreatedAtRole, "createdAt"},
        {UpdatedAtRole, "updatedAt"}, {DurationMsRole, "durationMs"}, {PinnedRole, "pinned"},
        {ErrorRole, "error"},      {EndpointRole, "endpoint"}, {ThumbnailRole, "thumbnail"},
    };
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_jobs.size())
        return {};
    const oic::store::Job &job = m_jobs.at(index.row());
    switch (role) {
    case IdRole:
        return job.id;
    case StatusRole:
        return job.status;
    case ModeRole:
        return job.mode;
    case ModelRole:
        return job.model;
    case PromptRole:
        return job.prompt;
    case CreatedAtRole:
        return job.createdAt;
    case UpdatedAtRole:
        return job.updatedAt;
    case DurationMsRole:
        return job.durationMs;
    case PinnedRole:
        return job.pinned;
    case ErrorRole:
        return job.error;
    case EndpointRole:
        return job.endpoint;
    case ThumbnailRole:
        return index.row() < m_thumbnails.size() ? QVariant(m_thumbnails.at(index.row())) : QVariant();
    }
    return {};
}

void HistoryModel::refresh(int limit, int offset)
{
    if (m_backend == nullptr || m_backend->database() == nullptr)
        return;
    QString error;
    beginResetModel();
    m_jobs = m_backend->database()->listJobs(limit, offset, false, &error);
    m_thumbnails.clear();
    reloadThumbnails();
    endResetModel();
    emit countChanged();
}

void HistoryModel::reloadThumbnails()
{
    QString error;
    for (const oic::store::Job &job : m_jobs) {
        QString thumb;
        const QList<oic::store::Asset> assets = m_backend->database()->assetsForJob(job.id, &error);
        for (const oic::store::Asset &asset : assets) {
            if (asset.role == QLatin1String("result")) {  // first result asset (ordinal-ordered)
                thumb = asset.relPath;
                break;
            }
        }
        m_thumbnails.append(thumb);
    }
}

QString HistoryModel::jobIdAt(int row) const
{
    return (row >= 0 && row < m_jobs.size()) ? m_jobs.at(row).id : QString();
}

bool HistoryModel::pin(int row, bool pinned)
{
    const QString id = jobIdAt(row);
    if (id.isEmpty() || m_backend->database() == nullptr)
        return false;
    QString error;
    if (!m_backend->database()->setPinned(id, pinned, &error))
        return false;
    m_jobs[row].pinned = pinned;  // reflect the change in the cache too, else data() goes stale
    emit dataChanged(index(row), index(row), {PinnedRole});
    return true;
}

bool HistoryModel::removeAt(int row)
{
    const QString id = jobIdAt(row);
    if (id.isEmpty() || m_backend->database() == nullptr)
        return false;
    QString error;
    if (!m_backend->database()->softDeleteJob(id, 0, &error))
        return false;
    refresh();
    return true;
}

}  // namespace oic::app
