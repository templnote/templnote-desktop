// Deterministic complex-document test: drives the agent's tool wire format
// (ToolSchema -> ToolRegistry validation -> DocumentPort -> editor engine)
// through four realistic multi-block scenarios.

#include "adapter/AIEngineAdapters.h"
#include "ai/ModelRouter.h"
#include "ai/SkillCatalog.h"
#include "ai/ToolRegistry.h"
#include "ai/ToolSchema.h"
#include "engine/CapabilityGate.h"
#include "engine/EditorSession.h"
#include "engine/Types.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace Templnote;
using namespace Templnote::AI;

class ComplexDocsTests : public QObject {
  Q_OBJECT

private:
  static QJsonObject initialDocument() {
    QJsonArray blocks;
    auto add = [&](const QString &id, const QString &type,
                   const QString &text) {
      QJsonObject b;
      b.insert(QStringLiteral("id"), id);
      b.insert(QStringLiteral("type"), type);
      b.insert(QStringLiteral("text"), text);
      blocks.append(b);
    };
    add(QStringLiteral("b-title"), QStringLiteral("paragraph"),
        QStringLiteral("Project Notes"));
    add(QStringLiteral("b-intro"), QStringLiteral("paragraph"),
        QStringLiteral("This project explores local-first document editing with AI assistance."));
    add(QStringLiteral("b-old"), QStringLiteral("paragraph"),
        QStringLiteral("Old conclusion that should be replaced."));
    QJsonObject doc;
    doc.insert(QStringLiteral("format"), QStringLiteral("templnote"));
    doc.insert(QStringLiteral("version"), 1);
    doc.insert(QStringLiteral("blocks"), blocks);
    return doc;
  }

  struct Fixture {
    Engine::EditorSession session;
    Adapter::SessionDocumentPort port{&session};
    ToolRegistry registry;

    Fixture() {
      session.loadJson(initialDocument());
      session.setActor(Engine::Actor::human(QStringLiteral("local-owner")));
      for (const QJsonValue &value : ToolSchema::definitions()) {
        const QJsonObject fn =
            value.toObject().value(QStringLiteral("function")).toObject();
        ToolInfo tool;
        tool.name = fn.value(QStringLiteral("name")).toString();
        tool.inputSchema = fn.value(QStringLiteral("parameters")).toObject();
        tool.execute = [this, tool](const QJsonObject &args,
                                    const ToolContext &) -> ToolResult {
          QJsonObject command = args;
          command.insert(QStringLiteral("type"), tool.name);
          QJsonObject patch;
          QString error;
          if (!port.applyCommand(command, QStringLiteral("ai-agent"), &patch,
                                 &error))
            return {false, QString(), QJsonObject(), QJsonObject(), error};
          return {true, QStringLiteral("applied"), patch, QJsonObject(),
                  QString()};
        };
        registry.add(tool);
      }
    }

    ToolResult call(const QString &name, const QJsonObject &args) {
      ToolContext ctx;
      ctx.sessionId = QStringLiteral("s1");
      ctx.agentId = QStringLiteral("ai-agent");
      ctx.callId = name;
      return registry.execute(name, args, ctx);
    }
  };

  static QJsonObject findBlock(const QJsonObject &doc, const QString &id) {
    for (const QJsonValue &value : doc.value(QStringLiteral("blocks")).toArray())
      if (value.toObject().value(QStringLiteral("id")).toString() == id)
        return value.toObject();
    return {};
  }

private slots:
  void builtinSkillCoversAllCommands() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SkillCatalog catalog(dir.path(), dir.path());
    const SkillInfo skill = catalog.get(QStringLiteral("document-editing"));
    QVERIFY(!skill.content.isEmpty());
    for (int i = 0; i <= static_cast<int>(Engine::Command::Type::ApplyJson); ++i) {
      const QString name = Engine::Command::typeName(
          static_cast<Engine::Command::Type>(i));
      QVERIFY2(skill.content.contains(name), qPrintable(name));
    }
  }

  void scenario1_headingAndDivider() {
    Fixture f;
    QVERIFY(f.call(QStringLiteral("setBlockType"),
                   {{QStringLiteral("blockId"), QStringLiteral("b-title")},
                    {QStringLiteral("blockType"), 1},
                    {QStringLiteral("headingLevel"), 1}})
                .ok);
    QVERIFY(f.call(QStringLiteral("setText"),
                   {{QStringLiteral("blockId"), QStringLiteral("b-old")},
                    {QStringLiteral("text"),
                     QStringLiteral("Outcome: shipped successfully")}})
                .ok);
    QVERIFY(f.call(QStringLiteral("insertBlock"),
                   {{QStringLiteral("afterId"), QStringLiteral("b-old")},
                    {QStringLiteral("blockType"), 7},
                    {QStringLiteral("text"), QString()}})
                .ok);
    const QJsonObject doc = f.session.toJson();
    const QJsonObject title = findBlock(doc, QStringLiteral("b-title"));
    QCOMPARE(title.value(QStringLiteral("type")).toString(),
             QStringLiteral("heading"));
    QCOMPARE(title.value(QStringLiteral("headingLevel")).toInt(), 1);
    QCOMPARE(findBlock(doc, QStringLiteral("b-old"))
                 .value(QStringLiteral("text")).toString(),
             QStringLiteral("Outcome: shipped successfully"));
    bool divider = false;
    for (const QJsonValue &value : doc.value(QStringLiteral("blocks")).toArray())
      if (value.toObject().value(QStringLiteral("type")).toString() ==
          QStringLiteral("divider"))
        divider = true;
    QVERIFY(divider);
  }

  void scenario2_todoCheckedAndFormat() {
    Fixture f;
    QVERIFY(f.call(QStringLiteral("insertBlock"),
                   {{QStringLiteral("afterId"), QStringLiteral("b-old")},
                    {QStringLiteral("blockType"), 4},
                    {QStringLiteral("text"),
                     QStringLiteral("Verify the release checklist")}})
                .ok);
    const QJsonObject doc = f.session.toJson();
    QString todoId;
    for (const QJsonValue &value : doc.value(QStringLiteral("blocks")).toArray()) {
      const QJsonObject b = value.toObject();
      if (b.value(QStringLiteral("type")).toString() == QStringLiteral("todo"))
        todoId = b.value(QStringLiteral("id")).toString();
    }
    QVERIFY(!todoId.isEmpty());
    QVERIFY(f.call(QStringLiteral("setChecked"),
                   {{QStringLiteral("blockId"), todoId},
                    {QStringLiteral("checked"), true}})
                .ok);
    QVERIFY(f.call(QStringLiteral("toggleFormat"),
                   {{QStringLiteral("blockId"), QStringLiteral("b-intro")},
                    {QStringLiteral("start"), 0},
                    {QStringLiteral("end"), 12},
                    {QStringLiteral("format"), QStringLiteral("bold")}})
                .ok);
    const QJsonObject intro = findBlock(f.session.toJson(),
                                        QStringLiteral("b-intro"));
    // Formatting is encoded as inline markers by the editor engine.
    QVERIFY(intro.value(QStringLiteral("text")).toString().contains(
        QStringLiteral("**")));
  }

  void scenario3_calloutAndCode() {
    Fixture f;
    QVERIFY(f.call(QStringLiteral("insertBlock"),
                   {{QStringLiteral("afterId"), QStringLiteral("b-old")},
                    {QStringLiteral("blockType"), 10},
                    {QStringLiteral("text"),
                     QStringLiteral("Deadline moved to Friday")}})
                .ok);
    QJsonObject doc = f.session.toJson();
    QString calloutId;
    for (const QJsonValue &value : doc.value(QStringLiteral("blocks")).toArray()) {
      const QJsonObject b = value.toObject();
      if (b.value(QStringLiteral("type")).toString() == QStringLiteral("callout"))
        calloutId = b.value(QStringLiteral("id")).toString();
    }
    QVERIFY(!calloutId.isEmpty());
    QVERIFY(f.call(QStringLiteral("setCalloutType"),
                   {{QStringLiteral("blockId"), calloutId},
                    {QStringLiteral("calloutType"), QStringLiteral("warning")}})
                .ok);
    QVERIFY(f.call(QStringLiteral("insertBlock"),
                   {{QStringLiteral("afterId"), calloutId},
                    {QStringLiteral("blockType"), 6},
                    {QStringLiteral("text"), QStringLiteral("const x = 1;")}})
                .ok);
    doc = f.session.toJson();
    QString codeId;
    for (const QJsonValue &value : doc.value(QStringLiteral("blocks")).toArray()) {
      const QJsonObject b = value.toObject();
      if (b.value(QStringLiteral("type")).toString() == QStringLiteral("code"))
        codeId = b.value(QStringLiteral("id")).toString();
    }
    QVERIFY(!codeId.isEmpty());
    QVERIFY(f.call(QStringLiteral("setCodeLanguage"),
                   {{QStringLiteral("blockId"), codeId},
                    {QStringLiteral("language"), QStringLiteral("javascript")}})
                .ok);
    const QJsonObject metadata =
        findBlock(f.session.toJson(), codeId).value(QStringLiteral("metadata")).toObject();
    QVERIFY(metadata.contains(QStringLiteral("codeLanguage")));
  }

  void scenario4_nestedColumnsViaApplyJson() {
    Fixture f;
    QJsonObject doc = initialDocument();
    QJsonArray blocks = doc.value(QStringLiteral("blocks")).toArray();
    QJsonObject columns;
    columns.insert(QStringLiteral("id"), QStringLiteral("cols"));
    columns.insert(QStringLiteral("type"), QStringLiteral("columns"));
    columns.insert(QStringLiteral("text"), QString());
    QJsonObject left;
    left.insert(QStringLiteral("id"), QStringLiteral("col-left"));
    left.insert(QStringLiteral("parentId"), QStringLiteral("cols"));
    left.insert(QStringLiteral("type"), QStringLiteral("column"));
    left.insert(QStringLiteral("text"), QStringLiteral("Left column"));
    QJsonObject right;
    right.insert(QStringLiteral("id"), QStringLiteral("col-right"));
    right.insert(QStringLiteral("parentId"), QStringLiteral("cols"));
    right.insert(QStringLiteral("type"), QStringLiteral("column"));
    right.insert(QStringLiteral("text"), QStringLiteral("Right column"));
    blocks.append(columns);
    // The document format is flat: children are linked by parentId.
    blocks.append(left);
    blocks.append(right);
    doc.insert(QStringLiteral("blocks"), blocks);
    const QString json = QString::fromUtf8(
        QJsonDocument(doc).toJson(QJsonDocument::Compact));
    QVERIFY(f.call(QStringLiteral("applyJson"),
                   {{QStringLiteral("text"), json}})
                .ok);
    const QJsonObject result = f.session.toJson();
    const QJsonObject cols = findBlock(result, QStringLiteral("cols"));
    QCOMPARE(cols.value(QStringLiteral("type")).toString(),
             QStringLiteral("columns"));
    int childCount = 0;
    for (const QJsonValue &value : result.value(QStringLiteral("blocks")).toArray())
      if (value.toObject().value(QStringLiteral("parentId")).toString() ==
          QStringLiteral("cols"))
        ++childCount;
    QCOMPARE(childCount, 2);
  }
};

QTEST_GUILESS_MAIN(ComplexDocsTests)
#include "tst_complexdocs.moc"
