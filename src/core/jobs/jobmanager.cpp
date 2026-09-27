// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/jobs/jobmanager.h"

#include <QDateTime>
#include <QMutexLocker>
#include <QUuid>

namespace oic::jobs {

JobManager::JobManager(JobDeps deps, Config config, QObject *parent)
    : QObject(parent), m_deps(std::move(deps)), m_config(config)
{
    // Queued cross-thread delivery copies the payload by name, so it must be registered.
    qRegisterMetaType<JobOutcome>("oic::jobs::JobOutcome");

    const int workers = qMax(1, m_config.maxConcurrent);
    m_workers.reserve(workers);
    for (int i = 0; i < workers; ++i) {
        QThread *thread = QThread::create([this] { workerLoop(); });
        m_workers.append(thread);
        thread->start();
    }
}

JobManager::~JobManager()
{
    {
        QMutexLocker lock(&m_mutex);
        m_shutdown = true;
    }
    m_cv.wakeAll();
    for (QThread *thread : m_workers) {
        thread->wait();
        delete thread;
    }
}

QString JobManager::submit(const JobSpec &spec, QString *error)
{
    const qint64 now = m_deps.clock ? m_deps.clock() : QDateTime::currentMSecsSinceEpoch();
    QString jobId;
    {
        QMutexLocker lock(&m_mutex);
        evictExpiredLocked(now);
        if (m_queue.size() >= static_cast<qsizetype>(m_config.maxPending)) {
            if (error)
                *error = QStringLiteral("等待队列已满（上限 %1），请稍后再试").arg(m_config.maxPending);
            return QString();
        }
        if (m_tracked.size() >= static_cast<qsizetype>(m_config.maxLive)) {
            if (error)
                *error = QStringLiteral("内存中活跃任务已达上限（%1）").arg(m_config.maxLive);
            return QString();
        }

        Entry entry;
        entry.jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        entry.spec = spec;
        entry.spec.jobId = entry.jobId;  // persist under the id we hand back now
        entry.cancel = std::make_shared<CancelToken>();

        Tracked tracked;
        tracked.cancel = entry.cancel;
        m_tracked.insert(entry.jobId, tracked);

        jobId = entry.jobId;
        m_queue.enqueue(entry);
    }
    m_cv.wakeOne();
    emit submitted(jobId);
    return jobId;
}

bool JobManager::cancel(const QString &jobId)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_tracked.find(jobId);
    if (it == m_tracked.end() || !it->cancel)
        return false;
    it->cancel->cancel();
    return true;
}

int JobManager::pendingCount() const
{
    QMutexLocker lock(&m_mutex);
    return static_cast<int>(m_queue.size());
}

int JobManager::runningCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_running;
}

int JobManager::liveCount() const
{
    QMutexLocker lock(&m_mutex);
    return static_cast<int>(m_tracked.size());
}

void JobManager::workerLoop()
{
    for (;;) {
        Entry entry;
        {
            QMutexLocker lock(&m_mutex);
            for (;;) {
                if (m_shutdown)
                    return;
                if (!m_queue.isEmpty())
                    break;
                m_cv.wait(&m_mutex);
            }
            entry = m_queue.dequeue();
            ++m_running;
        }

        emit started(entry.jobId);
        QString error;
        const JobOutcome outcome = runJob(entry.spec, m_deps, entry.cancel.get(), &error);
        const qint64 now = m_deps.clock ? m_deps.clock() : QDateTime::currentMSecsSinceEpoch();

        {
            QMutexLocker lock(&m_mutex);
            --m_running;
            auto it = m_tracked.find(entry.jobId);
            if (it != m_tracked.end())
                it->finishedAt = now;
            evictExpiredLocked(now);
        }
        emit finished(entry.jobId, outcome);
    }
}

void JobManager::evictExpiredLocked(qint64 nowMs)
{
    const qint64 ttlMs = static_cast<qint64>(m_config.ttlSeconds) * 1000;
    for (auto it = m_tracked.begin(); it != m_tracked.end();) {
        if (it->finishedAt != 0 && nowMs - it->finishedAt > ttlMs)
            it = m_tracked.erase(it);
        else
            ++it;
    }
}

}  // namespace oic::jobs
