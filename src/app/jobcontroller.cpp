// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/app/jobcontroller.h"

#include <QBuffer>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QUrl>

#include "oic/app/backend.h"
#include "oic/core/limits.h"
#include "oic/jobs/jobmanager.h"
#include "oic/protocol/imageprotocol.h"
#include "oic/store/database.h"
#include "oic/store/imageprobe.h"

namespace oic::app {
namespace {

// Reads a whole file into memory, refusing anything over the per-reference cap so a stray
// 500 MB drop is rejected here rather than after a worker round-trip. Returns false with a
// user-facing reason on read failure or oversize.
bool readFileCapped(const QString &path, QByteArray *bytes, QString *error)
{
    QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        *error = QStringLiteral("文件不存在：%1").arg(info.fileName());
        return false;
    }
    if (info.size() > oic::limits::kMaxReferenceImageBytes) {
        *error = QStringLiteral("参考图超过 %1 字节上限").arg(oic::limits::kMaxReferenceImageBytes);
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取文件：%1").arg(info.fileName());
        return false;
    }
    *bytes = file.readAll();
    return true;
}

}  // namespace

JobController::JobController(Backend *backend, QObject *parent) : QObject(parent), m_backend(backend)
{
    if (m_backend != nullptr && m_backend->jobs() != nullptr) {
        connect(m_backend->jobs(), &oic::jobs::JobManager::started, this, &JobController::jobStarted);
        connect(m_backend->jobs(), &oic::jobs::JobManager::finished, this,
                [this](const QString &jobId, const oic::jobs::JobOutcome &outcome) {
                    QStringList relPaths;
                    for (const oic::jobs::AssetRef &asset : outcome.assets)
                        relPaths.append(asset.relPath);
                    emit jobFinished(jobId, outcome.status, outcome.error, relPaths);
                });
    }
}

bool JobController::resolveProfile(const QString &profileName, oic::store::Profile *out, QString *error)
{
    if (m_backend == nullptr || m_backend->database() == nullptr) {
        *error = QStringLiteral("后端未就绪");
        return false;
    }
    QString storeError;
    const QList<oic::store::Profile> profiles = m_backend->database()->profiles(&storeError);
    if (!storeError.isEmpty()) {  // a store failure must not be mis-reported as "no such profile"
        *error = storeError;
        return false;
    }
    for (const oic::store::Profile &candidate : profiles) {
        if (candidate.name == profileName) {
            *out = candidate;
            return true;
        }
    }
    *error = QStringLiteral("未找到 profile「%1」").arg(profileName);
    return false;
}

QString JobController::submitImage(const QString &profileName, const QString &model, const QString &prompt,
                                   const QString &size, int n, const QString &protocolHint,
                                   const QList<PendingReference> &references, bool editMode, QString *error)
{
    if (m_backend == nullptr || m_backend->jobs() == nullptr) {
        *error = QStringLiteral("后端未就绪");
        return {};
    }

    oic::store::Profile profile;
    if (!resolveProfile(profileName, &profile, error))
        return {};

    oic::jobs::JobSpec spec;
    spec.profileName = profileName;
    spec.baseUrl = profile.baseUrl;
    spec.credentialTarget = profile.credentialTarget;
    spec.protocolHint = protocolHint.isEmpty() ? profile.protocol : protocolHint;
    spec.timeoutSeconds = profile.timeoutSeconds > 0 ? profile.timeoutSeconds : oic::limits::kDefaultTimeoutSeconds;
    spec.image.model = model;
    spec.image.prompt = prompt;
    spec.image.size = size;
    spec.image.n = n;
    spec.image.editMode = editMode;
    if (editMode) {
        for (const PendingReference &ref : references) {
            oic::protocol::ReferenceImage image;
            image.name = ref.name;
            image.dataUrl = ref.dataUrl;
            image.contentType = ref.mime;
            spec.image.references.append(image);
        }
    }

    QString submitError;
    const QString jobId = m_backend->jobs()->submit(spec, &submitError);
    if (jobId.isEmpty() && error != nullptr)
        *error = submitError;
    return jobId;
}

QString JobController::generate(const QString &profileName, const QString &model, const QString &prompt,
                                const QString &size, int n, const QString &protocolHint, QString *error)
{
    return submitImage(profileName, model, prompt, size, n, protocolHint, {}, /*editMode=*/false, error);
}

QString JobController::generateJob(const QString &profileName, const QString &model, const QString &prompt,
                                   const QString &size, int n, const QString &protocolHint)
{
    QString error;
    const QString jobId = generate(profileName, model, prompt, size, n, protocolHint, &error);
    m_lastError = error;
    return jobId;
}

QString JobController::addReferencePath(const QString &urlOrPath)
{
    m_lastError.clear();
    if (m_references.size() >= oic::limits::kMaxEditReferenceImages) {
        m_lastError = QStringLiteral("最多 %1 张参考图").arg(oic::limits::kMaxEditReferenceImages);
        return {};
    }
    const QUrl url(urlOrPath);
    const QString path = url.isLocalFile() ? url.toLocalFile() : urlOrPath;

    QByteArray bytes;
    QString readError;
    if (!readFileCapped(path, &bytes, &readError)) {
        m_lastError = readError;
        return {};
    }
    const oic::store::ImageInfo info = oic::store::probeImage(bytes);
    if (!info.recognized) {
        m_lastError = QStringLiteral("文件不是可识别的图片格式：%1").arg(QFileInfo(path).fileName());
        return {};
    }

    PendingReference ref;
    ref.source = QUrl::fromLocalFile(path).toString();
    ref.name = QFileInfo(path).fileName();
    ref.mime = info.mime;
    ref.dataUrl = oic::protocol::dataUrlFromBytes(bytes, info.mime);
    m_references.append(ref);
    emit referencesChanged();
    return ref.source;
}

bool JobController::addReferenceFromClipboard()
{
    m_lastError.clear();
    const QClipboard *clipboard = QGuiApplication::clipboard();
    const QMimeData *mime = clipboard ? clipboard->mimeData() : nullptr;
    if (mime == nullptr) {
        m_lastError = QStringLiteral("无法读取剪贴板");
        return false;
    }

    // Copying an image FILE in Explorer puts file URLs on the clipboard, not a bitmap; a
    // screenshot or a copy-from-app puts a bitmap. Handle both, so "paste" does the expected
    // thing regardless of how the image got onto the clipboard.
    if (mime->hasUrls()) {
        int staged = 0;
        for (const QUrl &url : mime->urls()) {
            if (!url.isLocalFile())
                continue;
            if (!addReferencePath(url.toLocalFile()).isEmpty())
                ++staged;
        }
        if (staged > 0)
            return true;
        // URLs were present but none was a usable image; fall through to the bitmap check,
        // then report the per-file reason captured by addReferencePath.
        if (!m_lastError.isEmpty())
            return false;
    }

    const QImage image = clipboard->image();
    if (image.isNull()) {
        m_lastError = QStringLiteral("剪贴板里没有图片或图片文件");
        return false;
    }
    if (m_references.size() >= oic::limits::kMaxEditReferenceImages) {
        m_lastError = QStringLiteral("最多 %1 张参考图").arg(oic::limits::kMaxEditReferenceImages);
        return false;
    }

    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG") || png.isEmpty()) {
        m_lastError = QStringLiteral("剪贴板图片无法编码为 PNG");
        return false;
    }
    if (png.size() > oic::limits::kMaxReferenceImageBytes) {
        m_lastError = QStringLiteral("剪贴板图片超过 %1 字节上限").arg(oic::limits::kMaxReferenceImageBytes);
        return false;
    }

    PendingReference ref;
    ref.name = QStringLiteral("pasted-image.png");
    ref.mime = QStringLiteral("image/png");
    ref.dataUrl = oic::protocol::dataUrlFromBytes(png, ref.mime);
    ref.source = ref.dataUrl;  // a pasted bitmap has no file path; the UI previews the data URL
    m_references.append(ref);
    emit referencesChanged();
    return true;
}

QStringList JobController::referenceSources() const
{
    QStringList sources;
    for (const PendingReference &ref : m_references)
        sources.append(ref.source);
    return sources;
}

bool JobController::removeReference(const QString &sourceUrl)
{
    for (int i = 0; i < m_references.size(); ++i) {
        if (m_references.at(i).source == sourceUrl) {
            m_references.removeAt(i);
            emit referencesChanged();
            return true;
        }
    }
    return false;
}

void JobController::clearReferences()
{
    m_references.clear();
    m_lastError.clear();
    emit referencesChanged();
}

QString JobController::generateEdit(const QString &profileName, const QString &model, const QString &prompt,
                                    const QString &size, int n, const QString &protocolHint)
{
    m_lastError.clear();
    if (m_references.isEmpty()) {
        m_lastError = QStringLiteral("图生图模式下请先上传参考图");
        return {};
    }
    QString error;
    const QString jobId = submitImage(profileName, model, prompt, size, n, protocolHint, m_references,
                                      /*editMode=*/true, &error);
    m_lastError = error;
    if (!jobId.isEmpty()) {
        m_references.clear();  // handed off to the job; referencesChanged repaints the card empty
        emit referencesChanged();
    }
    return jobId;
}

bool JobController::cancel(const QString &jobId)
{
    return m_backend != nullptr && m_backend->jobs() != nullptr && m_backend->jobs()->cancel(jobId);
}

}  // namespace oic::app
