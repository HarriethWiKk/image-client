// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
//
// MCP stdio surface: line-delimited JSON-RPC 2.0 framing, initialize / ping / tools/list,
// and the error paths a client hits first. The tool handlers are a later milestone, so
// tools/call is only checked for its "known tool, no handler yet" contract.

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTest>

#include "oic/mcp/server.h"

namespace {

QJsonObject responseFor(const QByteArray &line)
{
    oic::mcp::Server server;
    const QByteArray response = server.handleLine(line);
    return QJsonDocument::fromJson(response).object();
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
    void knownToolReportsTheMissingHandler();
    void nonObjectIsInvalidRequest();
    void responsesAreSingleLines();
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

void TstMcp::knownToolReportsTheMissingHandler()
{
    const QJsonObject response = responseFor(
        R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"generate_image","arguments":{"prompt":"a cat"}}})");
    const QJsonObject result = response.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("isError")).toBool(), true);

    const QJsonArray content = result.value(QStringLiteral("content")).toArray();
    QCOMPARE(content.size(), 1);
    QCOMPARE(content.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("text"));
    QVERIFY(content.at(0).toObject().value(QStringLiteral("text")).toString().contains(QStringLiteral("not implemented")));
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

QTEST_GUILESS_MAIN(TstMcp)
#include "tst_mcp.moc"
