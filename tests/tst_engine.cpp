#include "engine/EditorSession.h"
#include "engine/Types.h"
#include "engine/Commands.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace Templnote;
using namespace Templnote::Engine;

class EngineTests : public QObject {
  Q_OBJECT

private:
  static QJsonObject emptyDoc() {
    QJsonObject json;
    json.insert(QStringLiteral("format"), QStringLiteral("templnote"));
    json.insert(QStringLiteral("version"), 1);
    QJsonArray blocks;
    QJsonObject block;
    block.insert(QStringLiteral("id"), QStringLiteral("b1"));
    block.insert(QStringLiteral("type"), QStringLiteral("paragraph"));
    block.insert(QStringLiteral("text"), QStringLiteral("Hello world"));
    blocks.append(block);
    json.insert(QStringLiteral("blocks"), blocks);
    return json;
  }

  static EditorSession *sessionWith(const QJsonObject &doc,
                                    QObject *parent = nullptr) {
    auto *s = new EditorSession(parent);
    bool ok = s->loadJson(doc);
    Q_ASSERT(ok);
    Q_UNUSED(ok);
    return s;
  }

private slots:
  void testHeadlessTypesRoundTrip() {
    Actor a = Actor::agent(QStringLiteral("agent-1"), QStringLiteral("Writer"));
    QJsonObject json = a.toJson();
    QCOMPARE(json.value(QStringLiteral("kind")).toString(),
             QStringLiteral("agent"));
    Actor restoredActor = Actor::fromJson(json);
    QCOMPARE(restoredActor.id, a.id);
    QCOMPARE(restoredActor.kind, a.kind);
    QVERIFY(restoredActor.isAgent());

    Command cmd = Command::replaceRange(QStringLiteral("b1"), 0, 5,
                                        QStringLiteral("Goodbye"));
    cmd.actor = a;
    cmd.lamport = 42;
    QJsonObject cmdJson = cmd.toJson();
    QCOMPARE(cmdJson.value(QStringLiteral("type")).toString(),
             QStringLiteral("replaceRange"));
    Command restored = Command::fromJson(cmdJson);
    QCOMPARE(restored.type, cmd.type);
    QCOMPARE(restored.blockId, cmd.blockId);
    QCOMPARE(restored.start, 0);
    QCOMPARE(restored.end, 5);
    QCOMPARE(restored.text, QStringLiteral("Goodbye"));
    QCOMPARE(restored.lamport, quint64(42));
    QCOMPARE(restored.actor.id, a.id);
  }

  void testApplyReplaceRange() {
    EditorSession s;
    QVERIFY(s.loadJson(emptyDoc()));
    const int r0 = s.revision();

    auto result = s.apply(Command::replaceRange(
        QStringLiteral("b1"), 0, 5, QStringLiteral("Goodbye")));
    QVERIFY2(result.ok(), qPrintable(result.error()));
    QCOMPARE(s.revision(), r0 + 1);
    QCOMPARE(s.document().findBlock(QStringLiteral("b1"))->text,
             QStringLiteral("Goodbye world"));
    QCOMPARE(result.value().ops.size(), 1);
    QCOMPARE(result.value().ops.first().type, Op::Type::ReplaceRange);
    QCOMPARE(result.value().actor.kind, Actor::Kind::Human);
    QVERIFY(result.value().timestampMs > 0);
  }

  void testApplyInsertAndDelete() {
    EditorSession s;
    QVERIFY(s.loadJson(emptyDoc()));

    auto ins = s.apply(Command::insertText(QStringLiteral("b1"), 5,
                                           QStringLiteral(" there")));
    QVERIFY(ins.ok());
    QCOMPARE(s.document().findBlock(QStringLiteral("b1"))->text,
             QStringLiteral("Hello there world"));

    auto del = s.apply(Command::deleteRange(QStringLiteral("b1"), 5, 11));
    QVERIFY(del.ok());
    QCOMPARE(s.document().findBlock(QStringLiteral("b1"))->text,
             QStringLiteral("Hello world"));
    QCOMPARE(del.value().ops.first().type, Op::Type::DeleteRange);
    QCOMPARE(del.value().ops.first().payload.toString(),
             QStringLiteral(" there"));
  }

  void testInvalidCommandsRejected() {
    EditorSession s;
    QVERIFY(s.loadJson(emptyDoc()));
    const int r0 = s.revision();

    QVERIFY(!s.canApply(Command::insertText(QStringLiteral("missing"), 0,
                                            QStringLiteral("x"))));
    auto bad = s.apply(Command::deleteRange(QStringLiteral("b1"), 10, 5));
    QVERIFY(!bad.ok());
    auto missing = s.apply(Command::setText(QStringLiteral("nope"),
                                            QStringLiteral("x")));
    QVERIFY(!missing.ok());
    QCOMPARE(s.revision(), r0);
  }

  void testUndoRedo() {
    EditorSession s;
    QVERIFY(s.loadJson(emptyDoc()));
    QVERIFY(!s.canUndo());

    s.apply(Command::setText(QStringLiteral("b1"), QStringLiteral("First")));
    QVERIFY(s.canUndo());
    QCOMPARE(s.document().findBlock(QStringLiteral("b1"))->text,
             QStringLiteral("First"));

    s.apply(Command::setText(QStringLiteral("b1"), QStringLiteral("Second")));
    QVERIFY(s.canUndo());

    QVERIFY(s.undo());
    QCOMPARE(s.document().findBlock(QStringLiteral("b1"))->text,
             QStringLiteral("First"));
    QVERIFY(s.undo());
    QCOMPARE(s.document().findBlock(QStringLiteral("b1"))->text,
             QStringLiteral("Hello world"));
    QVERIFY(!s.canUndo());

    QVERIFY(s.redo());
    QCOMPARE(s.document().findBlock(QStringLiteral("b1"))->text,
             QStringLiteral("First"));
    QVERIFY(s.redo());
    QCOMPARE(s.document().findBlock(QStringLiteral("b1"))->text,
             QStringLiteral("Second"));
    QVERIFY(!s.canRedo());
  }

  void testBlockCommands() {
    EditorSession s;
    QVERIFY(s.loadJson(emptyDoc()));

    Command invalidSplit;
    invalidSplit.type = Command::Type::SplitBlock;
    auto split = s.apply(invalidSplit);
    QVERIFY(!split.ok()); // no blockId -> rejected

    Command splitCmd;
    splitCmd.type = Command::Type::SplitBlock;
    splitCmd.blockId = QStringLiteral("b1");
    splitCmd.position = 5;
    auto sr = s.apply(splitCmd);
    QVERIFY2(sr.ok(), qPrintable(sr.error()));
    QCOMPARE(s.document().count(), 2);
    QCOMPARE(s.document().findBlock(QStringLiteral("b1"))->text,
             QStringLiteral("Hello"));
    const QString newId = sr.value().ops.first().blockId;
    QVERIFY(!newId.isEmpty());
    QCOMPARE(s.document().findBlock(newId)->text, QStringLiteral(" world"));

    auto changeType = s.apply(Command::setBlockType(
        newId, static_cast<int>(BlockType::Heading), 2));
    QVERIFY(changeType.ok());
    QCOMPARE(s.document().findBlock(newId)->type, BlockType::Heading);
    QCOMPARE(s.document().findBlock(newId)->headingLevel, 2);
  }

  void testFormatToggle() {
    using Commands::toggleFormat;
    auto bold = toggleFormat(QStringLiteral("hello world"), 0, 5,
                             QStringLiteral("bold"));
    QVERIFY(bold.ok());
    QCOMPARE(bold.text, QStringLiteral("**hello** world"));

    auto unbold = toggleFormat(bold.text, 0, 9, QStringLiteral("bold"));
    QVERIFY(unbold.ok());
    QCOMPARE(unbold.text, QStringLiteral("hello world"));

    auto sub = toggleFormat(QStringLiteral("abc"), 1, 1,
                            QStringLiteral("subscript"));
    QVERIFY(sub.ok());
    QCOMPARE(sub.text, QStringLiteral("a<sub></sub>bc"));
    QCOMPARE(sub.start, 6);
  }

  void testPatchRoundTrip() {
    EditorSession s;
    QVERIFY(s.loadJson(emptyDoc()));
    auto r = s.apply(Command::replaceRange(QStringLiteral("b1"), 0, 5,
                                           QStringLiteral("Goodbye")));
    QVERIFY(r.ok());
    const QJsonObject json = r.value().toJson();
    const Patch restored = Patch::fromJson(json);
    QCOMPARE(restored.revision, r.value().revision);
    QCOMPARE(restored.ops.size(), r.value().ops.size());
    QCOMPARE(restored.ops.first().text, QStringLiteral("Goodbye"));
    QCOMPARE(restored.actor.id, r.value().actor.id);
  }

  void testRemotePatchReplay() {
    EditorSession a;
    EditorSession b;
    QVERIFY(a.loadJson(emptyDoc()));
    QVERIFY(b.loadJson(emptyDoc()));

    auto r = a.apply(Command::replaceRange(QStringLiteral("b1"), 0, 5,
                                           QStringLiteral("Goodbye")));
    QVERIFY(r.ok());
    auto remote = b.applyRemote(r.value());
    QVERIFY2(remote.ok(), qPrintable(remote.error()));
    QCOMPARE(b.document().findBlock(QStringLiteral("b1"))->text,
             a.document().findBlock(QStringLiteral("b1"))->text);
  }

  void testActorStamping() {
    EditorSession s;
    QVERIFY(s.loadJson(emptyDoc()));
    Command cmd = Command::setText(QStringLiteral("b1"),
                                   QStringLiteral("AI wrote this"));
    cmd.actor = Actor::agent(QStringLiteral("writer-agent"));
    auto r = s.apply(cmd);
    QVERIFY(r.ok());
    QCOMPARE(r.value().actor.kind, Actor::Kind::Agent);
    QCOMPARE(r.value().actor.id, QStringLiteral("writer-agent"));
    QVERIFY(r.value().lamport >= 1);
  }
};

QTEST_GUILESS_MAIN(EngineTests)
#include "tst_engine.moc"
