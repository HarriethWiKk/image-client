// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QList>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QThread>
#include <QWaitCondition>

#include <memory>

#include "oic/core/limits.h"
#include "oic/jobs/executor.h"

namespace oic::jobs {

// Bounded worker pool that runs runJob() off the caller's thread and reports state
// transitions back through queued signals (SPEC 9.2: the GUI thread does no network).
// The MCP server does not use this; it calls runJob() directly with a deadline (SPEC 8.2).
//
// Each worker calls runJob(), which builds its own store::Database on that thread, so the
// thread-bound SQL connection (oic-store-<threadId>) is correct with no extra plumbing.
// Concurrent writers coexist through WAL + busy_timeout (SPEC 6.2).
class JobManager : public QObject {
    Q_OBJECT
public:
    struct Config {
        int maxConcurrent = limits::kMaxConcurrentJobs;  // worker threads
        int maxPending = limits::kMaxPendingJobs;        // queued, not yet running
        int maxLive = limits::kMaxLiveJobs;              // in-memory tracked entries
        int ttlSeconds = limits::kJobTtlSeconds;         // retention of finished entries
    };

    explicit JobManager(JobDeps deps, Config config = {}, QObject *parent = nullptr);
    ~JobManager() override;

    JobManager(const JobManager &) = delete;
    JobManager &operator=(const JobManager &) = delete;

    // Returns the new job id, or an empty string with *error set when the pending queue or
    // the live cap is full. Fail-fast: a bounded pool that blocks the caller would make the
    // bound meaningless (SPEC 3).
    QString submit(const JobSpec &spec, QString *error);

    // Requests cooperative cancellation. True if the job is still tracked and was signalled.
    // The in-flight Transport::send cannot be interrupted, so it takes effect at the next
    // checkpoint (SPEC 9.2).
    bool cancel(const QString &jobId);

    int pendingCount() const;
    int runningCount() const;
    int liveCount() const;

Q_SIGNALS:
    void submitted(const QString &jobId);
    void started(const QString &jobId);
    void finished(const QString &jobId, oic::jobs::JobOutcome outcome);

private:
    struct Entry {
        QString jobId;
        JobSpec spec;
        std::shared_ptr<CancelToken> cancel;
    };
    struct Tracked {
        std::shared_ptr<CancelToken> cancel;
        qint64 finishedAt = 0;  // 0 while not yet finished
    };

    void workerLoop();
    void evictExpiredLocked(qint64 nowMs);  // caller holds m_mutex

    JobDeps m_deps;
    Config m_config;

    mutable QMutex m_mutex;
    QWaitCondition m_cv;
    QQueue<Entry> m_queue;
    QMap<QString, Tracked> m_tracked;
    int m_running = 0;
    bool m_shutdown = false;

    QList<QThread *> m_workers;
};

}  // namespace oic::jobs

Q_DECLARE_METATYPE(oic::jobs::JobOutcome)
