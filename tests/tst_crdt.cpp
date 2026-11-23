#include "engine/CrdtDocumentState.h"
#include "engine/EditorSession.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace Templnote;
using namespace Templnote::Engine;

class CrdtTests : public QObject {
  Q_OBJECT

private:
  static QJsonObject doc(const QString &text = QStringLiteral("Hello")) {
    QJsonObject block;
    block.insert(QStringLiteral("id"), QStringLiteral("b1"));
    block.insert(QStringLiteral("type"), QStringLiteral("paragraph"));
    block.insert(QStringLiteral("text"), text);
    QJsonArray blocks;
    blocks.append(block);
    QJsonObject d;
    d.insert(QStringLiteral("format"), QStringLiteral("templnote"));
    d.insert(QStringLiteral("version"), 1);
    d.insert(QStringLiteral("blocks"), blocks);
    return d;
  }

private slots:
  void testConvergentMerge() {
    CrdtDocumentState a;
    CrdtDocumentState b;
    QVERIFY(a.load(doc()));
    QVERIFY(b.load(doc()));

    Command ca = Command::setText(QStringLiteral("b1"),
                                  QStringLiteral("Alice edit"));
    ca.actor = Actor::human(QStringLiteral("alice"));
    ca.lamport = 2;
    QVector<Op> opsA;
    QVERIFY(a.apply(ca, &opsA).isEmpty());

    Command cb = Command::setText(QStringLiteral("b1"),
                                  QStringLiteral("Bob edit"));
    cb.actor = Actor::human(QStringLiteral("bob"));
    cb.lamport = 2;
    QVector<Op> opsB;
    QVERIFY(b.apply(cb, &opsB).isEmpty());

    // Each replica sees the other's ops, in opposite orders.
    QVERIFY(a.merge(opsB) >= 0);
    QVERIFY(b.merge(opsA) >= 0);

    QCOMPARE(a.document().findBlock(QStringLiteral("b1"))->text,
             b.document().findBlock(QStringLiteral("b1"))->text);
    // "bob" > "alice" lexicographically -> Bob wins on both replicas.
    QCOMPARE(a.document().findBlock(QStringLiteral("b1"))->text,
             QStringLiteral("Bob edit"));
  }

  void testMergeIdempotent() {
    CrdtDocumentState a;
    CrdtDocumentState b;
    QVERIFY(a.load(doc()));
    QVERIFY(b.load(doc()));
    Command cmd = Command::setText(QStringLiteral("b1"),
                                   QStringLiteral("shared"));
    cmd.actor = Actor::agent(QStringLiteral("writer"));
    cmd.lamport = 3;
    QVector<Op> ops;
    QVERIFY(a.apply(cmd, &ops).isEmpty());
    QCOMPARE(a.merge(ops), 0); // already known
    const int added = b.merge(ops);
    QVERIFY(added > 0);
    QCOMPARE(b.merge(ops), 0); // second time is a no-op
    QCOMPARE(b.document().findBlock(QStringLiteral("b1"))->text,
             QStringLiteral("shared"));
  }

  void testDifferentBlocksBothSurvive() {
    CrdtDocumentState a;
    CrdtDocumentState b;
    QVERIFY(a.load(doc()));
    QVERIFY(b.load(doc()));

    auto insert = [](CrdtDocumentState &state, const QString &actorId,
                     const QString &text) {
      Command cmd = Command::insertBlock(QStringLiteral("b1"),
                                         static_cast<int>(BlockType::Paragraph),
                                         text);
      cmd.actor = Actor::human(actorId);
      cmd.lamport = 4;
      QVector<Op> ops;
      state.apply(cmd, &ops);
      return ops;
    };
    const QVector<Op> opsA = insert(a, QStringLiteral("alice"),
                                    QStringLiteral("from Alice"));
    const QVector<Op> opsB = insert(b, QStringLiteral("bob"),
                                    QStringLiteral("from Bob"));

    a.merge(opsB);
    b.merge(opsA);
    QCOMPARE(a.document().count(), b.document().count());
    QCOMPARE(a.document().count(), 3);
    QCOMPARE(a.serialize(), b.serialize());

    bool hasAlice = false, hasBob = false;
    for (int i = 0; i < a.document().count(); ++i) {
      const QString text = a.document().blockAt(i)->text;
      if (text == QLatin1String("from Alice")) hasAlice = true;
      if (text == QLatin1String("from Bob")) hasBob = true;
    }
    QVERIFY(hasAlice);
    QVERIFY(hasBob);
  }

  void testSessionWithCrdtBackend() {
    EditorSession session;
    auto *state = new CrdtDocumentState();
    QVERIFY(state->load(doc()));
    session.setDocumentState(state);
    auto r = session.apply(Command::setText(QStringLiteral("b1"),
                                            QStringLiteral("via CRDT")));
    QVERIFY(r.ok());
    QCOMPARE(session.document().findBlock(QStringLiteral("b1"))->text,
             QStringLiteral("via CRDT"));
    QVERIFY(session.undo());
    QCOMPARE(session.document().findBlock(QStringLiteral("b1"))->text,
             QStringLiteral("Hello"));
  }
};

QTEST_GUILESS_MAIN(CrdtTests)
#include "tst_crdt.moc"
