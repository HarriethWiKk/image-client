// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QImage>
#include <QQuickImageProvider>
#include <QString>

namespace oic::app {

// Serves image://asset/<relPath> from the asset store. Decoding honours the SPEC 6.4
// safety intent: a *finite* allocation limit (in MiB -- Qt treats 0 as "check disabled", so
// it must never be 0). read() decodes only the current frame, so an animated container
// cannot allocate every frame. A missing file, or one with no decoder here (e.g. webp on a
// Qt without the imageformats webp plugin -- SPEC 6.5), returns an opaque placeholder so
// the GUI can show "saved, cannot preview" rather than nothing.
class AssetImageProvider : public QQuickImageProvider {
public:
    explicit AssetImageProvider(QString assetsRoot, int allocationLimitMiB = kAllocationLimitMiB);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    // Default cap. 256 MiB matches Qt's own default and is ~8x the decoded bytes of the
    // largest real result here (a 4K RGBA image is ~33 MiB), while still refusing a
    // decompression bomb. Must be finite -- 0 would DISABLE the check (SPEC 6.4).
    static constexpr int kAllocationLimitMiB = 256;

private:
    static QImage placeholder(const QSize &size);

    QString m_assetsRoot;
    int m_allocationLimitMiB;
};

}  // namespace oic::app
