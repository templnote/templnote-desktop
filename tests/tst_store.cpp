#include "store/DocumentStore.h"
#include "engine/EditorSession.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace Templnote;
using namespace Templnote::Engine;
using namespace Templnote::Store;

class StoreTests : public QObject {
  Q_OBJECT

private:
  static QJsonObject sampleDoc(const QString &text = QStringLiteral("Hello")) {
    QJsonObject block;
    block.insert(QStringLiteral("id"), QStringLiteral("b1"));
    block.insert(QStringLiteral("type"), QStringLiteral("paragraph"));
    block.insert(QStringLiteral("text"), text);
    QJsonArray blocks;
    blocks.append(block);
    QJsonObject doc;
    doc.insert(QStringLiteral("format"), QStringLiteral("templnote"));
    doc.insert(QStringLiteral("version"), 1);
    doc.insert(QStringLiteral("blocks"), blocks);
    return doc;
  }

private slots:
  void testLoadAndInitialize() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("doc.templnote"));

    EditorSession s;
    QVERIFY(s.loadJson(sampleDoc()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(s.documentJson().toUtf8());
    f.close();

    DocumentStore store(dir.filePath(QStringLiteral("journal.sqlite")));
    auto loaded = store.load(path);
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.value().revision, 0);

    auto init = store.initialize(path, s.toJson());
    QVERIFY2(init.ok(), qPrintable(init.error()));
    QCOMPARE(init.value().revision, 1);
    QCOMPARE(store.currentRevision(path), 1);

    auto after = store.load(path);
    QVERIFY(after.ok());
    QCOMPARE(after.value().revision, 1);
  }

  void testCommitAndCASConflict() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("doc.templnote"));

    EditorSession s;
    QVERIFY(s.loadJson(sampleDoc()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(s.documentJson().toUtf8());
    f.close();

    DocumentStore store(dir.filePath(QStringLiteral("journal.sqlite")));
    store.initialize(path, s.toJson()); // revision 1

    auto r = s.apply(Command::setText(QStringLiteral("b1"),
                                      QStringLiteral("AI edit")));
    QVERIFY(r.ok());

    auto commit = store.commit(path, 1, s.toJson(), r.value(), s.lamport(),
                               QStringLiteral("agent-write"));
    QVERIFY2(commit.ok(), qPrintable(commit.error()));
    QCOMPARE(commit.value().revision, 2);
    QCOMPARE(store.currentRevision(path), 2);

    // Stale writer using revision 1 must be rejected.
    auto conflict = store.commit(path, 1, sampleDoc(QStringLiteral("stale")),
                                 r.value(), s.lamport());
    QVERIFY(!conflict.ok());
    QCOMPARE(conflict.error(), QStringLiteral("conflict"));
    QCOMPARE(store.currentRevision(path), 2);

    // On-disk content must be the committed version, not the stale one.
    auto loaded = store.load(path);
    QVERIFY(loaded.ok());
    const QJsonArray blocks = loaded.value().document
                                  .value(QStringLiteral("blocks")).toArray();
    QCOMPARE(blocks.first().toObject().value(QStringLiteral("text")).toString(),
             QStringLiteral("AI edit"));
  }

  void testJournalAndSnapshots() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("doc.templnote"));
    const QString journal = dir.filePath(QStringLiteral("journal.sqlite"));

    EditorSession s;
    QVERIFY(s.loadJson(sampleDoc()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(s.documentJson().toUtf8());
    f.close();

    DocumentStore store(journal);
    store.initialize(path, s.toJson());

    auto r1 = s.apply(Command::setText(QStringLiteral("b1"),
                                       QStringLiteral("v2")));
    QVERIFY(r1.ok());
    QVERIFY(store.commit(path, 1, s.toJson(), r1.value(), s.lamport(),
                         QStringLiteral("writer-agent")).ok());

    auto r2 = s.apply(Command::setText(QStringLiteral("b1"),
                                       QStringLiteral("v3")));
    QVERIFY(r2.ok());
    QVERIFY(store.commit(path, 2, s.toJson(), r2.value(), s.lamport(),
                         QStringLiteral("grammar-check")).ok());

    const QVector<SnapshotInfo> versions = store.versions(path);
    QCOMPARE(versions.size(), 3);
    QCOMPARE(versions.first().revision, 1);
    QCOMPARE(versions.last().revision, 3);
    QCOMPARE(versions.last().label, QStringLiteral("grammar-check"));
    QCOMPARE(versions.last().actor.kind, Actor::Kind::Human);

    const QVector<DocumentStore::PatchEntry> history = store.patches(path);
    QCOMPARE(history.size(), 3);
    QCOMPARE(history.last().patch.ops.size(), 1);

    // Restore revision 1 -> new revision 4 with v1 content.
    auto restored = store.restore(path, versions.first().id);
    QVERIFY2(restored.ok(), qPrintable(restored.error()));
    QCOMPARE(restored.value().revision, 4);
    const auto doc = store.load(path);
    QVERIFY(doc.ok());
    const QJsonArray blocks = doc.value().document
                                  .value(QStringLiteral("blocks")).toArray();
    QCOMPARE(blocks.first().toObject().value(QStringLiteral("text")).toString(),
             QStringLiteral("Hello"));
  }

  void testAgentProvenance() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("doc.templnote"));

    EditorSession s;
    QVERIFY(s.loadJson(sampleDoc()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(s.documentJson().toUtf8());
    f.close();

    DocumentStore store(dir.filePath(QStringLiteral("journal.sqlite")));
    store.initialize(path, s.toJson());

    Command cmd = Command::setText(QStringLiteral("b1"),
                                   QStringLiteral("written by AI"));
    cmd.actor = Actor::pipeline(QStringLiteral("pipeline-1"),
                                QStringLiteral("Write"));
    auto r = s.apply(cmd);
    QVERIFY(r.ok());
    QVERIFY(store.commit(path, 1, s.toJson(), r.value(), s.lamport(),
                         QStringLiteral("write-stage")).ok());

    const auto history = store.patches(path);
    QCOMPARE(history.size(), 2);
    QCOMPARE(history.last().patch.actor.kind, Actor::Kind::Pipeline);
    QCOMPARE(history.last().patch.actor.displayName, QStringLiteral("Write"));
  }
};

QTEST_GUILESS_MAIN(StoreTests)
#include "tst_store.moc"
