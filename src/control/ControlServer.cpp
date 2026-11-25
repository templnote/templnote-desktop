#include "ControlServer.h"

#include "engine/Types.h"

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QTcpSocket>
#include <QUuid>

namespace Templnote {

ControlServer::ControlServer(LiveBridge *bridge, const QString &configDir,
                             QObject *parent)
    : QObject(parent),
      m_bridge(bridge),
      m_configDir(configDir),
      m_server(new QTcpServer(this)) {
  m_flushTimer.setInterval(kFlushIntervalMs);
  m_flushTimer.setSingleShot(true);
  connect(&m_flushTimer, &QTimer::timeout, this, &ControlServer::flushStream);
  connect(m_server, &QTcpServer::newConnection, this,
          &ControlServer::onNewConnection);
  connect(m_bridge->session(), &Engine::EditorSession::patchApplied, this,
          [this](const Engine::Patch &patch) {
            publishPatch(patch.toJson());
          });
}

ControlServer::~ControlServer() { stop(); }

bool ControlServer::start(quint16 port, QString *error) {
  if (m_server->isListening())
    return true;
  if (!m_server->listen(QHostAddress::LocalHost, port)) {
    if (error)
      *error = m_server->errorString();
    return false;
  }
  m_token = QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (!m_configDir.isEmpty()) {
    QDir().mkpath(m_configDir);
    m_endpointPath = QDir(m_configDir).filePath(QStringLiteral("ai-control.json"));
    QFile file(m_endpointPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
      const QJsonObject descriptor{
          {QStringLiteral("port"), static_cast<int>(m_server->serverPort())},
          {QStringLiteral("token"), m_token},
          {QStringLiteral("host"), QStringLiteral("127.0.0.1")},
      };
      file.write(QJsonDocument(descriptor).toJson(QJsonDocument::Indented));
      file.flush();
    } else {
      m_endpointPath.clear();
    }
  }
  return true;
}

void ControlServer::stop() {
  m_flushTimer.stop();
  for (QTcpSocket *socket : m_sockets) {
    socket->disconnect(this);
    socket->disconnectFromHost();
  }
  m_sockets.clear();
  m_subscribers.clear();
  m_buffers.clear();
  m_server->close();
}

void ControlServer::onNewConnection() {
  while (m_server->hasPendingConnections()) {
    auto *socket = m_server->nextPendingConnection();
    socket->setParent(this);
    m_sockets.insert(socket);
    connect(socket, &QTcpSocket::readyRead, this,
            &ControlServer::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this,
            &ControlServer::onSocketDisconnected);
  }
}

void ControlServer::onSocketDisconnected() {
  auto *socket = qobject_cast<QTcpSocket *>(sender());
  if (!socket)
    return;
  m_sockets.remove(socket);
  m_subscribers.remove(socket);
  m_buffers.remove(socket);
  socket->deleteLater();
}

void ControlServer::onReadyRead() {
  auto *socket = qobject_cast<QTcpSocket *>(sender());
  if (!socket)
    return;
  QByteArray &buffer = m_buffers[socket];
  buffer.append(socket->readAll());

  while (true) {
    const int newline = buffer.indexOf('\n');
    if (newline < 0)
      break;
    QByteArray line = buffer.left(newline);
    buffer.remove(0, newline + 1);
    if (line.size() > kMaxLineBytes) {
      send(socket, {{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QStringLiteral("request too large")}});
      socket->disconnectFromHost();
      return;
    }
    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
      send(socket, {{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QStringLiteral("invalid JSON")}});
      continue;
    }
    send(socket, handle(socket, parsed.object()));
  }
}

void ControlServer::send(QTcpSocket *socket, const QJsonObject &message) {
  if (!socket || socket->state() != QAbstractSocket::ConnectedState)
    return;
  socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
  socket->flush();
}

void ControlServer::publishPatch(const QJsonObject &patch) {
  if (m_subscribers.isEmpty())
    return;
  const QJsonObject event{{QStringLiteral("event"), QStringLiteral("patch")},
                          {QStringLiteral("patch"), patch}};
  for (QTcpSocket *socket : m_subscribers)
    send(socket, event);
}

bool ControlServer::authorized(const QJsonObject &request) const {
  return !m_token.isEmpty() &&
         request.value(QStringLiteral("token")).toString() == m_token;
}

QJsonObject ControlServer::agentActor() const {
  return {{QStringLiteral("id"), QStringLiteral("mini-harness")},
          {QStringLiteral("kind"), QStringLiteral("agent")},
          {QStringLiteral("displayName"), QStringLiteral("Harness Agent")}};
}

QJsonObject ControlServer::context() const {
  const QJsonObject document = m_bridge->session()->toJson();
  return {{QStringLiteral("open"), !m_bridge->documentPath().isEmpty()},
          {QStringLiteral("path"), m_bridge->documentPath()},
          {QStringLiteral("revision"), m_bridge->loadedRevision()},
          {QStringLiteral("blocks"), document.value(QStringLiteral("blocks"))},
          {QStringLiteral("autoCommit"), m_bridge->autoCommit()},
          {QStringLiteral("streaming"), m_streaming}};
}

QString ControlServer::lastBlockId() const {
  const QJsonArray blocks =
      m_bridge->session()->toJson().value(QStringLiteral("blocks")).toArray();
  if (blocks.isEmpty())
    return {};
  return blocks.last().toObject().value(QStringLiteral("id")).toString();
}

bool ControlServer::applyCommands(const QJsonArray &commands,
                                  const QString &label, QString *error) {
  if (commands.isEmpty()) {
    if (error)
      *error = QStringLiteral("no commands");
    return false;
  }
  const bool previous = m_bridge->autoCommit();
  m_bridge->setAutoCommit(false);
  for (const QJsonValue &value : commands) {
    if (!value.isObject()) {
      m_bridge->cancelPending();
      m_bridge->setAutoCommit(previous);
      if (error)
        *error = QStringLiteral("command must be an object");
      return false;
    }
    QJsonObject command = value.toObject();
    if (!command.contains(QStringLiteral("actor")))
      command.insert(QStringLiteral("actor"), agentActor());
    if (!m_bridge->applyCommand(command)) {
      m_bridge->cancelPending();
      m_bridge->setAutoCommit(previous);
      if (error)
        *error = QStringLiteral("command rejected");
      return false;
    }
  }
  if (previous && !m_bridge->commitPending(label)) {
    m_bridge->setAutoCommit(previous);
    if (error)
      *error = QStringLiteral("commit failed");
    return false;
  }
  m_bridge->setAutoCommit(previous);
  return true;
}

bool ControlServer::syncViewToDisk(QString *error) {
  if (auto *model = m_bridge->blockModel()) {
    if (model->isModified() && !model->saveCurrentDocument()) {
      if (error)
        *error = QStringLiteral("could not save pending editor changes");
      return false;
    }
  }
  if (!m_bridge->loadFromDisk()) {
    if (error)
      *error = QStringLiteral("could not refresh the live session");
    return false;
  }
  return true;
}

bool ControlServer::flushStream() {
  if (!m_streaming || m_streamBuffer.isEmpty())
    return true;
  const QString text = m_streamBuffer;
  m_streamBuffer.clear();
  const QJsonObject command{
      {QStringLiteral("type"), QStringLiteral("insertText")},
      {QStringLiteral("blockId"), m_streamBlockId},
      {QStringLiteral("position"), m_streamPosition},
      {QStringLiteral("text"), text},
      {QStringLiteral("actor"), agentActor()},
  };
  if (!m_bridge->applyCommand(command)) {
    m_flushTimer.stop();
    emit errorOccurred(QStringLiteral("stream insert failed"));
    return false;
  }
  m_streamPosition += text.size();
  return true;
}

QJsonObject ControlServer::handle(QTcpSocket *socket,
                                  const QJsonObject &request) {
  const QJsonValue id = request.value(QStringLiteral("id"));
  auto reply = [&id](bool ok, const QJsonObject &result, const QString &error) {
    QJsonObject response{{QStringLiteral("id"), id},
                         {QStringLiteral("ok"), ok}};
    if (ok)
      response.insert(QStringLiteral("result"), result);
    else
      response.insert(QStringLiteral("error"), error);
    return response;
  };

  if (!authorized(request))
    return reply(false, {}, QStringLiteral("unauthorized"));

  const QString method = request.value(QStringLiteral("method")).toString();
  const QJsonObject params = request.value(QStringLiteral("params")).toObject();

  const bool mutating = method == QStringLiteral("apply") ||
                        method == QStringLiteral("cancel") ||
                        method.startsWith(QStringLiteral("stream_"));
  if (mutating && m_bridge->documentPath().isEmpty())
    return reply(false, {}, QStringLiteral("no document is open"));

  if (method == QStringLiteral("ping"))
    return reply(true, {{QStringLiteral("pong"), true}}, {});

  if (method == QStringLiteral("context"))
    return reply(true, context(), {});

  if (method == QStringLiteral("subscribe")) {
    m_subscribers.insert(socket);
    return reply(true, {{QStringLiteral("subscribed"), true}}, {});
  }

  if (method == QStringLiteral("apply")) {
    if (m_streaming)
      return reply(false, {}, QStringLiteral("a stream is in progress"));
    QString error;
    if (!syncViewToDisk(&error))
      return reply(false, {}, error);
    if (!applyCommands(params.value(QStringLiteral("commands")).toArray(),
                       QStringLiteral("harness-apply"), &error))
      return reply(false, {}, error);
    return reply(true,
                 {{QStringLiteral("revision"), m_bridge->loadedRevision()}}, {});
  }

  if (method == QStringLiteral("stream_start")) {
    if (m_streaming)
      m_bridge->cancelPending();
    m_flushTimer.stop();
    m_streamBuffer.clear();
    QString error;
    if (!syncViewToDisk(&error))
      return reply(false, {}, error);
    const QString afterId = lastBlockId();
    const QJsonObject command{
        {QStringLiteral("type"), QStringLiteral("insertBlock")},
        {QStringLiteral("afterId"), afterId},
        {QStringLiteral("blockType"), 0},
        {QStringLiteral("text"), QString()},
        {QStringLiteral("actor"), agentActor()},
    };
    const bool previous = m_bridge->autoCommit();
    m_bridge->setAutoCommit(false);
    if (!m_bridge->applyCommand(command)) {
      m_bridge->setAutoCommit(previous);
      return reply(false, {}, QStringLiteral("could not create stream block"));
    }
    m_streamBlockId = lastBlockId();
    m_streamPosition = 0;
    m_streaming = true;
    return reply(true,
                 {{QStringLiteral("blockId"), m_streamBlockId},
                  {QStringLiteral("revision"), m_bridge->loadedRevision()}},
                 {});
  }

  if (method == QStringLiteral("stream_delta")) {
    if (!m_streaming)
      return reply(false, {}, QStringLiteral("no active stream"));
    const QString text = params.value(QStringLiteral("text")).toString();
    if (!text.isEmpty()) {
      m_streamBuffer.append(text);
      if (m_streamBuffer.size() >= kMaxStreamBufferBytes ||
          m_streamBuffer.size() >= kImmediateFlushBytes) {
        if (!flushStream())
          return reply(false, {}, QStringLiteral("stream insert failed"));
      } else if (!m_flushTimer.isActive()) {
        m_flushTimer.start();
      }
    }
    return reply(true, {{QStringLiteral("buffered"), m_streamBuffer.size()}}, {});
  }

  if (method == QStringLiteral("stream_end")) {
    if (!m_streaming)
      return reply(false, {}, QStringLiteral("no active stream"));
    m_flushTimer.stop();
    if (!flushStream()) {
      m_streaming = false;
      m_bridge->setAutoCommit(true);
      return reply(false, {}, QStringLiteral("stream insert failed"));
    }
    m_streaming = false;
    const bool committed = m_bridge->commitPending(QStringLiteral("harness-stream"));
    m_bridge->setAutoCommit(true);
    if (!committed)
      return reply(false, {}, QStringLiteral("commit failed"));
    return reply(true,
                 {{QStringLiteral("revision"), m_bridge->loadedRevision()},
                  {QStringLiteral("blockId"), m_streamBlockId}},
                 {});
  }

  if (method == QStringLiteral("cancel")) {
    m_flushTimer.stop();
    m_streamBuffer.clear();
    m_streaming = false;
    if (!m_bridge->cancelPending())
      return reply(false, {}, QStringLiteral("could not revert stream"));
    return reply(true, {{QStringLiteral("revision"), m_bridge->loadedRevision()}}, {});
  }

  return reply(false, {}, QStringLiteral("unknown method: ") + method);
}

} // namespace Templnote
