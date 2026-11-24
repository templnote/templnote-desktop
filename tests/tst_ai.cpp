#include "adapter/AIEngineAdapters.h"
#include "ai/ToolSchema.h"
#include "ai/PipelineRunner.h"
#include "engine/CapabilityGate.h"
#include "plugins/PluginManager.h"
#include "engine/EditorSession.h"
#include "engine/Types.h"
#include "sync/SyncManager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QVariantMap>
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

  void testAllEngineCommandsHaveTools() {
    const QJsonArray tools = ToolSchema::definitions();
    QStringList names;
    for (const QJsonValue &value : tools)
      names.append(value.toObject()
                        .value(QStringLiteral("function")).toObject()
                        .value(QStringLiteral("name")).toString());

    // Every command the editor engine accepts must be callable by the agent.
    for (int i = 0; i <= static_cast<int>(Engine::Command::Type::ApplyJson); ++i) {
      const auto type = static_cast<Engine::Command::Type>(i);
      const QString name = Engine::Command::typeName(type);
      QVERIFY2(names.contains(name), qPrintable(name));
    }
    // All fourteen block types must be documented in the harness guidance.
    const QString guidance = ToolSchema::editingGuidance();
    for (int type = 0; type <= 13; ++type)
      QVERIFY2(guidance.contains(QString::number(type)),
               qPrintable(QString::number(type)));
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

    const auto parsed = ToolSchema::fromToolCall(call);
    QVERIFY(parsed.ok);
    QCOMPARE(parsed.command.value(QStringLiteral("type")).toString(),
             QStringLiteral("setText"));

    EditorSession session;
    QVERIFY(session.loadJson(doc()));
    Adapter::SessionDocumentPort port(&session);
    QJsonObject patch;
    QString error;
    QVERIFY(port.applyCommand(parsed.value(), QStringLiteral("ai-agent"),
                              &patch, &error));
    QCOMPARE(session.document().findBlock(QStringLiteral("b1"))->text,
             QStringLiteral("AI text"));
    QVERIFY(patch.value(QStringLiteral("ops")).isArray());
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
    const auto parsed = ToolSchema::fromToolCall(call);
    QVERIFY(parsed.ok);
    QCOMPARE(parsed.command.value(QStringLiteral("text")).toString(),
             QStringLiteral("Bye"));
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

  void testNoDefaultAgentSequence() {
    // There is no built-in pipeline. The runner starts with no agents and the
    // host supplies the user's configured selection.
    PipelineRunner pipeline;
    QVERIFY(pipeline.agentRuns().isEmpty());
    const QVector<AgentRun> runs{{QStringLiteral("writer"),
                                  QStringLiteral("Follow the prompt."), true},
                                 {QStringLiteral("reviewer"), QString(), true}};
    pipeline.setAgentRuns(runs);
    QCOMPARE(pipeline.agentRuns().size(), 2);
    QCOMPARE(pipeline.agentRuns().first().id, QStringLiteral("writer"));
  }

  void testAgentLabel() {
    QCOMPARE(Adapter::agentLabel(QStringLiteral("writer")), QStringLiteral("Writer"));
    QCOMPARE(Adapter::agentLabel(QStringLiteral("agent-1")),
             QStringLiteral("Agent 1"));
  }

  void testDocumentDiff() {
    const QString original = QString::fromUtf8(
        QJsonDocument(doc()).toJson(QJsonDocument::Compact));

    QJsonObject changed = doc();
    QJsonArray blocks = changed.value(QStringLiteral("blocks")).toArray();
    QJsonObject first = blocks.at(0).toObject();
    first.insert(QStringLiteral("text"), QStringLiteral("Hello AI"));
    blocks.replace(0, first);

    QJsonObject added;
    added.insert(QStringLiteral("id"), QStringLiteral("b2"));
    added.insert(QStringLiteral("type"), QStringLiteral("paragraph"));
    added.insert(QStringLiteral("text"), QStringLiteral("New block"));
    blocks.append(added);
    changed.insert(QStringLiteral("blocks"), blocks);

    const QVariantList diff = Adapter::diffDocuments(
        original,
        QString::fromUtf8(QJsonDocument(changed).toJson(QJsonDocument::Compact)));
    QCOMPARE(diff.size(), 2);

    const QVariantMap changedEntry = diff.at(0).toMap();
    QCOMPARE(changedEntry.value(QStringLiteral("kind")).toString(),
             QStringLiteral("changed"));
    QCOMPARE(changedEntry.value(QStringLiteral("before")).toString(),
             QStringLiteral("Hello world"));
    QCOMPARE(changedEntry.value(QStringLiteral("after")).toString(),
             QStringLiteral("Hello AI"));

    const QVariantMap addedEntry = diff.at(1).toMap();
    QCOMPARE(addedEntry.value(QStringLiteral("kind")).toString(),
             QStringLiteral("added"));
    QCOMPARE(addedEntry.value(QStringLiteral("after")).toString(),
             QStringLiteral("New block"));
  }

  void testReplayCommands() {
    const QString original = QString::fromUtf8(
        QJsonDocument(doc()).toJson(QJsonDocument::Compact));

    QJsonObject command;
    command.insert(QStringLiteral("type"), QStringLiteral("setText"));
    command.insert(QStringLiteral("blockId"), QStringLiteral("b1"));
    command.insert(QStringLiteral("text"), QStringLiteral("Replayed"));

    QVector<Adapter::StagedCommand> commands;
    commands.append({command, QStringLiteral("write")});

    const Adapter::ReplayResult result =
        Adapter::replayCommands(original, commands);
    QVERIFY2(result.ok, qPrintable(result.error));

    const QJsonObject document =
        QJsonDocument::fromJson(QJsonDocument(result.document).toJson()).object();
    const QJsonArray blocks = document.value(QStringLiteral("blocks")).toArray();
    QCOMPARE(blocks.at(0).toObject().value(QStringLiteral("text")).toString(),
             QStringLiteral("Replayed"));
    QVERIFY(!result.patch.value(QStringLiteral("ops")).toArray().isEmpty());

    // A rejected replay leaves the editor untouched and names the agent.
    QJsonObject invalid;
    invalid.insert(QStringLiteral("type"), QStringLiteral("setText"));
    invalid.insert(QStringLiteral("blockId"), QStringLiteral("missing"));
    invalid.insert(QStringLiteral("text"), QStringLiteral("nope"));
    QVector<Adapter::StagedCommand> badCommands;
    badCommands.append({invalid, QStringLiteral("agent-1")});
    const Adapter::ReplayResult bad = Adapter::replayCommands(original, badCommands);
    QVERIFY(!bad.ok);
    QVERIFY(bad.error.contains(QStringLiteral("agent-1")));
  }
};

QTEST_GUILESS_MAIN(AiTests)
#include "tst_ai.moc"
