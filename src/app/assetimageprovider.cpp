// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/app/assetimageprovider.h"

#include <QColor>
#include <QDir>
#include <QImageReader>

namespace oic::app {

AssetImageProvider::AssetImageProvider(QString assetsRoot, int allocationLimitMiB)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_assetsRoot(std::move(assetsRoot))
    , m_allocationLimitMiB(allocationLimitMiB)
{
}

QImage AssetImageProvider::placeholder(const QSize &size)
{
    const QSize resolved = (size.isValid() && !size.isEmpty()) ? size : QSize(256, 256);
    QImage image(resolved, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(0x20, 0x20, 0x20));
    return image;
}

QImage AssetImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const auto givePlaceholder = [&](const QSize &hint) {
        QImage image = placeholder(hint);
        if (size != nullptr)
            *size = image.size();
        return image;
    };

    // The id is an <asset relPath> inside the store. Refuse anything that climbs out of the
    // assets root (traversal), which would otherwise let a crafted id read arbitrary files.
    const QString root = QDir::cleanPath(m_assetsRoot);
    const QString absolute = QDir::cleanPath(QDir(m_assetsRoot).filePath(id));
    if (!absolute.startsWith(root + QLatin1Char('/')))
        return givePlaceholder(requestedSize);

    QImageReader reader(absolute);
    // SPEC 6.4: a *finite* allocation cap (in MiB). Qt has no setImageCountLimit; read()
    // decodes only the current frame of an animated container, so a multi-frame GIF/TIFF
    // cannot allocate every frame -- and setAllocationLimit bounds that single frame.
    // setAllocationLimit(0) would DISABLE the check, so it must never be 0.
    reader.setAllocationLimit(m_allocationLimitMiB);

    QImage image = reader.read();
    if (image.isNull())
        return givePlaceholder(requestedSize);  // missing, corrupt, or no decoder (e.g. webp)

    if (requestedSize.isValid() && !requestedSize.isEmpty())
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    if (size != nullptr)
        *size = image.size();
    return image;
}

}  // namespace oic::app
