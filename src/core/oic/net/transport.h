// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QString>
#include <QUrl>

#include <functional>

#include "oic/core/limits.h"

namespace oic::net {

// Name resolution seam. Production resolves through the system, while tests hand
// back a fixed address so the pinned code path runs without any network.
using Resolver = std::function<QList<QHostAddress>(const QString &hostname)>;

struct Header {
    QByteArray name;
    QByteArray value;
};

struct Request {
    QByteArray method = QByteArrayLiteral("GET");
    QUrl url;
    QList<Header> headers;
    QByteArray body;
    int timeoutSeconds = limits::kDefaultTimeoutSeconds;
    qint64 maxResponseBytes = limits::kMaxUpstreamJsonBytes;
};

struct Reply {
    // status > 0 means a response was received, including non-2xx: the caller has
    // to read the body either way to build the diagnostic. error is only set when
    // the request never produced a usable response (policy refusal, connect
    // failure, timeout, oversized body).
    int status = 0;
    QByteArray body;
    QList<Header> headers;
    QString contentType;
    QUrl effectiveUrl;  // original-host form; the pinned IP never reaches logs

    QString error;      // user-facing text, empty when a response was received
    QString detail;     // transport-level text, for the diagnostic panel
    bool retryable = false;  // worth trying the next candidate endpoint
    int hops = 0;       // redirects followed
};

// Blocking by design: §9.2 keeps network work off the GUI thread and runs each
// job on a worker, so a nested event loop is the simplest correct shape here.
class Transport {
public:
    struct Settings {
        // §8.3 exemptions from the publicly-routable rule, e.g. a local gateway.
        QStringList trustedHosts;
        Resolver resolver;  // empty → systemResolve()
    };

    explicit Transport(Settings settings = {});

    Reply send(const Request &request) const;

    static QList<QHostAddress> systemResolve(const QString &hostname);

private:
    Settings settings_;
};

}  // namespace oic::net
