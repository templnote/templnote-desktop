#include <QtTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QHostAddress>
#include <QTemporaryDir>

#include <memory>

#include "bridge/LiveBridge.h"
#include "control/ControlServer.h"
#include "store/DocumentStore.h"

using namespace Templnote;

class ControlTest : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void contextAndAuthorization();
  void applyCommitsToStore();
  void streamUpdatesLiveAndCommitsOnce();
  void cancelRevertsTheStream();
  void subscribersReceivePatches();
  void streamingStaysBatchedUnderLoad();

private:
  QJsonObject send(const QString &method,
                   const QJsonObject &params = QJsonObject(),
                   const QString &token = QStringLiteral("__default"));
  QJsonArray blocks() const;
  QString textOfFirstBlock() const;

  std::unique_ptr<QTemporaryDir> m_dir;
  QString m_docPath;
  std::unique_ptr<Store::DocumentStore> m_store;
  std::unique_ptr<LiveBridge> m_bridge;
  std::unique_ptr<ControlServer> m_server;
  QString m_token;
  quint64 m_id = 0;
};

QJsonArray ControlTest::blocks() const {
  return m_bridge->session()->toJson().value(QStringLiteral("blocks")).toArray();
}

QString ControlTest::textOfFirstBlock() const {
  const QJsonArray all = blocks();
  if (all.isEmpty())
    return {};
  return all.first().toObject().value(QStringLiteral("text")).toString();
}

void ControlTest::init() {
  m_dir = std::make_unique<QTemporaryDir>();
  QVERIFY(m_dir->isValid());
  m_docPath = m_dir->filePath(QStringLiteral("cat.templnote"));

  const QJsonObject seed{
      {QStringLiteral("format"), QStringLiteral("templnote")},
      {QStringLiteral("version"), 1},
      {QStringLiteral("blocks"), QJsonArray{QJsonObject{
                                     {QStringLiteral("id"), QStringLiteral("b1")},
                                     {QStringLiteral("type"), QStringLiteral("paragraph")},
                                     {QStringLiteral("text"), QString()}}}},
  };
  QFile file(m_docPath);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write(QJsonDocument(seed).toJson(QJsonDocument::Indented));
  file.close();

  m_store = std::make_unique<Store::DocumentStore>(
      m_dir->filePath(QStringLiteral("journal.sqlite")));
  const auto initialized = m_store->initialize(m_docPath, seed);
  QVERIFY2(initialized.ok(), qPrintable(initialized.error()));

  m_bridge = std::make_unique<LiveBridge>();
  m_bridge->setAllowedRoot(m_dir->path());
  m_bridge->setStore(m_store.get());
  m_bridge->setDocumentPath(m_docPath);
  QVERIFY(m_bridge->loadFromDisk());

  QString error;
  m_server = std::make_unique<ControlServer>(
      m_bridge.get(), m_dir->filePath(QStringLiteral("cfg")));
  QVERIFY2(m_server->start(0, &error), qPrintable(error));
  m_token = m_server->token();
  m_id = 0;
}

void ControlTest::cleanup() {
  m_server.reset();
  m_bridge.reset();
  m_store.reset();
}

QJsonObject ControlTest::send(const QString &method, const QJsonObject &params,
                              const QString &token) {
  QTcpSocket socket;
  socket.connectToHost(QHostAddress::LocalHost, m_server->port());
  QElapsedTimer connectTimer;
  connectTimer.start();
  while (socket.state() != QAbstractSocket::ConnectedState &&
         connectTimer.elapsed() < 3000)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  if (socket.state() != QAbstractSocket::ConnectedState)
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("connect failed")}};

  QJsonObject request{
      {QStringLiteral("id"), static_cast<int>(++m_id)},
      {QStringLiteral("method"), method},
      {QStringLiteral("params"), params},
      {QStringLiteral("token"), token == QStringLiteral("__default") ? m_token : token},
  };
  socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
  socket.flush();

  QByteArray buffer;
  QElapsedTimer replyTimer;
  replyTimer.start();
  while (!buffer.contains('\n') && replyTimer.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (socket.bytesAvailable() > 0)
      buffer.append(socket.readAll());
  }
  if (!buffer.contains('\n'))
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("read timeout")}};
  const QByteArray line = buffer.left(buffer.indexOf('\n'));
  QJsonParseError error;
  const QJsonDocument parsed = QJsonDocument::fromJson(line, &error);
  if (error.error != QJsonParseError::NoError)
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("bad reply")}};
  return parsed.object();
}

void ControlTest::contextAndAuthorization() {
  const QJsonObject reply = send(QStringLiteral("context"));
  QVERIFY2(reply.value(QStringLiteral("ok")).toBool(),
           qPrintable(reply.value(QStringLiteral("error")).toString()));
  const QJsonObject result = reply.value(QStringLiteral("result")).toObject();
  QVERIFY(result.value(QStringLiteral("open")).toBool());
  QCOMPARE(result.value(QStringLiteral("path")).toString(), m_docPath);
  QCOMPARE(result.value(QStringLiteral("blocks")).toArray().size(), 1);

  const QJsonObject denied = send(QStringLiteral("context"), QJsonObject(),
                                  QStringLiteral("wrong-token"));
  QVERIFY(!denied.value(QStringLiteral("ok")).toBool());
  QCOMPARE(denied.value(QStringLiteral("error")).toString(),
           QStringLiteral("unauthorized"));
}

void ControlTest::applyCommitsToStore() {
  const int before = m_bridge->loadedRevision();
  const QJsonObject command{
      {QStringLiteral("type"), QStringLiteral("setText")},
      {QStringLiteral("blockId"), QStringLiteral("b1")},
      {QStringLiteral("text"), QStringLiteral("Hello from the harness")},
  };
  const QJsonObject reply = send(QStringLiteral("apply"),
                                 {{QStringLiteral("commands"), QJsonArray{command}}});
  QVERIFY2(reply.value(QStringLiteral("ok")).toBool(),
           qPrintable(reply.value(QStringLiteral("error")).toString()));
  QVERIFY(m_bridge->loadedRevision() > before);
  QVERIFY(textOfFirstBlock().contains(QStringLiteral("Hello from the harness")));

  const auto stored = m_store->load(m_docPath);
  QVERIFY(stored.ok());
  QCOMPARE(stored.value().document.value(QStringLiteral("blocks")).toArray()
               .first().toObject().value(QStringLiteral("text")).toString(),
           QStringLiteral("Hello from the harness"));
}

void ControlTest::streamUpdatesLiveAndCommitsOnce() {
  const QJsonObject start = send(QStringLiteral("stream_start"));
  QVERIFY2(start.value(QStringLiteral("ok")).toBool(),
           qPrintable(start.value(QStringLiteral("error")).toString()));
  const QString blockId = start.value(QStringLiteral("result")).toObject()
                              .value(QStringLiteral("blockId")).toString();
  QVERIFY(!blockId.isEmpty());

  const int before = m_bridge->loadedRevision();
  const QString passage = QStringLiteral("A cat's life is a quiet study in routines. ");
  for (int i = 0; i < 3; ++i) {
    const QJsonObject delta = send(QStringLiteral("stream_delta"),
                                   {{QStringLiteral("text"), passage}});
    QVERIFY(delta.value(QStringLiteral("ok")).toBool());
  }
  QTest::qWait(150); // let the 60 ms batching timer flush into the live view
  QVERIFY(QJsonDocument(m_bridge->session()->toJson())
              .toJson(QJsonDocument::Compact)
              .contains("quiet study"));
  QCOMPARE(m_bridge->loadedRevision(), before); // no commit mid-stream

  const QJsonObject end = send(QStringLiteral("stream_end"));
  QVERIFY2(end.value(QStringLiteral("ok")).toBool(),
           qPrintable(end.value(QStringLiteral("error")).toString()));
  QCOMPARE(m_bridge->loadedRevision(), before + 1);
  QCOMPARE(m_store->load(m_docPath).value().revision, before + 1);

  const QJsonArray all = m_store->load(m_docPath).value().document
                             .value(QStringLiteral("blocks")).toArray();
  bool found = false;
  for (const QJsonValue &value : all) {
    if (value.toObject().value(QStringLiteral("text")).toString().contains(
            QStringLiteral("quiet study")))
      found = true;
  }
  QVERIFY(found);
}

void ControlTest::cancelRevertsTheStream() {
  const QJsonObject start = send(QStringLiteral("stream_start"));
  QVERIFY(start.value(QStringLiteral("ok")).toBool());
  const int before = m_bridge->loadedRevision();
  QVERIFY(send(QStringLiteral("stream_delta"),
               {{QStringLiteral("text"), QStringLiteral("draft text")}})
              .value(QStringLiteral("ok")).toBool());
  QTest::qWait(150);
  QVERIFY(send(QStringLiteral("cancel")).value(QStringLiteral("ok")).toBool());
  QCOMPARE(m_bridge->loadedRevision(), before);
  QVERIFY(!QJsonDocument(m_store->load(m_docPath).value().document)
               .toJson(QJsonDocument::Compact)
               .contains("draft text"));
}

void ControlTest::subscribersReceivePatches() {
  QTcpSocket subscriber;
  subscriber.connectToHost(QHostAddress::LocalHost, m_server->port());
  QElapsedTimer connectTimer;
  connectTimer.start();
  while (subscriber.state() != QAbstractSocket::ConnectedState &&
         connectTimer.elapsed() < 3000)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  QVERIFY(subscriber.state() == QAbstractSocket::ConnectedState);
  const QJsonObject subscribe{
      {QStringLiteral("id"), static_cast<int>(++m_id)},
      {QStringLiteral("method"), QStringLiteral("subscribe")},
      {QStringLiteral("token"), m_token},
  };
  subscriber.write(QJsonDocument(subscribe).toJson(QJsonDocument::Compact) + '\n');
  QVERIFY(subscriber.waitForBytesWritten(3000));

  QJsonObject command{
      {QStringLiteral("type"), QStringLiteral("setText")},
      {QStringLiteral("blockId"), QStringLiteral("b1")},
      {QStringLiteral("text"), QStringLiteral("Subscribed change")},
  };
  QVERIFY(send(QStringLiteral("apply"),
               {{QStringLiteral("commands"), QJsonArray{command}}})
              .value(QStringLiteral("ok")).toBool());

  QByteArray buffer;
  QElapsedTimer eventTimer;
  eventTimer.start();
  auto lineCount = [&buffer]() {
    return static_cast<int>(buffer.count('\n'));
  };
  while (lineCount() < 2 && eventTimer.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (subscriber.bytesAvailable() > 0)
      buffer.append(subscriber.readAll());
  }
  QVERIFY2(lineCount() >= 2, "no patch event received");
  const QByteArray firstLine = buffer.left(buffer.indexOf('\n'));
  buffer.remove(0, firstLine.size() + 1);
  const QByteArray secondLine = buffer.left(buffer.indexOf('\n'));
  const QJsonObject event = QJsonDocument::fromJson(secondLine).object();
  QCOMPARE(event.value(QStringLiteral("event")).toString(),
           QStringLiteral("patch"));
  QVERIFY(event.value(QStringLiteral("patch")).isObject());
}

void ControlTest::streamingStaysBatchedUnderLoad() {
  QVERIFY(send(QStringLiteral("stream_start")).value(QStringLiteral("ok")).toBool());
  const int revisionBefore = m_bridge->loadedRevision();
  const int patchesBefore = m_store->patches(m_docPath).size();

  QElapsedTimer timer;
  timer.start();
  const QString chunk = QStringLiteral("The cat moves through the day with quiet certainty. ");
  for (int i = 0; i < 200; ++i) {
    QVERIFY(send(QStringLiteral("stream_delta"),
                 {{QStringLiteral("text"), chunk}})
                .value(QStringLiteral("ok")).toBool());
  }
  const qint64 elapsed = timer.elapsed();
  QVERIFY(send(QStringLiteral("stream_end")).value(QStringLiteral("ok")).toBool());

  // One transaction for the whole stream, regardless of how many deltas arrived.
  QCOMPARE(m_bridge->loadedRevision(), revisionBefore + 1);
  QCOMPARE(m_store->patches(m_docPath).size(), patchesBefore + 1);
  qInfo() << "streamed 200 deltas in" << elapsed << "ms with one commit";
  QVERIFY2(elapsed < 15000, "streaming throughput regressed");
}

QTEST_MAIN(ControlTest)
#include "tst_control.moc"
