// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/mcp/tools.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>

#include "oic/core/limits.h"
#include "oic/protocol/imageprotocol.h"

namespace oic::mcp {
namespace {

// SPEC 13.2's starting quota; the MCP side has no settings UI, so it uses the documented
// default rather than inventing a second source of truth.
constexpr qint64 kDefaultAssetQuotaBytes = 2LL * 1024 * 1024 * 1024;

QJsonObject stringProperty(const QString &description, int maxLength = 0)
{
    QJsonObject property{{QStringLiteral("type"), QStringLiteral("string")},
                         {QStringLiteral("description"), description}};
    if (maxLength > 0) {
        property.insert(QStringLiteral("maxLength"), maxLength);
    }
    return property;
}

QJsonObject objectSchema(const QJsonObject &properties, const QJsonArray &required)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), properties},
                       {QStringLiteral("required"), required},
                       {QStringLiteral("additionalProperties"), false}};
}

QJsonObject textContent(const QString &text)
{
    QJsonObject content;
    content.insert(QStringLiteral("type"), QStringLiteral("text"));
    content.insert(QStringLiteral("text"), text);
    return content;
}

QJsonObject toolError(const QString &message)
{
    QJsonObject result;
    result.insert(QStringLiteral("content"), QJsonArray{textContent(message)});
    result.insert(QStringLiteral("isError"), true);
    return result;
}

QJsonObject resourceLink(const store::Asset &asset)
{
    QJsonObject link;
    link.insert(QStringLiteral("type"), QStringLiteral("resource_link"));
    link.insert(QStringLiteral("uri"), QStringLiteral("image-client://assets/") + asset.id);
    link.insert(QStringLiteral("name"), QFileInfo(asset.relPath).fileName());
    if (!asset.mime.isEmpty()) {
        link.insert(QStringLiteral("mimeType"), asset.mime);
    }
    return link;
}

// Options shared by generate_image and edit_image (SPEC 5.5). n is capped at 4 on the MCP
// side, stricter than the GUI's 8 (SPEC 8.2).
QJsonObject imageOptions()
{
    QJsonObject options;
    options.insert(QStringLiteral("model"),
                   stringProperty(QStringLiteral("Model id; defaults to the profile's image model."), 200));
    options.insert(QStringLiteral("size"),
                   stringProperty(QStringLiteral("Free-form WxH, e.g. 1024x1024."), 64));
    options.insert(QStringLiteral("n"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                               {QStringLiteral("minimum"), 1},
                               {QStringLiteral("maximum"), 4},
                               {QStringLiteral("default"), 1}});
    options.insert(QStringLiteral("profile"),
                   stringProperty(QStringLiteral("Profile name; defaults to the first configured profile.")));
    options.insert(QStringLiteral("quality"),
                   stringProperty(QStringLiteral("Upstream quality option, passed through when non-empty.")));
    options.insert(QStringLiteral("background"),
                   stringProperty(QStringLiteral("Upstream background option, passed through when non-empty.")));
    options.insert(QStringLiteral("moderation"),
                   stringProperty(QStringLiteral("Upstream moderation option, passed through when non-empty.")));
    options.insert(QStringLiteral("output_format"),
                   stringProperty(QStringLiteral("Upstream output format, passed through when non-empty.")));
    options.insert(QStringLiteral("output_compression"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                               {QStringLiteral("minimum"), 0},
                               {QStringLiteral("maximum"), 100}});
    options.insert(QStringLiteral("inline_images"),
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                               {QStringLiteral("default"), false},
                               {QStringLiteral("description"),
                                QStringLiteral("Return image bytes inline instead of "
                                               "image-client:// asset links.")}});
    return options;
}

QJsonObject generateImageTool()
{
    QJsonObject properties = imageOptions();
    properties.insert(QStringLiteral("prompt"),
                      stringProperty(QStringLiteral("Image description."), 16000));
    return QJsonObject{
        {QStringLiteral("name"), QStringLiteral("generate_image")},
        {QStringLiteral("description"),
         QStringLiteral("Generate images from a text prompt. Waits for the job (bounded by the "
                        "900 s job deadline) and returns image-client:// asset links.")},
        {QStringLiteral("inputSchema"), objectSchema(properties, QJsonArray{QStringLiteral("prompt")})}};
}

QJsonObject editImageTool()
{
    QJsonObject properties = imageOptions();
    properties.insert(QStringLiteral("prompt"),
                      stringProperty(QStringLiteral("Edit instruction."), 16000));
    properties.insert(QStringLiteral("edit_images"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                  {QStringLiteral("items"),
                                   QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                                  {QStringLiteral("minItems"), 1},
                                  {QStringLiteral("maxItems"), 16},
                                  {QStringLiteral("description"),
                                   QStringLiteral("Reference images as image/* base64 data URLs.")}});
    return QJsonObject{
        {QStringLiteral("name"), QStringLiteral("edit_image")},
        {QStringLiteral("description"),
         QStringLiteral("Edit images with a text instruction. References are base64 data URLs; "
                        "returns image-client:// asset links.")},
        {QStringLiteral("inputSchema"),
         objectSchema(properties, QJsonArray{QStringLiteral("prompt"), QStringLiteral("edit_images")})}};
}

QJsonObject importReferenceTool()
{
    QJsonObject properties;
    properties.insert(QStringLiteral("path"),
                      stringProperty(QStringLiteral("Local image path under the configured input roots.")));
    return QJsonObject{
        {QStringLiteral("name"), QStringLiteral("import_reference_image")},
        {QStringLiteral("description"),
         QStringLiteral("Import a local image as a reference (stdio only). The path must resolve "
                        "inside the configured input roots; symlink escapes are rejected.")},
        {QStringLiteral("inputSchema"),
         objectSchema(properties, QJsonArray{QStringLiteral("path")})}};
}

QJsonObject getJobTool()
{
    QJsonObject properties;
    properties.insert(QStringLiteral("job_id"), stringProperty(QStringLiteral("Job id.")));
    return QJsonObject{
        {QStringLiteral("name"), QStringLiteral("get_job")},
        {QStringLiteral("description"), QStringLiteral("Read a job's status and assets.")},
        {QStringLiteral("inputSchema"),
         objectSchema(properties, QJsonArray{QStringLiteral("job_id")})}};
}

QJsonObject cancelJobTool()
{
    QJsonObject properties;
    properties.insert(QStringLiteral("job_id"), stringProperty(QStringLiteral("Job id.")));
    return QJsonObject{
        {QStringLiteral("name"), QStringLiteral("cancel_job")},
        {QStringLiteral("description"),
         QStringLiteral("Cancel a job. An upstream request already in flight finishes first.")},
        {QStringLiteral("inputSchema"),
         objectSchema(properties, QJsonArray{QStringLiteral("job_id")})}};
}

QString stringArg(const QJsonObject &args, const QString &key)
{
    return args.value(key).toString().trimmed();
}

QJsonObject jsonResource(const QString &uri, const QJsonDocument &document)
{
    QJsonObject content;
    content.insert(QStringLiteral("uri"), uri);
    content.insert(QStringLiteral("mimeType"), QStringLiteral("application/json"));
    content.insert(QStringLiteral("text"),
                   QString::fromUtf8(document.toJson(QJsonDocument::Compact)));

    QJsonObject result;
    result.insert(QStringLiteral("contents"), QJsonArray{content});
    return result;
}

QJsonObject jobSummary(const store::Job &job, bool includePrompt)
{
    QJsonObject summary;
    summary.insert(QStringLiteral("id"), job.id);
    summary.insert(QStringLiteral("status"), job.status);
    summary.insert(QStringLiteral("mode"), job.mode);
    summary.insert(QStringLiteral("profile"), job.profile);
    summary.insert(QStringLiteral("model"), job.model);
    summary.insert(QStringLiteral("n"), job.n);
    summary.insert(QStringLiteral("createdAt"), static_cast<double>(job.createdAt));
    summary.insert(QStringLiteral("durationMs"), static_cast<double>(job.durationMs));
    if (!job.error.isEmpty()) {
        summary.insert(QStringLiteral("error"), job.error);
    }
    if (includePrompt) {
        summary.insert(QStringLiteral("prompt"), job.prompt);
        summary.insert(QStringLiteral("size"), job.size);
    }
    return summary;
}

QJsonObject assetSummary(const store::Asset &asset)
{
    QJsonObject summary;
    summary.insert(QStringLiteral("id"), asset.id);
    summary.insert(QStringLiteral("jobId"), asset.jobId);
    summary.insert(QStringLiteral("mime"), asset.mime);
    summary.insert(QStringLiteral("bytes"), static_cast<double>(asset.bytes));
    summary.insert(QStringLiteral("width"), asset.width);
    summary.insert(QStringLiteral("height"), asset.height);
    summary.insert(QStringLiteral("createdAt"), static_cast<double>(asset.createdAt));
    summary.insert(QStringLiteral("uri"), QStringLiteral("image-client://assets/") + asset.id);
    return summary;
}

}  // namespace

QJsonArray toolDefinitions()
{
    return QJsonArray{generateImageTool(), editImageTool(), importReferenceTool(), getJobTool(),
                      cancelJobTool()};
}

bool isKnownTool(const QString &name)
{
    static const QStringList kTools{
        QStringLiteral("generate_image"),  QStringLiteral("edit_image"),
        QStringLiteral("import_reference_image"), QStringLiteral("get_job"),
        QStringLiteral("cancel_job")};
    return kTools.contains(name);
}

ToolHost makeDefaultToolHost(const store::Paths &paths)
{
    ToolHost host;

    host.profiles = [dbPath = paths.database](QString *error) {
        store::Database database(dbPath);
        if (!database.open(error)) {
            return QList<store::Profile>{};
        }
        return database.profiles(error);
    };

    host.runJob = [dbPath = paths.database, assetsRoot = paths.assets](
                      const jobs::JobSpec &spec, jobs::CancelToken *cancel, QString *error) {
        const jobs::JobDeps deps =
            jobs::makeDefaultDeps(dbPath, assetsRoot, kDefaultAssetQuotaBytes);
        return jobs::runJob(spec, deps, cancel, error);
    };

    host.job = [dbPath = paths.database](const QString &id, bool *found, QString *error) {
        store::Database database(dbPath);
        if (!database.open(error)) {
            if (found != nullptr) {
                *found = false;
            }
            return store::Job{};
        }
        return database.job(id, found, error);
    };

    host.assets = [dbPath = paths.database](const QString &jobId, QString *error) {
        store::Database database(dbPath);
        if (!database.open(error)) {
            return QList<store::Asset>{};
        }
        return database.assetsForJob(jobId, error);
    };

    host.jobs = [dbPath = paths.database](int limit, QString *error) {
        store::Database database(dbPath);
        if (!database.open(error)) {
            return QList<store::Job>{};
        }
        return database.listJobs(limit, 0, false, error);
    };

    host.asset = [dbPath = paths.database](const QString &id, bool *found, QString *error) {
        store::Database database(dbPath);
        if (!database.open(error)) {
            if (found != nullptr) {
                *found = false;
            }
            return store::Asset{};
        }
        return database.asset(id, found, error);
    };

    host.allAssets = [dbPath = paths.database](QString *error) {
        store::Database database(dbPath);
        if (!database.open(error)) {
            return QList<store::Asset>{};
        }
        return database.allAssets(error);
    };

    host.readAsset = [assetsRoot = paths.assets](const QString &relPath, QString *error) {
        const QString rootClean = QDir::cleanPath(assetsRoot);
        const QString absolute = QDir::cleanPath(QDir(assetsRoot).filePath(relPath));
        if (absolute != rootClean && !absolute.startsWith(rootClean + QLatin1Char('/'))) {
            if (error != nullptr) {
                *error = QStringLiteral("非法资源路径");
            }
            return QByteArray{};
        }
        QFile file(absolute);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error != nullptr) {
                *error = QStringLiteral("无法读取资源：%1").arg(QFileInfo(absolute).fileName());
            }
            return QByteArray{};
        }
        return file.readAll();
    };

    return host;
}

jobs::JobSpec buildJobSpec(const QJsonObject &args, const store::Profile &profile, bool editMode,
                           QString *error)
{
    jobs::JobSpec spec;
    if (error != nullptr) {
        error->clear();
    }
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return jobs::JobSpec{};
    };

    const QString prompt = args.value(QStringLiteral("prompt")).toString();
    if (prompt.trimmed().isEmpty()) {
        return fail(QStringLiteral("prompt is required"));
    }
    if (prompt.size() > limits::kMaxPromptChars) {
        return fail(QStringLiteral("prompt exceeds %1 characters").arg(limits::kMaxPromptChars));
    }

    const QJsonValue nValue = args.value(QStringLiteral("n"));
    const int n = nValue.isUndefined() ? 1 : nValue.toInt(1);
    if (n < 1 || n > 4) {
        return fail(QStringLiteral("n must be between 1 and 4"));
    }

    const QString modelArg = stringArg(args, QStringLiteral("model"));
    const QString model = modelArg.isEmpty()
                              ? (profile.imageModel.trimmed().isEmpty() ? QStringLiteral("gpt-image-2")
                                                                        : profile.imageModel.trimmed())
                              : modelArg;
    if (model.size() > limits::kMaxModelChars) {
        return fail(QStringLiteral("model exceeds %1 characters").arg(limits::kMaxModelChars));
    }

    const QString sizeArg = stringArg(args, QStringLiteral("size"));
    if (sizeArg.size() > 64) {
        return fail(QStringLiteral("size exceeds 64 characters"));
    }

    spec.profileName = profile.name;
    spec.baseUrl = profile.baseUrl;
    spec.protocolHint = profile.protocol;
    spec.credentialTarget = profile.credentialTarget;
    spec.timeoutSeconds =
        profile.timeoutSeconds > 0 ? profile.timeoutSeconds : limits::kDefaultTimeoutSeconds;

    spec.image.model = model;
    spec.image.prompt = prompt;
    spec.image.size = sizeArg.isEmpty() ? QStringLiteral("1024x1024") : sizeArg;
    spec.image.n = n;
    spec.image.editMode = editMode;
    spec.image.quality = stringArg(args, QStringLiteral("quality"));
    spec.image.background = stringArg(args, QStringLiteral("background"));
    spec.image.moderation = stringArg(args, QStringLiteral("moderation"));
    spec.image.outputFormat = stringArg(args, QStringLiteral("output_format"));

    const QJsonValue compression = args.value(QStringLiteral("output_compression"));
    if (!compression.isUndefined()) {
        if (!compression.isDouble() || compression.toInt(-1) < 0 || compression.toInt(-1) > 100) {
            return fail(QStringLiteral("output_compression must be an integer between 0 and 100"));
        }
        spec.image.outputCompression = compression.toInt();
    }

    if (editMode) {
        const QJsonValue refsValue = args.value(QStringLiteral("edit_images"));
        if (!refsValue.isArray() || refsValue.toArray().isEmpty()) {
            return fail(QStringLiteral("edit_images must be a non-empty array of data URLs"));
        }
        const QJsonArray refs = refsValue.toArray();
        if (refs.size() > limits::kMaxEditReferenceImages) {
            return fail(QStringLiteral("edit_images exceeds %1 images")
                            .arg(limits::kMaxEditReferenceImages));
        }
        for (const QJsonValue &value : refs) {
            if (!value.isString()) {
                return fail(QStringLiteral("edit_images entries must be strings"));
            }
            const QString dataUrl = value.toString();
            if (dataUrl.startsWith(QLatin1String("image-client://"))) {
                return fail(QStringLiteral("asset references are not available yet; pass an "
                                           "image/* base64 data URL"));
            }
            QString mime;
            QString encoded;
            QString dataError;
            if (!protocol::parseDataUrl(dataUrl, &mime, &encoded, &dataError)) {
                return fail(dataError);
            }
            protocol::ReferenceImage reference;
            reference.dataUrl = dataUrl;
            reference.contentType = mime;
            spec.image.references.append(reference);
        }
    }

    return spec;
}

QJsonObject outcomeToResult(const jobs::JobOutcome &outcome, bool inlineImages,
                            const std::function<QByteArray(const QString &, QString *)> &readAsset)
{
    QJsonArray content;
    const bool succeeded = outcome.status == QLatin1String("succeeded");

    QString summary;
    if (succeeded) {
        summary = QStringLiteral("job %1 succeeded in %2 ms; %3 image(s)")
                      .arg(outcome.jobId)
                      .arg(outcome.durationMs)
                      .arg(outcome.assets.size());
    } else if (outcome.status == QLatin1String("cancelled")) {
        summary = QStringLiteral("job %1 cancelled").arg(outcome.jobId);
    } else {
        summary = outcome.error.isEmpty() ? QStringLiteral("job %1 failed").arg(outcome.jobId)
                                          : outcome.error;
    }
    content.append(textContent(summary));

    for (const jobs::AssetRef &asset : outcome.assets) {
        if (inlineImages && readAsset) {
            QString readError;
            const QByteArray bytes = readAsset(asset.relPath, &readError);
            if (!bytes.isEmpty()) {
                QJsonObject image;
                image.insert(QStringLiteral("type"), QStringLiteral("image"));
                image.insert(QStringLiteral("data"), QString::fromLatin1(bytes.toBase64()));
                image.insert(QStringLiteral("mimeType"),
                             asset.mime.isEmpty() ? QStringLiteral("image/png") : asset.mime);
                content.append(image);
                continue;
            }
        }
        QJsonObject link;
        link.insert(QStringLiteral("type"), QStringLiteral("resource_link"));
        link.insert(QStringLiteral("uri"), QStringLiteral("image-client://assets/") + asset.id);
        link.insert(QStringLiteral("name"), QFileInfo(asset.relPath).fileName());
        if (!asset.mime.isEmpty()) {
            link.insert(QStringLiteral("mimeType"), asset.mime);
        }
        content.append(link);
    }

    QJsonObject result;
    result.insert(QStringLiteral("content"), content);
    result.insert(QStringLiteral("isError"), !succeeded);
    return result;
}

ToolReply executeTool(const QString &name, const QJsonObject &args, const QString &jobId,
                      jobs::CancelToken *cancel, const ToolHost &host)
{
    ToolReply reply;

    if (name == QLatin1String("get_job")) {
        const QString id = args.value(QStringLiteral("job_id")).toString().trimmed();
        if (id.isEmpty()) {
            reply.protocolError = true;
            reply.error = QStringLiteral("job_id is required");
            return reply;
        }
        if (!host.job || !host.assets) {
            reply.result = toolError(QStringLiteral("tool host is not configured"));
            return reply;
        }
        QString error;
        bool found = false;
        const store::Job job = host.job(id, &found, &error);
        if (!error.isEmpty()) {
            reply.result = toolError(error);
            return reply;
        }
        if (!found) {
            reply.result = toolError(QStringLiteral("job not found: %1").arg(id));
            return reply;
        }
        const QList<store::Asset> assets = host.assets(id, &error);
        if (!error.isEmpty()) {
            reply.result = toolError(error);
            return reply;
        }

        QJsonArray content;
        content.append(textContent(QStringLiteral("job %1: %2").arg(id, job.status)));
        if (!job.error.isEmpty()) {
            content.append(textContent(job.error));
        }
        for (const store::Asset &asset : assets) {
            content.append(resourceLink(asset));
        }
        QJsonObject result;
        result.insert(QStringLiteral("content"), content);
        result.insert(QStringLiteral("isError"), false);
        reply.result = result;
        return reply;
    }

    if (name == QLatin1String("cancel_job")) {
        reply.protocolError = true;
        reply.error = QStringLiteral("cancel_job is served by the transport layer");
        return reply;
    }

    if (name == QLatin1String("import_reference_image")) {
        reply.result = toolError(QStringLiteral(
            "import_reference_image is not implemented yet; pass base64 data URLs to edit_image"));
        return reply;
    }

    if (name != QLatin1String("generate_image") && name != QLatin1String("edit_image")) {
        reply.protocolError = true;
        reply.error = QStringLiteral("tool not implemented: %1").arg(name);
        return reply;
    }

    if (!host.profiles || !host.runJob) {
        reply.result = toolError(QStringLiteral("tool host is not configured"));
        return reply;
    }

    QString error;
    const QList<store::Profile> profiles = host.profiles(&error);
    if (!error.isEmpty()) {
        reply.result = toolError(error);
        return reply;
    }
    if (profiles.isEmpty()) {
        reply.result = toolError(QStringLiteral("no provider profile is configured"));
        return reply;
    }

    const QString wanted = stringArg(args, QStringLiteral("profile"));
    store::Profile profile;
    bool found = false;
    for (const store::Profile &candidate : profiles) {
        if (wanted.isEmpty() || candidate.name == wanted) {
            profile = candidate;
            found = true;
            break;
        }
    }
    if (!found) {
        reply.protocolError = true;
        reply.error = QStringLiteral("unknown profile: %1").arg(wanted);
        return reply;
    }

    const bool editMode = name == QLatin1String("edit_image");
    jobs::JobSpec spec = buildJobSpec(args, profile, editMode, &error);
    if (!error.isEmpty()) {
        reply.protocolError = true;
        reply.error = error;
        return reply;
    }
    spec.jobId = jobId;

    QString runError;
    const jobs::JobOutcome outcome = host.runJob(spec, cancel, &runError);
    if (outcome.status.isEmpty()) {
        reply.result =
            toolError(runError.isEmpty() ? QStringLiteral("job failed to start") : runError);
        return reply;
    }

    reply.result = outcomeToResult(outcome, args.value(QStringLiteral("inline_images")).toBool(),
                                   host.readAsset);
    return reply;
}

QJsonArray resourceDefinitions()
{
    const auto make = [](const QString &uri, const QString &name, const QString &description) {
        QJsonObject resource;
        resource.insert(QStringLiteral("uri"), uri);
        resource.insert(QStringLiteral("name"), name);
        resource.insert(QStringLiteral("mimeType"), QStringLiteral("application/json"));
        resource.insert(QStringLiteral("description"), description);
        return resource;
    };

    return QJsonArray{
        make(QStringLiteral("image-client://capabilities"), QStringLiteral("Capabilities"),
             QStringLiteral("Server name, version, protocol and tool surface.")),
        make(QStringLiteral("image-client://profiles"), QStringLiteral("Profiles"),
             QStringLiteral("Configured provider profiles.")),
        make(QStringLiteral("image-client://jobs"), QStringLiteral("Jobs"),
             QStringLiteral("Most recent jobs.")),
        make(QStringLiteral("image-client://assets"), QStringLiteral("Assets"),
             QStringLiteral("Stored assets."))};
}

QJsonObject readResource(const QString &uri, const ToolHost &host, bool *found, QString *error)
{
    if (found != nullptr) {
        *found = false;
    }
    if (error != nullptr) {
        error->clear();
    }

    const QUrl url(uri);
    if (url.scheme() != QLatin1String("image-client")) {
        return {};
    }

    const QString node = url.host();
    const QStringList parts = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const auto markFound = [found]() {
        if (found != nullptr) {
            *found = true;
        }
    };

    if (node == QLatin1String("capabilities")) {
        markFound();
        QJsonObject payload;
        payload.insert(QStringLiteral("name"), QString::fromLatin1(kServerName));
        payload.insert(QStringLiteral("version"), QString::fromLatin1(kServerVersion));
        payload.insert(QStringLiteral("protocolVersion"), QString::fromLatin1(kProtocolLatest));

        QJsonArray tools;
        for (const QJsonValue &value : toolDefinitions()) {
            tools.append(value.toObject().value(QStringLiteral("name")));
        }
        payload.insert(QStringLiteral("tools"), tools);
        payload.insert(QStringLiteral("resources"),
                       QJsonArray{QStringLiteral("image-client://capabilities"),
                                  QStringLiteral("image-client://profiles"),
                                  QStringLiteral("image-client://jobs"),
                                  QStringLiteral("image-client://assets")});
        return jsonResource(uri, QJsonDocument(payload));
    }

    if (node == QLatin1String("profiles")) {
        markFound();
        if (!host.profiles) {
            if (error != nullptr) {
                *error = QStringLiteral("tool host is not configured");
            }
            return {};
        }
        QString readError;
        const QList<store::Profile> profiles = host.profiles(&readError);
        if (!readError.isEmpty()) {
            if (error != nullptr) {
                *error = readError;
            }
            return {};
        }
        QJsonArray items;
        for (const store::Profile &profile : profiles) {
            QJsonObject item;
            item.insert(QStringLiteral("name"), profile.name);
            item.insert(QStringLiteral("protocol"), profile.protocol);
            item.insert(QStringLiteral("baseUrl"), profile.baseUrl);
            item.insert(QStringLiteral("imageModel"), profile.imageModel);
            item.insert(QStringLiteral("timeoutSeconds"), profile.timeoutSeconds);
            items.append(item);
        }
        return jsonResource(uri, QJsonDocument(items));
    }

    if (node == QLatin1String("jobs")) {
        markFound();
        if (parts.isEmpty()) {
            if (!host.jobs) {
                if (error != nullptr) {
                    *error = QStringLiteral("tool host is not configured");
                }
                return {};
            }
            QString readError;
            const QList<store::Job> jobs = host.jobs(50, &readError);
            if (!readError.isEmpty()) {
                if (error != nullptr) {
                    *error = readError;
                }
                return {};
            }
            QJsonArray items;
            for (const store::Job &job : jobs) {
                items.append(jobSummary(job, true));
            }
            return jsonResource(uri, QJsonDocument(items));
        }
        if (parts.size() == 1) {
            const QString id = parts.first();
            if (!host.job) {
                if (error != nullptr) {
                    *error = QStringLiteral("tool host is not configured");
                }
                return {};
            }
            QString readError;
            bool jobFound = false;
            const store::Job job = host.job(id, &jobFound, &readError);
            if (!readError.isEmpty()) {
                if (error != nullptr) {
                    *error = readError;
                }
                return {};
            }
            if (!jobFound) {
                if (found != nullptr) {
                    *found = false;
                }
                return {};
            }
            QJsonObject payload = jobSummary(job, true);
            if (host.assets) {
                const QList<store::Asset> assets = host.assets(id, &readError);
                if (!readError.isEmpty()) {
                    if (error != nullptr) {
                        *error = readError;
                    }
                    return {};
                }
                QJsonArray items;
                for (const store::Asset &asset : assets) {
                    items.append(assetSummary(asset));
                }
                payload.insert(QStringLiteral("assets"), items);
            }
            return jsonResource(uri, QJsonDocument(payload));
        }
        if (found != nullptr) {
            *found = false;
        }
        return {};
    }

    if (node == QLatin1String("assets")) {
        markFound();
        if (parts.isEmpty()) {
            if (!host.allAssets) {
                if (error != nullptr) {
                    *error = QStringLiteral("tool host is not configured");
                }
                return {};
            }
            QString readError;
            const QList<store::Asset> assets = host.allAssets(&readError);
            if (!readError.isEmpty()) {
                if (error != nullptr) {
                    *error = readError;
                }
                return {};
            }
            QJsonArray items;
            for (const store::Asset &asset : assets) {
                items.append(assetSummary(asset));
            }
            return jsonResource(uri, QJsonDocument(items));
        }
        if (parts.size() == 1) {
            const QString id = parts.first();
            if (!host.asset || !host.readAsset) {
                if (error != nullptr) {
                    *error = QStringLiteral("tool host is not configured");
                }
                return {};
            }
            QString readError;
            bool assetFound = false;
            const store::Asset asset = host.asset(id, &assetFound, &readError);
            if (!readError.isEmpty()) {
                if (error != nullptr) {
                    *error = readError;
                }
                return {};
            }
            if (!assetFound) {
                if (found != nullptr) {
                    *found = false;
                }
                return {};
            }
            const QByteArray bytes = host.readAsset(asset.relPath, &readError);
            if (!readError.isEmpty() || bytes.isEmpty()) {
                if (error != nullptr) {
                    *error = readError.isEmpty() ? QStringLiteral("asset is empty") : readError;
                }
                return {};
            }
            QJsonObject content;
            content.insert(QStringLiteral("uri"), uri);
            content.insert(QStringLiteral("mimeType"),
                           asset.mime.isEmpty() ? QStringLiteral("application/octet-stream")
                                                : asset.mime);
            content.insert(QStringLiteral("blob"), QString::fromLatin1(bytes.toBase64()));

            QJsonObject result;
            result.insert(QStringLiteral("contents"), QJsonArray{content});
            return result;
        }
        if (found != nullptr) {
            *found = false;
        }
        return {};
    }

    return {};
}

}  // namespace oic::mcp
