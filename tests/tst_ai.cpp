#include "ai/ToolSchema.h"
#include "ai/PipelineRunner.h"
#include "engine/CapabilityGate.h"
#include "plugins/PluginManager.h"
#include "engine/EditorSession.h"
#include "sync/SyncManager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace Templnote;
using namespace Templnote::Engine;
using namespace Templnote::AI;

class AiTests : public QObject {
  Q_OBJECT

private:
  static QJsonObject doc() {
    QJsonObject block;
    block.insert(QStringLiteral("id"), QStringLiteral("b1"));
    block.insert(QStringLiteral("type"), QStringLiteral("paragraph"));
    block.insert(QStringLiteral("text"), QStringLiteral("Hello world"));
    QJsonArray blocks;
    blocks.append(block);
    QJsonObject d;
    d.insert(QStringLiteral("format"), QStringLiteral("templnote"));
    d.insert(QStringLiteral("version"), 1);
    d.insert(QStringLiteral("blocks"), blocks);
    return d;
  }

private slots:
  void initTestCase() {
    static QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("TEMPLNOTE_CONFIG_DIR", dir.path().toUtf8());
    QCoreApplication::setOrganizationName("TemplnoteAiTest");
    QCoreApplication::setApplicationName("TemplnoteAiTest");
  }

  void testToolDefinitions() {
    const QJsonArray tools = ToolSchema::definitions();
    QVERIFY(tools.size() >= 6);
    for (const QJsonValue &value : tools) {
      const QJsonObject fn = value.toObject()
                                 .value(QStringLiteral("function")).toObject();
      QVERIFY(!fn.value(QStringLiteral("name")).toString().isEmpty());
      QVERIFY(fn.value(QStringLiteral("parameters")).isObject());
    }
  }

  void testToolCallParsingAndApply() {
    QJsonObject arguments;
    arguments.insert(QStringLiteral("blockId"), QStringLiteral("b1"));
    arguments.insert(QStringLiteral("text"), QStringLiteral("AI text"));
    QJsonObject call;
    call.insert(QStringLiteral("name"), QStringLiteral("setText"));
    call.insert(QStringLiteral("arguments"), arguments);

    auto parsed = ToolSchema::fromToolCall(call);
    QVERIFY(parsed.ok());
    QCOMPARE(parsed.value().type, Command::Type::SetText);

    EditorSession session;
    QVERIFY(session.loadJson(doc()));
    auto result = session.apply(parsed.value());
    QVERIFY(result.ok());
    QCOMPARE(session.document().findBlock(QStringLiteral("b1"))->text,
             QStringLiteral("AI text"));
    QCOMPARE(result.value().actor.kind, Actor::Kind::Agent);
  }

  void testToolCallStringArguments() {
    QJsonObject call;
    call.insert(QStringLiteral("name"), QStringLiteral("replaceRange"));
    QJsonObject args;
    args.insert(QStringLiteral("blockId"), QStringLiteral("b1"));
    args.insert(QStringLiteral("start"), 0);
    args.insert(QStringLiteral("end"), 5);
    args.insert(QStringLiteral("text"), QStringLiteral("Bye"));
    call.insert(QStringLiteral("arguments"),
                QString::fromUtf8(QJsonDocument(args).toJson(
                    QJsonDocument::Compact)));
    auto parsed = ToolSchema::fromToolCall(call);
    QVERIFY(parsed.ok());
    QCOMPARE(parsed.value().text, QStringLiteral("Bye"));
  }

  void testCapabilityGate() {
    CapabilityGate gate;
    gate.setRole(QStringLiteral("owner"), CapabilityGate::Role::Owner);
    gate.setRole(QStringLiteral("viewer"), CapabilityGate::Role::Viewer);

    Command aiCmd;
    aiCmd.type = Command::Type::SetText;
    aiCmd.actor = Actor::agent(QStringLiteral("owner"));
    QVERIFY(gate.allows(aiCmd));

    aiCmd.actor = Actor::agent(QStringLiteral("viewer"));
    QString reason;
    QVERIFY(!gate.allows(aiCmd, &reason));
    QVERIFY(reason.contains(QStringLiteral("AI")));

    Command humanCmd;
    humanCmd.type = Command::Type::InsertText;
    humanCmd.actor = Actor::human(QStringLiteral("viewer"));
    QVERIFY(!gate.allows(humanCmd)); // read-only
  }

  void testPluginTools() {
    const QJsonArray tools = ToolSchema::pluginDefinitions();
    QVERIFY(tools.size() >= 2);
    QStringList names;
    for (const QJsonValue &value : tools)
      names.append(value.toObject().value(QStringLiteral("function"))
                       .toObject().value(QStringLiteral("name")).toString());
    QVERIFY(names.contains(QStringLiteral("createPlugin")));
    QVERIFY(names.contains(QStringLiteral("writePluginFile")));
    // Approval is a user consent step, never an agent-callable tool.
    QVERIFY(!names.contains(QStringLiteral("approvePlugin")));
  }

  void testPluginToolExecution() {
    PluginManager manager;
    QJsonObject spec;
    spec.insert(QStringLiteral("id"), QStringLiteral("ai-made-plugin"));
    spec.insert(QStringLiteral("name"), QStringLiteral("AI Plugin"));

    QJsonObject createCall;
    createCall.insert(QStringLiteral("name"), QStringLiteral("createPlugin"));
    createCall.insert(QStringLiteral("arguments"), spec);
    auto created = manager.applyPluginTool(createCall);
    QVERIFY(created.value(QStringLiteral("ok")).toBool());

    QJsonObject writeArgs;
    writeArgs.insert(QStringLiteral("pluginId"), QStringLiteral("ai-made-plugin"));
    writeArgs.insert(QStringLiteral("path"), QStringLiteral("Widget.qml"));
    writeArgs.insert(QStringLiteral("contents"),
                     QStringLiteral("import QtQuick\nItem {}"));
    QJsonObject writeCall;
    writeCall.insert(QStringLiteral("name"), QStringLiteral("writePluginFile"));
    writeCall.insert(QStringLiteral("arguments"), writeArgs);
    auto written = manager.applyPluginTool(writeCall);
    QVERIFY(written.value(QStringLiteral("ok")).toBool());

    QJsonObject approveArgs;
    approveArgs.insert(QStringLiteral("pluginId"), QStringLiteral("ai-made-plugin"));
    QJsonObject approveCall;
    approveCall.insert(QStringLiteral("name"), QStringLiteral("approvePlugin"));
    approveCall.insert(QStringLiteral("arguments"), approveArgs);
    QVERIFY(!manager.applyPluginTool(approveCall)
                 .value(QStringLiteral("ok")).toBool());
    QVERIFY(manager.needsApproval(QStringLiteral("ai-made-plugin")));
    QVERIFY(manager.approvePlugin(QStringLiteral("ai-made-plugin")));
    QVERIFY(!manager.needsApproval(QStringLiteral("ai-made-plugin")));
    QVERIFY(manager.isEnabled(QStringLiteral("ai-made-plugin")) == false);
  }

  void testPipelineDefaultStages() {
    const QVector<PipelineStage> stages = PipelineRunner::defaultStages();
    QCOMPARE(stages.size(), 5);
    QCOMPARE(stages.first().id, QStringLiteral("write"));
    QCOMPARE(stages.last().id, QStringLiteral("validation"));
  }
};

QTEST_GUILESS_MAIN(AiTests)
#include "tst_ai.moc"
