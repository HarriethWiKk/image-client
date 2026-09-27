// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QByteArray>
#include <QString>

namespace oic::core {

// Strict, size-bounded base64 decoding.
//
// Two failures this guards against are both real, not hypothetical: a lax
// decoder silently drops every character outside the alphabet, so an HTML error
// page "decodes" into bytes that then get written out as a plausible-looking
// image; and an unbounded decode lets a hostile or merely oversized upstream
// allocate at will.
QByteArray decodeBase64Limited(const QString &encoded, qint64 limit, const QString &label,
                               bool *ok, QString *error);

}  // namespace oic::core
