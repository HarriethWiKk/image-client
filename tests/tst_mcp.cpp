// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
//
// MCP stdio surface: line-delimited JSON-RPC 2.0 framing, initialize / ping / tools/list,
// the tool argument mapping, and the error paths a client hits first. Tool execution is
// driven through a fake ToolHost, so no database, network, or Credential Manager is needed.

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMutex>
#include <QMutexLocker>
#include <QTest>
#include <QThread>
#include <QVector>

#include <memory>

#include "oic/mcp/server.h"
#include "oic/mcp/tools.h"

namespace {

QJsonObject call(oic::mcp::Server &server, const QByteArray &line)
{
    const QByteArray response = server.handleLine(line);
    return QJsonDocument::fromJson(response).object();
}

QJsonObject responseFor(const QByteArray &line)
{
    oic::mcp::Server server;
    return call(server, line);
}

QJsonObject toolByName(const QJsonArray &tools, const QString &name)
{
    for (const QJsonValue &value : tools) {
        const QJsonObject tool = value.toObject();
        if (tool.value(QStringLiteral("name")).toString() == name) {
            return tool;
        }
    }
    return {};
}

oic::store::Profile sampleProfile()
{
    oic::store::Profile profile;
    profile.name = QStringLiteral("p");
    profile.baseUrl = QStringLiteral("https://api.example.com");
    profile.protocol = QStringLiteral("openai");
    profile.imageModel = QStringLiteral("gpt-image-2");
    profile.timeoutSeconds = 300;
    profile.credentialTarget = QStringLiteral("image-client/profile/p");
    return profile;
}

// A host whose job run is captured, so the arguments -> JobSpec mapping can be asserted
// without running the executor.
oic::mcp::ToolHost fakeHost(const std::shared_ptr<oic::jobs::JobSpec> &captured,
                            const QString &status = QStringLiteral("succeeded"))
{
    oic::mcp::ToolHost host;
    const oic::store::Profile profile = sampleProfile();
    host.profiles = [profile](QString *) { return QList<oic::store::Profile>{profile}; };
    host.runJob = [captured, status](const oic::jobs::JobSpec &spec, oic::jobs::CancelToken *,
                                     QString *) {
        *captured = spec;
        oic::jobs::JobOutcome outcome;
        outcome.jobId = spec.jobId;
        outcome.status = status;
        outcome.durationMs = 12;
        if (status == QLatin1String("succeeded")) {
            oic::jobs::AssetRef asset;
            asset.id = QStringLiteral("asset-1");
            asset.relPath = QStringLiteral("j1/result.png");
            asset.mime = QStringLiteral("image/png");
            outcome.assets.append(asset);
        } else {
            outcome.error = QStringLiteral("upstream exploded");
        }
        return outcome;
    };
    host.readAsset = [](const QString &, QString *) { return QByteArray("PNGDATA"); };
    return host;
}

}  // namespace

class TstMcp : public QObject
{
    Q_OBJECT

private slots:
    void initializeEchoesSupportedVersion();
    void initializeFallsBackForUnknownVersion();
    void pingReturnsAnEmptyResult();
    void toolsListAdvertisesTheFrozenSurface();
    void generateSchemaCapsNAtFour();
    void unknownMethodIsMethodNotFound();
    void malformedJsonIsParseError();
    void notificationGetsNoResponse();
    void stringIdIsEchoed();
    void unknownToolIsInvalidParams();
    void nonObjectIsInvalidRequest();
    void responsesAreSingleLines();

    void specDefaultsComeFromTheProfile();
    void specRejectsOutOfRangeN();
    void specRequiresReferencesForEdit();
    void specAcceptsDataUrlReferences();
    void outcomeReturnsResourceLinks();
    void outcomeReturnsInlineImagesOnRequest();
    void generateImageRunsTheJob();
    void failedJobBecomesAToolError();
    void getJobReturnsStatusAndAssets();
    void importReferenceIsReportedAsUnavailable();
    void cancellationStopsARunningJob();
};

void TstMcp::initializeEchoesSupportedVersion()
{
    const QJsonObject response = responseFor(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})");

    QCOMPARE(response.value(QStringLiteral("jsonrpc")).toString(), QStringLiteral("2.0"));
    QCOMPARE(response.value(QStringLiteral("id")).toInt(), 1);

    const QJsonObject result = response.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("protocolVersion")).toString(), QStringLiteral("2025-06-18"));
    QCOMPARE(result.value(QStringLiteral("serverInfo")).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("image-client-mcp"));
    QVERIFY(result.value(QStringLiteral("capabilities")).toObject().value(QStringLiteral("tools")).isObject());
}

void TstMcp::initializeFallsBackForUnknownVersion()
{
    const QJsonObject response = responseFor(
        R"({"jsonrpc":"2.0","id":2,"method":"initialize","params":{"protocolVersion":"1999-01-01"}})");
    QCOMPARE(response.value(QStringLiteral("result"))
                 .toObject()
                 .value(QStringLiteral("protocolVersion"))
                 .toString(),
             QStringLiteral("2026-07-28"));
}

void TstMcp::pingReturnsAnEmptyResult()
{
    const QJsonObject response = responseFor(R"({"jsonrpc":"2.0","id":"p","method":"ping"})");
    QVERIFY(response.value(QStringLiteral("result")).isObject());
    QVERIFY(response.value(QStringLiteral("result")).toObject().isEmpty());
    QVERIFY(response.value(QStringLiteral("error")).isUndefined());
}

void TstMcp::toolsListAdvertisesTheFrozenSurface()
{
    const QJsonObject response = responseFor(R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})");
    const QJsonArray tools = response.value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray();

    QStringList names;
    for (const QJsonValue &value : tools) {
        names.append(value.toObject().value(QStringLiteral("name")).toString());
    }
    names.sort();

    QStringList expected{QStringLiteral("generate_image"), QStringLiteral("edit_image"),
                         QStringLiteral("import_reference_image"), QStringLiteral("get_job"),
                         QStringLiteral("cancel_job")};
    expected.sort();
    QCOMPARE(names, expected);
}

void TstMcp::generateSchemaCapsNAtFour()
{
    const QJsonObject response = responseFor(R"({"jsonrpc":"2.0","id":4,"method":"tools/list"})");
    const QJsonArray tools = response.value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray();
    const QJsonObject schema = toolByName(tools, QStringLiteral("generate_image"))
                                   .value(QStringLiteral("inputSchema"))
                                   .toObject();

    QCOMPARE(schema.value(QStringLiteral("type")).toString(), QStringLiteral("object"));
    QCOMPARE(schema.value(QStringLiteral("additionalProperties")).toBool(), false);

    const QJsonObject n = schema.value(QStringLiteral("properties")).toObject().value(QStringLiteral("n")).toObject();
    QCOMPARE(n.value(QStringLiteral("minimum")).toInt(), 1);
    QCOMPARE(n.value(QStringLiteral("maximum")).toInt(), 4);

    QVERIFY(schema.value(QStringLiteral("required")).toArray().contains(QJsonValue(QStringLiteral("prompt"))));
}

void TstMcp::unknownMethodIsMethodNotFound()
{
    const QJsonObject response = responseFor(R"({"jsonrpc":"2.0","id":5,"method":"no/such/method"})");
    QCOMPARE(response.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32601);
    QCOMPARE(response.value(QStringLiteral("id")).toInt(), 5);
}

void TstMcp::malformedJsonIsParseError()
{
    const QJsonObject response = responseFor(R"({"jsonrpc":)");
    QCOMPARE(response.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32700);
    QVERIFY(response.value(QStringLiteral("id")).isNull());
}

void TstMcp::notificationGetsNoResponse()
{
    oic::mcp::Server server;
    QVERIFY(server.handleLine(R"({"jsonrpc":"2.0","method":"notifications/initialized"})").isEmpty());
    QVERIFY(server.handleLine(R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":1}})").isEmpty());
    QVERIFY(server.handleLine("").isEmpty());
}

void TstMcp::stringIdIsEchoed()
{
    const QJsonObject response = responseFor(R"({"jsonrpc":"2.0","id":"abc","method":"ping"})");
    QVERIFY(response.value(QStringLiteral("id")).isString());
    QCOMPARE(response.value(QStringLiteral("id")).toString(), QStringLiteral("abc"));
}

void TstMcp::unknownToolIsInvalidParams()
{
    const QJsonObject response = responseFor(
        R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"no_such_tool","arguments":{}}})");
    QCOMPARE(response.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32602);
}

void TstMcp::nonObjectIsInvalidRequest()
{
    const QJsonObject response = responseFor("[]");
    QCOMPARE(response.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(), -32600);
    QVERIFY(response.value(QStringLiteral("id")).isNull());
}

void TstMcp::responsesAreSingleLines()
{
    oic::mcp::Server server;
    const QByteArray response = server.handleLine(R"({"jsonrpc":"2.0","id":8,"method":"tools/list"})");
    QVERIFY(!response.isEmpty());
    QVERIFY(!response.contains('\n'));
    QVERIFY(!response.contains('\r'));
}

void TstMcp::specDefaultsComeFromTheProfile()
{
    const QJsonObject args{{QStringLiteral("prompt"), QStringLiteral("a cat")}};
    QString error;
    const oic::jobs::JobSpec spec =
        oic::mcp::buildJobSpec(args, sampleProfile(), /*editMode=*/false, &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(spec.profileName, QStringLiteral("p"));
    QCOMPARE(spec.baseUrl, QStringLiteral("https://api.example.com"));
    QCOMPARE(spec.protocolHint, QStringLiteral("openai"));
    QCOMPARE(spec.credentialTarget, QStringLiteral("image-client/profile/p"));
    QCOMPARE(spec.timeoutSeconds, 300);
    QCOMPARE(spec.image.model, QStringLiteral("gpt-image-2"));
    QCOMPARE(spec.image.size, QStringLiteral("1024x1024"));
    QCOMPARE(spec.image.n, 1);
    QCOMPARE(spec.image.editMode, false);
    QCOMPARE(spec.image.outputCompression, -1);
}

void TstMcp::specRejectsOutOfRangeN()
{
    const QJsonObject args{{QStringLiteral("prompt"), QStringLiteral("a cat")},
                           {QStringLiteral("n"), 5}};
    QString error;
    const oic::jobs::JobSpec spec =
        oic::mcp::buildJobSpec(args, sampleProfile(), /*editMode=*/false, &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(spec.profileName.isEmpty());
}

void TstMcp::specRequiresReferencesForEdit()
{
    const QJsonObject args{{QStringLiteral("prompt"), QStringLiteral("make it blue")}};
    QString error;
    oic::mcp::buildJobSpec(args, sampleProfile(), /*editMode=*/true, &error);
    QVERIFY2(error.contains(QStringLiteral("edit_images")), qPrintable(error));
}

void TstMcp::specAcceptsDataUrlReferences()
{
    const QJsonObject args{
        {QStringLiteral("prompt"), QStringLiteral("make it blue")},
        {QStringLiteral("edit_images"),
         QJsonArray{QStringLiteral("data:image/png;base64,iVBORw0KGgo=")}}};
    QString error;
    const oic::jobs::JobSpec spec =
        oic::mcp::buildJobSpec(args, sampleProfile(), /*editMode=*/true, &error);

    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(spec.image.references.size(), 1);
    QCOMPARE(spec.image.references.first().contentType, QStringLiteral("image/png"));
    QVERIFY(spec.image.editMode);
}

void TstMcp::outcomeReturnsResourceLinks()
{
    oic::jobs::JobOutcome outcome;
    outcome.jobId = QStringLiteral("job-1");
    outcome.status = QStringLiteral("succeeded");
    outcome.durationMs = 5;
    oic::jobs::AssetRef asset;
    asset.id = QStringLiteral("asset-1");
    asset.relPath = QStringLiteral("j1/result.png");
    asset.mime = QStringLiteral("image/png");
    outcome.assets.append(asset);

    const QJsonObject result = oic::mcp::outcomeToResult(outcome, /*inlineImages=*/false, nullptr);
    QCOMPARE(result.value(QStringLiteral("isError")).toBool(), false);

    const QJsonArray content = result.value(QStringLiteral("content")).toArray();
    QCOMPARE(content.size(), 2);
    QCOMPARE(content.at(1).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("resource_link"));
    QCOMPARE(content.at(1).toObject().value(QStringLiteral("uri")).toString(),
             QStringLiteral("image-client://assets/asset-1"));
}

void TstMcp::outcomeReturnsInlineImagesOnRequest()
{
    oic::jobs::JobOutcome outcome;
    outcome.jobId = QStringLiteral("job-1");
    outcome.status = QStringLiteral("succeeded");
    oic::jobs::AssetRef asset;
    asset.id = QStringLiteral("asset-1");
    asset.relPath = QStringLiteral("j1/result.png");
    asset.mime = QStringLiteral("image/png");
    outcome.assets.append(asset);

    const QJsonObject result = oic::mcp::outcomeToResult(
        outcome, /*inlineImages=*/true,
        [](const QString &, QString *) { return QByteArray("abc"); });
    const QJsonArray content = result.value(QStringLiteral("content")).toArray();
    QCOMPARE(content.at(1).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("image"));
    QCOMPARE(content.at(1).toObject().value(QStringLiteral("data")).toString(),
             QString::fromLatin1(QByteArray("abc").toBase64()));
}

void TstMcp::generateImageRunsTheJob()
{
    auto captured = std::make_shared<oic::jobs::JobSpec>();
    oic::mcp::Server server(fakeHost(captured));

    const QJsonObject response = call(
        server,
        R"({"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"generate_image","arguments":{"prompt":"a cat","n":2,"size":"512x512"}}})");

    const QJsonObject result = response.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("isError")).toBool(), false);
    QCOMPARE(captured->image.n, 2);
    QCOMPARE(captured->image.size, QStringLiteral("512x512"));
    QCOMPARE(captured->image.model, QStringLiteral("gpt-image-2"));
    QCOMPARE(captured->profileName, QStringLiteral("p"));
    QVERIFY(!captured->jobId.isEmpty());

    const QJsonArray content = result.value(QStringLiteral("content")).toArray();
    QCOMPARE(content.at(1).toObject().value(QStringLiteral("uri")).toString(),
             QStringLiteral("image-client://assets/asset-1"));
}

void TstMcp::failedJobBecomesAToolError()
{
    auto captured = std::make_shared<oic::jobs::JobSpec>();
    oic::mcp::Server server(fakeHost(captured, QStringLiteral("failed")));

    const QJsonObject response = call(
        server,
        R"({"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"generate_image","arguments":{"prompt":"a cat"}}})");
    const QJsonObject result = response.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("isError")).toBool(), true);
    QVERIFY(result.value(QStringLiteral("content"))
                .toArray()
                .at(0)
                .toObject()
                .value(QStringLiteral("text"))
                .toString()
                .contains(QStringLiteral("upstream exploded")));
}

void TstMcp::getJobReturnsStatusAndAssets()
{
    oic::mcp::ToolHost host;
    host.job = [](const QString &id, bool *found, QString *error) {
        *found = true;
        error->clear();
        oic::store::Job job;
        job.id = id;
        job.status = QStringLiteral("succeeded");
        return job;
    };
    host.assets = [](const QString &, QString *error) {
        error->clear();
        oic::store::Asset asset;
        asset.id = QStringLiteral("asset-9");
        asset.relPath = QStringLiteral("j9/x.png");
        asset.mime = QStringLiteral("image/png");
        return QList<oic::store::Asset>{asset};
    };

    oic::mcp::Server server(host);
    const QJsonObject response = call(
        server,
        R"({"jsonrpc":"2.0","id":11,"method":"tools/call","params":{"name":"get_job","arguments":{"job_id":"j9"}}})");

    const QJsonObject result = response.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("isError")).toBool(), false);
    const QJsonArray content = result.value(QStringLiteral("content")).toArray();
    QVERIFY(content.at(0).toObject().value(QStringLiteral("text")).toString().contains(QStringLiteral("succeeded")));
    QCOMPARE(content.at(1).toObject().value(QStringLiteral("uri")).toString(),
             QStringLiteral("image-client://assets/asset-9"));
}

void TstMcp::importReferenceIsReportedAsUnavailable()
{
    oic::mcp::Server server;
    const QJsonObject response = call(
        server,
        R"({"jsonrpc":"2.0","id":12,"method":"tools/call","params":{"name":"import_reference_image","arguments":{"path":"C:/x.png"}}})");
    const QJsonObject result = response.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("isError")).toBool(), true);
    QVERIFY(result.value(QStringLiteral("content"))
                .toArray()
                .at(0)
                .toObject()
                .value(QStringLiteral("text"))
                .toString()
                .contains(QStringLiteral("not implemented")));
}

void TstMcp::cancellationStopsARunningJob()
{
    oic::mcp::ToolHost host;
    const oic::store::Profile profile = sampleProfile();
    host.profiles = [profile](QString *) { return QList<oic::store::Profile>{profile}; };
    host.runJob = [](const oic::jobs::JobSpec &spec, oic::jobs::CancelToken *cancel, QString *) {
        // Stands in for a blocking upstream request: it only returns once cancelled.
        while (!cancel->cancelled()) {
            QThread::msleep(5);
        }
        oic::jobs::JobOutcome outcome;
        outcome.jobId = spec.jobId;
        outcome.status = QStringLiteral("cancelled");
        return outcome;
    };

    const QByteArray input =
        R"({"jsonrpc":"2.0","id":21,"method":"tools/call","params":{"name":"generate_image","arguments":{"prompt":"a cat"}}})"
        "\n"
        R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":21}})"
        "\n";

    QBuffer buffer;
    buffer.setData(input);
    QVERIFY(buffer.open(QIODevice::ReadOnly));

    QVector<QByteArray> frames;
    QMutex mutex;
    oic::mcp::Server server(host);
    const int exit = server.run(&buffer, [&](const QByteArray &frame) {
        QMutexLocker locker(&mutex);
        frames.append(frame);
    });

    QCOMPARE(exit, 0);
    QCOMPARE(frames.size(), 1);
    const QJsonObject response = QJsonDocument::fromJson(frames.first()).object();
    QCOMPARE(response.value(QStringLiteral("id")).toInt(), 21);
    const QJsonObject result = response.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("isError")).toBool(), true);
    QVERIFY(result.value(QStringLiteral("content"))
                .toArray()
                .at(0)
                .toObject()
                .value(QStringLiteral("text"))
                .toString()
                .contains(QStringLiteral("cancelled")));
}

QTEST_GUILESS_MAIN(TstMcp)
#include "tst_mcp.moc"
