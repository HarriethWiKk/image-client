// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/store/imageprobe.h"

#include <QtGlobal>

namespace oic::store {
namespace {

quint16 be16(const QByteArray &d, int offset)
{
    return static_cast<quint16>((static_cast<quint8>(d.at(offset)) << 8) | static_cast<quint8>(d.at(offset + 1)));
}

quint32 be32(const QByteArray &d, int offset)
{
    return (static_cast<quint32>(static_cast<quint8>(d.at(offset))) << 24)
           | (static_cast<quint32>(static_cast<quint8>(d.at(offset + 1))) << 16)
           | (static_cast<quint32>(static_cast<quint8>(d.at(offset + 2))) << 8)
           | static_cast<quint32>(static_cast<quint8>(d.at(offset + 3)));
}

quint16 le16(const QByteArray &d, int offset)
{
    return static_cast<quint16>(static_cast<quint8>(d.at(offset))
                                | (static_cast<quint16>(static_cast<quint8>(d.at(offset + 1))) << 8));
}

quint32 le32(const QByteArray &d, int offset)
{
    return static_cast<quint32>(static_cast<quint8>(d.at(offset)))
           | (static_cast<quint32>(static_cast<quint8>(d.at(offset + 1))) << 8)
           | (static_cast<quint32>(static_cast<quint8>(d.at(offset + 2))) << 16)
           | (static_cast<quint32>(static_cast<quint8>(d.at(offset + 3))) << 24);
}

bool has(const QByteArray &d, int offset, int length)
{
    return d.size() - offset >= length;
}

ImageInfo make(const QString &mime, int width, int height)
{
    ImageInfo info;
    info.recognized = true;
    info.mime = mime;
    info.width = width;
    info.height = height;
    return info;
}

bool probePng(const QByteArray &d, ImageInfo *out)
{
    // IHDR is required to be the first chunk: signature(8) + length(4) + "IHDR"(4).
    if (!has(d, 0, 24) || !d.startsWith("\x89PNG\r\n\x1a\n")) {
        return false;
    }
    if (!d.mid(12, 4).startsWith("IHDR")) {
        return false;
    }
    const quint32 width = be32(d, 16);
    const quint32 height = be32(d, 20);
    if (width == 0 || height == 0) {
        return false;
    }
    *out = make(QStringLiteral("image/png"), static_cast<int>(width), static_cast<int>(height));
    return true;
}

bool probeGif(const QByteArray &d, ImageInfo *out)
{
    if (!has(d, 0, 10) || (!d.startsWith("GIF87a") && !d.startsWith("GIF89a"))) {
        return false;
    }
    const quint32 width = le16(d, 6);
    const quint32 height = le16(d, 8);
    if (width == 0 || height == 0) {
        return false;
    }
    *out = make(QStringLiteral("image/gif"), static_cast<int>(width), static_cast<int>(height));
    return true;
}

bool probeBmp(const QByteArray &d, ImageInfo *out)
{
    if (!has(d, 0, 26) || !d.startsWith("BM")) {
        return false;
    }
    const qint32 width = static_cast<qint32>(le32(d, 18));
    const qint32 height = static_cast<qint32>(le32(d, 22));  // negative means top-down
    if (width <= 0 || height == 0) {
        return false;
    }
    *out = make(QStringLiteral("image/bmp"), static_cast<int>(width), static_cast<int>(qAbs(height)));
    return true;
}

bool probeJpeg(const QByteArray &d, ImageInfo *out)
{
    if (!has(d, 0, 4) || static_cast<quint8>(d.at(0)) != 0xff || static_cast<quint8>(d.at(1)) != 0xd8) {
        return false;
    }
    int offset = 2;
    while (offset + 9 < d.size()) {
        if (static_cast<quint8>(d.at(offset)) != 0xff) {
            ++offset;  // stray padding byte between markers
            continue;
        }
        const quint8 marker = static_cast<quint8>(d.at(offset + 1));
        // SOF0..SOF15 minus the DLP/DHT/JPEG-char variants carry the frame size.
        if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
            const quint32 height = be16(d, offset + 5);
            const quint32 width = be16(d, offset + 7);
            if (width == 0 || height == 0) {
                return false;
            }
            *out = make(QStringLiteral("image/jpeg"), static_cast<int>(width), static_cast<int>(height));
            return true;
        }
        if (marker == 0xd8 || marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) {
            offset += 2;  // standalone markers carry no length field
            continue;
        }
        if (marker == 0xd9) {
            return false;  // walked past the end of the image without a frame
        }
        const int segmentLength = be16(d, offset + 2);
        if (segmentLength < 2) {
            return false;
        }
        offset += 2 + segmentLength;
    }
    return false;
}

bool probeWebp(const QByteArray &d, ImageInfo *out)
{
    if (!has(d, 0, 20) || !d.startsWith("RIFF") || !d.mid(8, 4).startsWith("WEBP")) {
        return false;
    }
    const QByteArray chunk = d.mid(12, 4);
    if (chunk == "VP8 ") {
        // Keyframe bitstream: 3-byte frame tag, then sync 0x9d 0x01 0x2a at payload
        // offset 3, then the 14-bit dimensions at payload offsets 6 and 8.
        if (!has(d, 0, 30) || !d.mid(23, 3).startsWith("\x9d\x01\x2a")) {
            return false;
        }
        const quint32 width = le16(d, 26) & 0x3fff;
        const quint32 height = le16(d, 28) & 0x3fff;
        if (width == 0 || height == 0) {
            return false;
        }
        *out = make(QStringLiteral("image/webp"), static_cast<int>(width), static_cast<int>(height));
        return true;
    }
    if (chunk == "VP8L") {
        if (!has(d, 0, 25) || static_cast<quint8>(d.at(20)) != 0x2f) {
            return false;
        }
        const quint32 bits = le32(d, 21);
        const quint32 width = (bits & 0x3fff) + 1;
        const quint32 height = ((bits >> 14) & 0x3fff) + 1;
        *out = make(QStringLiteral("image/webp"), static_cast<int>(width), static_cast<int>(height));
        return true;
    }
    if (chunk == "VP8X") {
        if (!has(d, 0, 30)) {
            return false;
        }
        // Canvas size is 24-bit minus-one, little-endian, at offset 24.
        const quint32 width = (static_cast<quint8>(d.at(24)) | (static_cast<quint8>(d.at(25)) << 8)
                               | (static_cast<quint8>(d.at(26)) << 16)) + 1;
        const quint32 height = (static_cast<quint8>(d.at(27)) | (static_cast<quint8>(d.at(28)) << 8)
                                | (static_cast<quint8>(d.at(29)) << 16)) + 1;
        *out = make(QStringLiteral("image/webp"), static_cast<int>(width), static_cast<int>(height));
        return true;
    }
    return false;
}

}  // namespace

ImageInfo probeImage(const QByteArray &bytes)
{
    ImageInfo info;
    if (bytes.size() < 10) {
        return info;
    }
    if (probePng(bytes, &info) || probeJpeg(bytes, &info) || probeWebp(bytes, &info) || probeGif(bytes, &info)
        || probeBmp(bytes, &info)) {
        return info;
    }
    return ImageInfo();
}

}  // namespace oic::store
