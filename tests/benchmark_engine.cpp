#include "engine/CrdtDocumentState.h"
#include "engine/EditorSession.h"
#include "engine/Types.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QtTest>

using namespace Templnote;
using namespace Templnote::Engine;

class EngineBenchmark : public QObject {
  Q_OBJECT
private slots:
  void benchEngineApply() {
    EditorSession session;
    QJsonObject block;
    block.insert(QStringLiteral("id"), QStringLiteral("b1"));
    block.insert(QStringLiteral("type"), QStringLiteral("paragraph"));
    block.insert(QStringLiteral("text"), QString());
    QJsonArray blocks;
    blocks.append(block);
    QJsonObject doc;
    doc.insert(QStringLiteral("format"), QStringLiteral("templnote"));
    doc.insert(QStringLiteral("version"), 1);
    doc.insert(QStringLiteral("blocks"), blocks);
    session.loadJson(doc);

    QElapsedTimer timer;
    timer.start();
    const int n = 500;
    for (int i = 0; i < n; ++i) {
      auto r = session.apply(Command::insertText(
          QStringLiteral("b1"), i, QStringLiteral("x")));
      if (!r.ok())
        break;
    }
    QTextStream(stdout) << "engine apply x" << n << ": " << timer.elapsed()
                        << " ms (" << QString::number(double(timer.elapsed()) / n, 'f', 3)
                        << " ms/op)\n";
  }

  void benchCrdtRebuild() {
    CrdtDocumentState state;
    QJsonObject block;
    block.insert(QStringLiteral("id"), QStringLiteral("b1"));
    block.insert(QStringLiteral("type"), QStringLiteral("paragraph"));
    block.insert(QStringLiteral("text"), QString());
    QJsonArray blocks;
    blocks.append(block);
    QJsonObject doc;
    doc.insert(QStringLiteral("format"), QStringLiteral("templnote"));
    doc.insert(QStringLiteral("version"), 1);
    doc.insert(QStringLiteral("blocks"), blocks);
    state.load(doc);

    QElapsedTimer timer;
    timer.start();
    const int n = 300;
    for (int i = 0; i < n; ++i) {
      Command cmd = Command::insertText(QStringLiteral("b1"), i,
                                        QStringLiteral("x"));
      cmd.lamport = i + 1;
      state.apply(cmd, nullptr);
    }
    QTextStream(stdout) << "crdt apply x" << n << ": " << timer.elapsed()
                        << " ms (" << QString::number(double(timer.elapsed()) / n, 'f', 3)
                        << " ms/op), opSet=" << state.opSet().size() << "\n";
  }

  void benchCrdtMerge() {
    CrdtDocumentState a;
    CrdtDocumentState b;
    QJsonObject block;
    block.insert(QStringLiteral("id"), QStringLiteral("b1"));
    block.insert(QStringLiteral("type"), QStringLiteral("paragraph"));
    block.insert(QStringLiteral("text"), QString());
    QJsonArray blocks;
    blocks.append(block);
    QJsonObject doc;
    doc.insert(QStringLiteral("format"), QStringLiteral("templnote"));
    doc.insert(QStringLiteral("version"), 1);
    doc.insert(QStringLiteral("blocks"), blocks);
    a.load(doc);
    b.load(doc);

    QVector<Op> ops;
    for (int i = 0; i < 200; ++i) {
      Command cmd = Command::insertText(QStringLiteral("b1"), i,
                                        QStringLiteral("x"));
      cmd.lamport = i + 1;
      cmd.actor = Actor::agent(QStringLiteral("a"));
      QVector<Op> produced;
      a.apply(cmd, &produced);
      ops.append(produced);
    }
    QElapsedTimer timer;
    timer.start();
    b.merge(ops);
    QTextStream(stdout) << "crdt merge 200 ops: " << timer.elapsed() << " ms\n";
  }
};

QTEST_GUILESS_MAIN(EngineBenchmark)
#include "benchmark_engine.moc"
