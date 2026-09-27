// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
//
// The container-header parser is checked against Qt's own decoder on the same
// bytes, so a width/height swap or an off-by-one shows up as a disagreement
// instead of as a test that agrees with its own fixture.

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QList>
#include <QSize>
#include <QString>
#include <QTest>

#include "oic/store/imageprobe.h"

namespace {

QByteArray encode(const QImage &image, const char *format, int quality = -1)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, QByteArray(format));
    if (quality >= 0) {
        writer.setQuality(quality);
    }
    if (!writer.write(image)) {
        return QByteArray();
    }
    return bytes;
}

QSize decodedSize(const QByteArray &bytes)
{
    QBuffer buffer(const_cast<QByteArray *>(&bytes));
    buffer.open(QIODevice::ReadOnly);
    return QImageReader(&buffer).size();
}

QByteArray le32Bytes(quint32 value)
{
    QByteArray out;
    out.append(char(value & 0xff));
    out.append(char((value >> 8) & 0xff));
    out.append(char((value >> 16) & 0xff));
    out.append(char((value >> 24) & 0xff));
    return out;
}

// A real RIFF/WEBP container: "RIFF" + u32 size + "WEBP" + fourcc + u32 size + payload.
QByteArray webp(const QByteArray &fourcc, const QByteArray &payload)
{
    QByteArray out;
    out += "RIFF";
    out += le32Bytes(4 + 8 + payload.size());
    out += "WEBP";
    out += fourcc;
    out += le32Bytes(payload.size());
    out += payload;
    return out;
}

QImage canvas(int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(QColor(10, 200, 30, 255));
    return image;
}

}  // namespace

class TstImageProbe : public QObject
{
    Q_OBJECT

private slots:
    void qtCanEncodePngJpegBmp()
    {
        const QList<QByteArray> formats = QImageWriter::supportedImageFormats();
        QVERIFY(formats.contains("png"));
        QVERIFY(formats.contains("jpeg"));
        QVERIFY(formats.contains("bmp"));
    }

    // Non-square sizes: a swapped pair only fails when the two differ.
    void matchesQtDecoderForPng_data();
    void matchesQtDecoderForPng();

    void matchesQtDecoderForJpeg_data();
    void matchesQtDecoderForJpeg();

    void matchesQtDecoderForBmp();

    void syntheticGifAndWebp();

    void rejectsNonImagesAndTruncation_data();
    void rejectsNonImagesAndTruncation();
};

void TstImageProbe::matchesQtDecoderForPng_data()
{
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("height");
    QTest::newRow("portrait") << 37 << 19;
    QTest::newRow("wide") << 512 << 17;
    QTest::newRow("tall") << 17 << 1024;
}

void TstImageProbe::matchesQtDecoderForPng()
{
    QFETCH(int, width);
    QFETCH(int, height);
    const QByteArray bytes = encode(canvas(width, height), "PNG");
    QVERIFY(!bytes.isEmpty());

    const oic::store::ImageInfo info = oic::store::probeImage(bytes);
    QVERIFY(info.recognized);
    QCOMPARE(info.mime, QStringLiteral("image/png"));
    QCOMPARE(info.width, width);
    QCOMPARE(info.height, height);
    QCOMPARE(decodedSize(bytes), QSize(width, height));  // the oracle
}

void TstImageProbe::matchesQtDecoderForJpeg_data()
{
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("height");
    QTest::newRow("small") << 41 << 23;
    QTest::newRow("asymmetric") << 640 << 129;
}

void TstImageProbe::matchesQtDecoderForJpeg()
{
    QFETCH(int, width);
    QFETCH(int, height);
    const QByteArray bytes = encode(canvas(width, height), "JPEG", 90);
    QVERIFY(!bytes.isEmpty());

    const oic::store::ImageInfo info = oic::store::probeImage(bytes);
    QVERIFY(info.recognized);
    QCOMPARE(info.mime, QStringLiteral("image/jpeg"));
    QCOMPARE(info.width, width);
    QCOMPARE(info.height, height);
    QCOMPARE(decodedSize(bytes), QSize(width, height));
}

void TstImageProbe::matchesQtDecoderForBmp()
{
    const QByteArray bytes = encode(canvas(61, 39), "BMP");
    QVERIFY(!bytes.isEmpty());

    const oic::store::ImageInfo info = oic::store::probeImage(bytes);
    QVERIFY(info.recognized);
    QCOMPARE(info.width, 61);
    QCOMPARE(info.height, 39);
    QCOMPARE(decodedSize(bytes), QSize(61, 39));
}

void TstImageProbe::syntheticGifAndWebp()
{
    // Qt here ships no WebP handler, so those three layouts get hand-built bytes
    // and only the arithmetic is exercised; the PNG/JPEG/BMP cases above are the
    // ones with an independent oracle.
    QByteArray gif;
    gif += "GIF89a";
    gif += QByteArray::fromHex("28001e0000");  // 40 x 30 logical screen, no palette bits
    QVERIFY(oic::store::probeImage(gif).recognized);
    QCOMPARE(oic::store::probeImage(gif).mime, QStringLiteral("image/gif"));
    QCOMPARE(oic::store::probeImage(gif).width, 40);
    QCOMPARE(oic::store::probeImage(gif).height, 30);

    // VP8 (lossy): 3-byte frame tag, sync code, then 14-bit dimensions.
    QByteArray lossy = QByteArray(3, '\x00') + QByteArray::fromHex("9d012a");
    lossy += QByteArray::fromHex("4100c900");  // 0x0041 = 65, 0x00c9 = 201 (little-endian, 14 bits)
    const oic::store::ImageInfo lossyInfo = oic::store::probeImage(webp("VP8 ", lossy));
    QCOMPARE(lossyInfo.width, 65);
    QCOMPARE(lossyInfo.height, 201);

    // VP8L (lossless): 0x2f then 14-bit minus-one width, 14-bit minus-one height.
    const quint32 bits = quint32(1023 - 1) | (quint32(767 - 1) << 14);
    QByteArray lossless = QByteArray(1, '\x2f');
    lossless += le32Bytes(bits);
    const oic::store::ImageInfo losslessInfo = oic::store::probeImage(webp("VP8L", lossless));
    QCOMPARE(losslessInfo.width, 1023);
    QCOMPARE(losslessInfo.height, 767);

    // VP8X (extended): canvas size as 24-bit minus-one.
    QByteArray extended(10, '\x00');
    extended[4] = char(0x3f);
    extended[7] = char(0x1f);
    const oic::store::ImageInfo extendedInfo = oic::store::probeImage(webp("VP8X", extended));
    QCOMPARE(extendedInfo.width, 64);
    QCOMPARE(extendedInfo.height, 32);
}

void TstImageProbe::rejectsNonImagesAndTruncation_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("short") << QByteArray("PNG");
    // An HTML error page is the payload this guard exists for: it must not be
    // written out as a .png that is not a PNG.
    QTest::newRow("html")
        << QByteArray("<!DOCTYPE html><html><head><title>504 Gateway Time-out</title></head><body>x</body></html>");
    QTest::newRow("json") << QByteArray("{\"error\": {\"message\": \"boom\"}} padded to be long enough here");
    QTest::newRow("png truncated") << QByteArray("\x89PNG\r\n\x1a\n", 8) + QByteArray("IHDR\x00\x00", 9);
    QTest::newRow("jpeg no frame")
        << QByteArray::fromHex("ffd8ffe000104a46494600010100000100010000ffd9");
}

void TstImageProbe::rejectsNonImagesAndTruncation()
{
    QFETCH(const QByteArray, bytes);
    const oic::store::ImageInfo info = oic::store::probeImage(bytes);
    QVERIFY2(!info.recognized, "recognized bytes that are not a complete image header");
    QCOMPARE(info.width, 0);
    QCOMPARE(info.height, 0);
}

QTEST_GUILESS_MAIN(TstImageProbe)
#include "tst_imageprobe.moc"
