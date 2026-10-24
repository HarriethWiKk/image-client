// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/mcp/tools.h"

#include <QJsonObject>
#include <QStringList>

namespace oic::mcp {
namespace {

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
                        "900 s job deadline) and returns image-client://asset links.")},
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
                                   QStringLiteral("Reference images as data URLs or "
                                                  "image-client://asset links.")}});
    return QJsonObject{
        {QStringLiteral("name"), QStringLiteral("edit_image")},
        {QStringLiteral("description"),
         QStringLiteral("Edit images with a text instruction. References come from imported "
                        "assets or inline base64 data URLs; returns image-client://asset links.")},
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

}  // namespace oic::mcp
