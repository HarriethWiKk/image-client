// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace oic::secret {

// SPEC 6.3: the Windows Credential Manager is the only place a key is kept.
// There is deliberately no file fallback -- falling back to a file would put back
// the plaintext this design exists to remove.
//
// Target names are "image-client/profile/<name>"; the profile name is restricted
// so one user cannot craft a name that overwrites another application's entry.

// Empty when the profile name is unusable (error then describes why).
QString targetNameFor(const QString &profileName, QString *error);

// Rejects secrets that cannot survive a round trip, and never leaves a partially
// written credential behind on failure.
bool writeSecret(const QString &targetName, const QByteArray &utf8Secret, QString *error);

// Returns an empty byte array when nothing is stored. absent is set when that is
// the reason, as opposed to a failure that must be reported.
QByteArray readSecret(const QString &targetName, QString *error, bool *absent = nullptr);

// Idempotent: removing something that is not there succeeds and reports false in
// existed when the pointer is given.
bool eraseSecret(const QString &targetName, QString *error, bool *existed = nullptr);

// Every profile credential this application owns, for listing and cleanup.
QStringList listTargetNames(QString *error);

// Largest secret the OS accepts, measured and documented in SPEC 6.3.
int maxSecretBytes();

}  // namespace oic::secret
