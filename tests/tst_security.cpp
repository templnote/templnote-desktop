#include "ai/AgentHttpServer.h"
#include "ai/ModelRouter.h"
#include "ai/PipelineRunner.h"
#include "ai/ProviderRegistry.h"
#include "ai/ToolSchema.h"
#include "engine/CapabilityGate.h"
#include "engine/ChangeFeed.h"
#include "engine/EditorSession.h"
#include "engine/Types.h"
#include "plugins/PluginManager.h"

#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace Templnote;
using namespace Templnote::Engine;
using namespace Templnote::AI;

namespace {

QJsonObject document() {
  QJsonObject block;
  block.insert(QStringLiteral("id"), QStringLiteral("b1"));
  block.insert(QStringLiteral("type"), QStringLiteral("paragraph"));
  block.insert(QStringLiteral("text"), QStringLiteral("Hello world"));
  QJsonArray blocks;
  blocks.append(block);
  QJsonObject doc;
  doc.insert(QStringLiteral("format"), QStringLiteral("templnote"));
  doc.insert(QStringLiteral("version"), 1);
  doc.insert(QStringLiteral("blocks"), blocks);
  return doc;
}

// QTEST_GUILESS_MAIN has no running event loop while a slot executes, so the
// server's newConnection/readyRead signals need qWait() to be delivered.
QByteArray exchange(quint16 port, const QByteArray &raw) {
  QTcpSocket socket;
  socket.connectToHost(QHostAddress::LocalHost, port);
  if (!socket.waitForConnected(2000))
    return {};
  QTest::qWait(20);
  socket.write(raw);
  socket.waitForBytesWritten(2000);
  QByteArray response;
  for (int i = 0; i < 50; ++i) {
    QTest::qWait(20);
    response += socket.readAll();
    const int separator = response.indexOf("\r\n\r\n");
    if (separator < 0)
      continue;
    const QByteArray header = response.left(separator).toLower();
    const int index = header.indexOf("content-length:");
    if (index < 0)
      return response;
    const int length = header.mid(index + 15).trimmed().toInt();
    if (response.size() >= separator + 4 + length)
      return response;
  }
  return response;
}

class FakeRouter : public ModelRouter {
  Q_OBJECT
public:
  QVector<ChatResponse> scripted;
  QVector<ChatRequest> seen;

  void chat(const ChatRequest &request) override {
    seen.append(request);
    QTimer::singleShot(0, this, [this] {
      if (!scripted.isEmpty())
        emit finished(scripted.takeFirst());
    });
  }
};

} // namespace

class SecurityTests : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    static QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("TEMPLNOTE_CONFIG_DIR", dir.path().toUtf8());
    QCoreApplication::setOrganizationName("TemplnoteSecurityTest");
    QCoreApplication::setApplicationName("TemplnoteSecurityTest");
  }

  void testGateIsEnforcedBySession() {
    EditorSession session;
    QVERIFY(session.loadJson(document()));
    CapabilityGate gate;
    gate.setRole(QStringLiteral("viewer"), CapabilityGate::Role::Viewer);

    QJsonObject args;
    args.insert(QStringLiteral("blockId"), QStringLiteral("b1"));
    args.insert(QStringLiteral("text"), QStringLiteral("Hacked"));
    QJsonObject call;
    call.insert(QStringLiteral("name"), QStringLiteral("setText"));
    call.insert(QStringLiteral("arguments"), args);
    Command cmd = ToolSchema::fromToolCall(call).value();
    cmd.actor = Actor::agent(QStringLiteral("viewer"));

    // Without a gate the unit-test behaviour is permissive.
    QVERIFY(session.apply(cmd).ok());
    session.loadJson(document());
    session.setCapabilityGate(&gate);
    auto denied = session.apply(cmd);
    QVERIFY(!denied.ok());
    QVERIFY(denied.error().contains(QStringLiteral("AI")));

    gate.setRole(QStringLiteral("ai-agent"), CapabilityGate::Role::Owner);
    cmd.actor = Actor::agent(QStringLiteral("ai-agent"));
    QVERIFY(session.apply(cmd).ok());
  }

  void testOpenAiFunctionArgumentsAreParsed() {
    QJsonObject arguments;
    arguments.insert(QStringLiteral("blockId"), QStringLiteral("b1"));
    arguments.insert(QStringLiteral("text"), QStringLiteral("From function"));
    QJsonObject function;
    function.insert(QStringLiteral("name"), QStringLiteral("setText"));
    function.insert(QStringLiteral("arguments"), arguments);
    QJsonObject call;
    call.insert(QStringLiteral("id"), QStringLiteral("c1"));
    call.insert(QStringLiteral("type"), QStringLiteral("function"));
    call.insert(QStringLiteral("function"), function);

    auto parsed = ToolSchema::fromToolCall(call);
    QVERIFY(parsed.ok());
    QCOMPARE(parsed.value().text, QStringLiteral("From function"));
  }

  void testHttpRequiresTokenAndNoWildcardCors() {
    EditorSession session;
    QVERIFY(session.loadJson(document()));
    AgentHttpServer server;
    server.setDocumentProvider(&session);
    server.setAuthToken(QStringLiteral("secret-token"));
    QVERIFY(server.listen(0));

    const QByteArray unauth = exchange(
        server.port(),
        "GET /document HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    QVERIFY(unauth.startsWith("HTTP/1.1 401"));
    // HTTP header names are case-insensitive; QHttpServer emits lowercase.
    QVERIFY(unauth.toLower().contains("www-authenticate: bearer"));
    QVERIFY(!unauth.contains("Access-Control-Allow-Origin"));

    const QByteArray ok = exchange(
        server.port(),
        "GET /document HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer "
        "secret-token\r\nConnection: close\r\n\r\n");
    QVERIFY(ok.startsWith("HTTP/1.1 200"));
    QVERIFY(ok.contains("\"blocks\""));
  }

  void testHttpIgnoresCallerActorAndGates() {
    EditorSession session;
    QVERIFY(session.loadJson(document()));
    CapabilityGate gate;
    gate.setRole(QStringLiteral("http-agent"), CapabilityGate::Role::Owner);
    session.setActor(Actor::agent(QStringLiteral("http-agent")));
    session.setCapabilityGate(&gate);
    ChangeFeed feed(&session);

    AgentHttpServer server;
    server.setDocumentProvider(&session);
    server.setChangesProvider(&feed);
    server.setAuthToken(QStringLiteral("t"));
    QVERIFY(server.listen(0));

    const QByteArray body =
        "{\"type\":\"setText\",\"blockId\":\"b1\",\"text\":\"Via HTTP\","
        "\"actor\":{\"id\":\"evil\",\"kind\":\"agent\"}}";
    const QByteArray raw =
        QByteArray("POST /command HTTP/1.1\r\nHost: localhost\r\n")
            .append("Authorization: Bearer t\r\nContent-Type: application/json\r\n")
            .append("Content-Length: ")
            .append(QByteArray::number(body.size()))
            .append("\r\nConnection: close\r\n\r\n")
            .append(body);
    const QByteArray response = exchange(server.port(), raw);
    QVERIFY(response.startsWith("HTTP/1.1 200"));
    QCOMPARE(session.document().findBlock(QStringLiteral("b1"))->text,
             QStringLiteral("Via HTTP"));
    QCOMPARE(feed.events().last().patch.actor.id,
             QStringLiteral("http-agent"));
  }

  void testSplitBodyIsBuffered() {
    EditorSession session;
    QVERIFY(session.loadJson(document()));
    AgentHttpServer server;
    server.setDocumentProvider(&session);
    server.setAuthToken(QStringLiteral("t"));
    QVERIFY(server.listen(0));

    const QByteArray body =
        "{\"type\":\"setText\",\"blockId\":\"b1\",\"text\":\"Split\"}";
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, server.port());
    QVERIFY(socket.waitForConnected(2000));
    QTest::qWait(20);
    const QByteArray header =
        QByteArray("POST /command HTTP/1.1\r\nHost: localhost\r\n")
            .append("Authorization: Bearer t\r\nContent-Type: application/json\r\n")
            .append("Content-Length: ")
            .append(QByteArray::number(body.size()))
            .append("\r\nConnection: close\r\n\r\n");
    socket.write(header + body.left(5));
    socket.waitForBytesWritten(1000);
    QTest::qWait(50);
    socket.write(body.mid(5));
    socket.waitForBytesWritten(1000);
    QByteArray response;
    for (int i = 0; i < 50; ++i) {
      QTest::qWait(20);
      response += socket.readAll();
      if (response.contains("HTTP/1.1 200"))
        break;
    }
    QVERIFY(response.startsWith("HTTP/1.1 200"));
  }

  void testPluginApprovalCannotComeFromAgent() {
    PluginManager manager;
    QJsonObject spec;
    spec.insert(QStringLiteral("id"), QStringLiteral("security-plugin"));
    QJsonObject create;
    create.insert(QStringLiteral("name"), QStringLiteral("createPlugin"));
    create.insert(QStringLiteral("arguments"), spec);
    QVERIFY(manager.applyPluginTool(create).value(QStringLiteral("ok")).toBool());

    QJsonObject approveArgs;
    approveArgs.insert(QStringLiteral("pluginId"), QStringLiteral("security-plugin"));
    QJsonObject approve;
    approve.insert(QStringLiteral("name"), QStringLiteral("approvePlugin"));
    approve.insert(QStringLiteral("arguments"), approveArgs);
    QVERIFY(!manager.applyPluginTool(approve).value(QStringLiteral("ok")).toBool());
    QVERIFY(manager.needsApproval(QStringLiteral("security-plugin")));
  }

  void testPipelineFeedsToolResultsBack() {
    EditorSession session;
    QVERIFY(session.loadJson(document()));

    QJsonObject arguments;
    arguments.insert(QStringLiteral("blockId"), QStringLiteral("b1"));
    arguments.insert(QStringLiteral("text"), QStringLiteral("Agent loop"));
    QJsonObject function;
    function.insert(QStringLiteral("name"), QStringLiteral("setText"));
    function.insert(QStringLiteral("arguments"), arguments);
    QJsonObject toolCall;
    toolCall.insert(QStringLiteral("id"), QStringLiteral("call-1"));
    toolCall.insert(QStringLiteral("type"), QStringLiteral("function"));
    toolCall.insert(QStringLiteral("function"), function);
    QJsonArray toolCalls;
    toolCalls.append(toolCall);

    FakeRouter router;
    ChatResponse first;
    first.ok = true;
    first.toolCalls = toolCalls;
    first.promptTokens = 10;
    first.completionTokens = 3;
    ChatResponse second;
    second.ok = true;
    second.text = QStringLiteral("done");
    router.scripted = {first, second};

    PipelineRunner pipeline;
    pipeline.setSession(&session);
    pipeline.setRouter(&router);
    pipeline.setStages({{QStringLiteral("write"), QStringLiteral("prompt"), true}});

    QEventLoop loop;
    PipelineResult result;
    QObject::connect(&pipeline, &PipelineRunner::finished, &loop,
                     [&](const PipelineResult &r) {
                       result = r;
                       loop.quit();
                     });
    pipeline.start(QStringLiteral("rewrite"));
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();

    QVERIFY(result.ok);
    QCOMPARE(router.seen.size(), 2);
    QCOMPARE(router.seen.at(1).messages.last().role, QStringLiteral("tool"));
    QCOMPARE(router.seen.at(1).messages.last().toolCallId,
             QStringLiteral("call-1"));
    QCOMPARE(result.promptTokens, 10);
    QCOMPARE(result.completionTokens, 3);
    QCOMPARE(session.document().findBlock(QStringLiteral("b1"))->text,
             QStringLiteral("Agent loop"));
  }
};

QTEST_GUILESS_MAIN(SecurityTests)
#include "tst_security.moc"
