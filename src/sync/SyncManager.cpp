#include "SyncManager.h"

#include "engine/CrdtDocumentState.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>

namespace Templnote::Sync {

namespace {
constexpr int kHeaderSize = 4;
}

SyncManager::SyncManager(int port, QObject *parent)
    : QObject(parent), m_port(port) {}

SyncManager::~SyncManager() { stop(); }

bool SyncManager::running() const { return m_server && m_server->isListening(); }

int SyncManager::peerCount() const { return m_peers.size(); }

void SyncManager::setState(Engine::CrdtDocumentState *state) { m_state = state; }

bool SyncManager::startServer() {
  if (!m_server) {
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this,
            &SyncManager::onNewConnection);
  }
  if (m_server->isListening())
    return true;
  const QHostAddress address =
      m_host.isEmpty() ? QHostAddress::LocalHost : QHostAddress(m_host);
  if (address.isNull() || !m_server->listen(address, m_port)) {
    emit syncError(m_server->errorString());
    return false;
  }
  emit runningChanged();
  return true;
}

void SyncManager::stop() {
  if (m_server) {
    m_server->close();
    delete m_server;
    m_server = nullptr;
    emit runningChanged();
  }
  for (QTcpSocket *socket : m_peers)
    socket->disconnectFromHost();
  m_peers.clear();
  m_authenticated.clear();
  m_buffers.clear();
  emit peersChanged();
}

void SyncManager::connectToPeer(const QString &host, int port) {
  auto *socket = new QTcpSocket(this);
  connect(socket, &QTcpSocket::readyRead, this, &SyncManager::onReadyRead);
  connect(socket, &QTcpSocket::disconnected, this,
          &SyncManager::onPeerDisconnected);
  connect(socket, &QTcpSocket::connected, this, &SyncManager::onPeerConnected);
  socket->connectToHost(host, port > 0 ? port : m_port);
  m_peers.append(socket);
  m_buffers.insert(socket, QByteArray());
  emit peersChanged();
}

void SyncManager::onPeerConnected() {
  auto *socket = qobject_cast<QTcpSocket *>(sender());
  if (!socket)
    return;
  // Authenticate before sending any document data.
  QJsonObject auth;
  auth.insert(QStringLiteral("type"), QStringLiteral("auth"));
  auth.insert(QStringLiteral("token"), m_authToken);
  sendFrame(socket, QJsonDocument(auth).toJson(QJsonDocument::Compact));
  m_authenticated.insert(socket);
}

void SyncManager::onNewConnection() {
  while (m_server->hasPendingConnections()) {
    QTcpSocket *socket = m_server->nextPendingConnection();
    connect(socket, &QTcpSocket::readyRead, this, &SyncManager::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this,
            &SyncManager::onPeerDisconnected);
    m_peers.append(socket);
    m_buffers.insert(socket, QByteArray());
    // No ops are sent until the peer authenticates.
  }
  emit peersChanged();
}

void SyncManager::onPeerDisconnected() {
  auto *socket = qobject_cast<QTcpSocket *>(sender());
  if (!socket)
    return;
  m_peers.removeAll(socket);
  m_authenticated.remove(socket);
  m_buffers.remove(socket);
  socket->deleteLater();
  emit peersChanged();
}

void SyncManager::sendFrame(QTcpSocket *socket, const QByteArray &payload) {
  if (!socket || socket->state() != QAbstractSocket::ConnectedState)
    return;
  QByteArray frame;
  frame.resize(kHeaderSize);
  const quint32 size = static_cast<quint32>(payload.size());
  for (int i = 0; i < kHeaderSize; ++i)
    frame[i] = static_cast<char>((size >> (8 * (kHeaderSize - 1 - i))) & 0xff);
  frame.append(payload);
  socket->write(frame);
  socket->flush();
}

void SyncManager::sendOps(QTcpSocket *socket) {
  if (!m_state || !socket || !m_authenticated.contains(socket) ||
      socket->state() != QAbstractSocket::ConnectedState)
    return;
  QJsonArray ops;
  for (const Engine::Op &op : m_state->opSet())
    ops.append(op.toJson());
  QJsonObject message;
  message.insert(QStringLiteral("type"), QStringLiteral("ops"));
  message.insert(QStringLiteral("ops"), ops);
  sendFrame(socket, QJsonDocument(message).toJson(QJsonDocument::Compact));
}

void SyncManager::broadcast() {
  for (QTcpSocket *socket : m_peers)
    sendOps(socket);
}

void SyncManager::onReadyRead() {
  auto *socket = qobject_cast<QTcpSocket *>(sender());
  if (!socket)
    return;
  QByteArray &buffer = m_buffers[socket];
  buffer.append(socket->readAll());
  while (buffer.size() >= static_cast<int>(kHeaderSize)) {
    quint32 size = 0;
    for (int i = 0; i < kHeaderSize; ++i)
      size = (size << 8) | static_cast<quint8>(buffer.at(i));
    if (size > kMaxFrameSize) {
      emit syncError(QStringLiteral("Oversized sync frame"));
      socket->disconnectFromHost();
      return;
    }
    if (static_cast<quint32>(buffer.size()) < kHeaderSize + size)
      break;
    const QByteArray payload = buffer.mid(kHeaderSize, size);
    buffer.remove(0, kHeaderSize + size);

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
      continue;
    const QJsonObject message = doc.object();
    if (message.value(QStringLiteral("type")).toString() == QLatin1String("auth")) {
      // The first frame is the token. Peers with the wrong token are dropped
      // before any document data is exchanged.
      const bool same = message.value(QStringLiteral("token")).toString() ==
                        m_authToken;
      if (!same) {
        emit syncError(QStringLiteral("Sync peer failed authentication"));
        socket->disconnectFromHost();
        return;
      }
      if (!m_authenticated.contains(socket)) {
        m_authenticated.insert(socket);
        sendOps(socket);
      }
      continue;
    }
    if (!m_authenticated.contains(socket)) {
      emit syncError(QStringLiteral("Unauthenticated sync frame ignored"));
      socket->disconnectFromHost();
      return;
    }
    processMessage(payload);
  }
}

void SyncManager::processMessage(const QByteArray &payload) {
  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject())
    return;
  const QJsonObject message = doc.object();
  if (message.value(QStringLiteral("type")).toString() != QLatin1String("ops"))
    return;
  QVector<Engine::Op> remote;
  const QJsonArray ops = message.value(QStringLiteral("ops")).toArray();
  remote.reserve(ops.size());
  for (const QJsonValue &value : ops)
    remote.append(Engine::Op::fromJson(value.toObject()));
  if (!m_state)
    return;
  const int added = m_state->merge(remote);
  if (added > 0) {
    emit remoteOpsMerged(added);
    broadcast();
  }
}

} // namespace Templnote::Sync
