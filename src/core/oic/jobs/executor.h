// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

#include "oic/core/limits.h"
#include "oic/net/transport.h"
#include "oic/protocol/imageprotocol.h"
#include "oic/protocol/parsers.h"

namespace oic::jobs {

// One submitted unit of work. Carries the credential *target*, never the key: the
// executor reads the secret just-in-time so it never lands in a persisted struct
// (SPEC 6.3).
struct JobSpec {
    QString jobId;           // optional pre-assigned id (JobManager hands one back at submit); empty → runJob generates
    QString profileName;
    QString baseUrl;
    QString protocolHint;    // "auto" | openai | grok | gemini (SPEC 5.1)
    QString credentialTarget;
    protocol::ImageRequest image;
    int timeoutSeconds = limits::kDefaultTimeoutSeconds;
};

struct AssetRef {
    QString id;
    QString relPath;    // relative to the assets root, as AssetStore reports it
    QString mime;
    int width = 0;
    int height = 0;
    qint64 bytes = 0;
};

struct JobOutcome {
    QString jobId;
    QString status;          // succeeded | failed | cancelled (store::Job.status)
    QString error;           // user-facing, empty on success
    QString diagnosticJson;  // redacted blob, empty on success
    QString endpoint;        // original-host form, never the pinned IP
    QString clientRequestId;
    qint64 durationMs = 0;
    QList<AssetRef> assets;  // written to disk and recorded in the store
    QStringList remoteOnly;  // SPEC 7.1 url-download-failed links kept as-is
};

// Injection seams, mirroring net::Resolver and protocol::MediaFetcher. makeDefaultDeps()
// wires the production implementations; tests replace them.
using Sender = std::function<net::Reply(const net::Request &)>;
using SecretReader = std::function<QByteArray(const QString &target, QString *error, bool *absent)>;
using Clock = std::function<qint64()>;  // epoch milliseconds

// Paths, not objects. A store::Database names its SQL connection after the constructing
// thread (store/database.cpp: "oic-store-<threadId>"), so runJob must build its own on
// the calling thread instead of receiving one created elsewhere.
struct JobDeps {
    QString databasePath;
    QString assetsRoot;
    qint64 assetQuotaBytes = 0;
    int assetLockTimeoutMs = 5000;

    Sender sender;
    SecretReader secrets;
    protocol::MediaFetcher fetcher;
    protocol::MediaLimits mediaLimits;  // default = frozen SPEC 3 caps; a seam so the cumulative-size path is testable
    Clock clock;
};

// Cooperative cancellation. Transport::send is blocking and cannot be interrupted
// mid-request (SPEC 9.2), so a cancel takes effect at the next checkpoint.
class CancelToken {
public:
    void cancel() { m_flag.store(true); }
    bool cancelled() const { return m_flag.load(); }

private:
    std::atomic_bool m_flag{false};
};

// Synchronous, thread-free core: runs one job end to end on the calling thread and
// persists every status transition. JobManager (jobmanager.h) pools this for the GUI;
// the MCP server calls it directly with a deadline (SPEC 8.2).
JobOutcome runJob(const JobSpec &spec, const JobDeps &deps, CancelToken *cancel, QString *error);

// Fills sender/secrets/fetcher/clock with the production implementations. The transport
// settings (trusted hosts, resolver) pass straight through to the shared net::Transport
// used for both the provider call and SPEC 7.1 url-branch downloads.
JobDeps makeDefaultDeps(const QString &databasePath, const QString &assetsRoot, qint64 quotaBytes,
                        const net::Transport::Settings &transport = {});

}  // namespace oic::jobs
