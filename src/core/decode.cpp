// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/core/decode.h"

#include <QByteArray>
#include <QRegularExpression>

namespace oic::core {
namespace {

const QRegularExpression &whitespace()
{
    static const QRegularExpression pattern(QStringLiteral("\\s+"));
    return pattern;
}

}  // namespace

QByteArray decodeBase64Limited(const QString &encoded, qint64 limit, const QString &label,
                               bool *ok, QString *error)
{
    if (ok != nullptr)
        *ok = false;

    // Whitespace is removed rather than rejected: MIME-wrapped base64 legitimately
    // contains newlines. Removing it first still lets the strict decoder reject the
    // characters an HTML error page is made of.
    const QString value = QString(encoded).remove(whitespace());

    // Reject on the encoded length before allocating, so an oversized payload is
    // never decoded. 4/3 plus the padding slack is the tightest useful bound.
    const qsizetype maxEncodedLength = 4 * ((limit + 2) / 3) + 4;
    if (static_cast<qint64>(value.size()) > maxEncodedLength) {
        if (error != nullptr)
            *error = QStringLiteral("%1 超过 %2 字节限制").arg(label).arg(limit);
        return {};
    }

    // fromBase64Encoding is used over fromBase64 deliberately: with the abort flag
    // the plain call returns whatever decoded before the first bad character, so
    // "YWJj<html>" would quietly yield a truncated image. The status distinguishes
    // a clean decode from a stopped-one.
    const QByteArray::FromBase64Result result =
        QByteArray::fromBase64Encoding(value.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (result.decodingStatus != QByteArray::Base64DecodingStatus::Ok) {
        if (error != nullptr)
            *error = QStringLiteral("%1 不是合法的 Base64 数据").arg(label);
        return {};
    }

    const QByteArray decoded = result.decoded;

    if (static_cast<qint64>(decoded.size()) > limit) {
        if (error != nullptr)
            *error = QStringLiteral("%1 超过 %2 字节限制").arg(label).arg(limit);
        return {};
    }

    if (ok != nullptr)
        *ok = true;
    return decoded;
}

}  // namespace oic::core
