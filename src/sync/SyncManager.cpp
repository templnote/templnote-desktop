#include "SyncManager.h"

#include "engine/CrdtDocumentState.h"
#include "tracking/ScopedTimer.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUuid>
#include <functional>
#include <utility>

namespace Templnote::Sync {

namespace {
constexpr int kHeaderSize = 4;

bool constantTimeEquals(const QByteArray &a, const QByteArray &b) {
  if (a.size() != b.size())
    return false;
  unsigned char diff = 0;
  for (int i = 0; i < a.size(); ++i)
    diff |= static_cast<unsigned char>(a.at(i)) ^
            static_cast<unsigned char>(b.at(i));
  return diff == 0;
}
} // namespace

SyncManager::SyncManager(int port, QObject *parent)
    : QObject(parent), m_port(port) {}

SyncManager::~SyncManager() { stop(); }

bool SyncManager::running() const { return m_server && m_server->isListening(); }

quint16 SyncManager::serverPort() const {
  return m_server ? m_server->serverPort() : 0;
}

int SyncManager::peerCount() const { return m_peers.size(); }

void SyncManager::setState(Engine::CrdtDocumentState *state) { m_state = state; }

bool SyncManager::startServer() {
  m_stopped = false;
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
  m_stopped = true;

  // Accepted sockets are children of the server. Reparenting in
  // onNewConnection()/connectToPeer() makes them children of this manager, so
  // it is safe to disconnect and delete them before destroying the server.
  const QVector<QTcpSocket *> peers = m_peers;
  for (QTcpSocket *socket : peers) {
    if (!socket)
      continue;
    disconnect(socket, nullptr, this, nullptr);
    socket->disconnectFromHost();
    delete socket;
  }
  m_peers.clear();
  m_authenticated.clear();
  m_buffers.clear();
  m_handshakes.clear();

  if (m_server) {
    m_server->close();
    delete m_server;
    m_server = nullptr;
    emit runningChanged();
  }
  emit peersChanged();
}

void SyncManager::connectToPeer(const QString &host, int port) {
  if (m_stopped || host.isEmpty() || port < 1 || port > 65535) {
    emit syncError(QStringLiteral("Invalid sync peer address"));
    return;
  }
  auto *socket = new QTcpSocket(this);
  connect(socket, &QTcpSocket::readyRead, this, &SyncManager::onReadyRead);
  connect(socket, &QTcpSocket::disconnected, this,
          &SyncManager::onPeerDisconnected);
  connect(socket, &QTcpSocket::connected, this, &SyncManager::onPeerConnected);
  socket->connectToHost(host, static_cast<quint16>(port));
  m_peers.append(socket);
  m_buffers.insert(socket, QByteArray());
  emit peersChanged();
}

void SyncManager::onPeerConnected() {
  // A remote peer is not trusted until it proves it knows the token, even on
  // an outbound connection we initiated ourselves.
  auto *socket = qobject_cast<QTcpSocket *>(sender());
  if (socket)
    beginHandshake(socket);
}

void SyncManager::onNewConnection() {
  if (m_stopped || !m_server)
    return;
  while (m_server->hasPendingConnections()) {
    QTcpSocket *socket = m_server->nextPendingConnection();
    socket->setParent(this); // do not let the server own the socket's lifetime
    connect(socket, &QTcpSocket::readyRead, this, &SyncManager::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this,
            &SyncManager::onPeerDisconnected);
    m_peers.append(socket);
    m_buffers.insert(socket, QByteArray());
    beginHandshake(socket);
  }
  emit peersChanged();
}

void SyncManager::beginHandshake(QTcpSocket *socket) {
  if (!socket || m_stopped || m_handshakes.contains(socket))
    return;
  Handshake hs;
  hs.nonce = QUuid::createUuid().toRfc4122().toHex();
  m_handshakes.insert(socket, hs);
  QJsonObject auth;
  auth.insert(QStringLiteral("type"), QStringLiteral("auth"));
  auth.insert(QStringLiteral("v"), 2);
  auth.insert(QStringLiteral("nonce"), QString::fromLatin1(hs.nonce));
  sendFrame(socket, QJsonDocument(auth).toJson(QJsonDocument::Compact));
}

QByteArray SyncManager::challengeMac(const Handshake &hs) const {
  // Both peers must derive the same proof, so order the two nonces
  // canonically instead of using "mine then theirs".
  QByteArray first = hs.nonce;
  QByteArray second = hs.peerNonce;
  if (second < first)
    std::swap(first, second);
  const QByteArray material = QByteArrayLiteral("templnote-sync-v2|") + first +
                              QByteArrayLiteral("|") + second;
  return QCryptographicHash::hash(material, QCryptographicHash::Sha256)
      .toHex();
}

QByteArray SyncManager::proof(const Handshake &hs) const {
  if (m_authToken.isEmpty())
    return {};
  return QMessageAuthenticationCode::hash(challengeMac(hs), m_authToken,
                                          QCryptographicHash::Sha256)
      .toHex();
}

bool SyncManager::verifyPeer(const Handshake &hs,
                             const QByteArray &mac) const {
  if (m_authToken.isEmpty())
    return false;
  return constantTimeEquals(mac, proof(hs));
}

void SyncManager::rejectPeer(QTcpSocket *socket, const QString &reason) {
  emit syncError(reason);
  if (socket) {
    disconnect(socket, nullptr, this, nullptr);
    socket->disconnectFromHost();
    socket->deleteLater();
  }
  m_peers.removeAll(socket);
  m_authenticated.remove(socket);
  m_buffers.remove(socket);
  m_handshakes.remove(socket);
  emit peersChanged();
}

void SyncManager::onPeerDisconnected() {
  auto *socket = qobject_cast<QTcpSocket *>(sender());
  if (!socket)
    return;
  m_peers.removeAll(socket);
  m_authenticated.remove(socket);
  m_buffers.remove(socket);
  m_handshakes.remove(socket);
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
  // Header-only ScopedTimer; only surface a slow send so a busy keystroke
  // stream does not flood the log.
  ScopedTimer timing(QStringLiteral("SyncManager::sendOps"), [](double ms) {
    if (ms >= 1.0)
      qInfo().noquote() << QStringLiteral("[SYNC_TIMING] sendOps: %1 ms")
                               .arg(ms, 0, 'f', 3);
  });

  const QVector<Engine::Op> allOps = m_state->opSet(); // implicit sharing, cheap
  if (allOps.isEmpty())
    return;

  // {"type":"ops","ops":[]} plus separators. The estimate below is
  // conservative; flush() still verifies the exact serialized size.
  constexpr qsizetype kFrameEnvelopeBytes = 64;
  QJsonArray batch;
  qsizetype estimatedBytes = kFrameEnvelopeBytes;

  std::function<void(QJsonArray)> sendBatch = [&](QJsonArray ops) {
    if (ops.isEmpty())
      return;
    QJsonObject message;
    message.insert(QStringLiteral("type"), QStringLiteral("ops"));
    message.insert(QStringLiteral("ops"), ops);
    const QByteArray payload =
        QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (payload.size() <= static_cast<int>(kMaxFrameSize)) {
      sendFrame(socket, payload);
      return;
    }
    if (ops.size() == 1) {
      // An atomic op larger than the frame limit cannot be split without
      // changing the wire protocol. Report it instead of dropping silently.
      emit syncError(QStringLiteral("Sync op too large to transmit."));
      return;
    }
    // Defensive fallback if the per-op estimate under-shot the real size:
    // halve the batch and retry, still in order.
    const qsizetype mid = ops.size() / 2;
    QJsonArray left, right;
    for (qsizetype i = 0; i < ops.size(); ++i) {
      if (i < mid)
        left.append(ops.at(i));
      else
        right.append(ops.at(i));
    }
    sendBatch(left);
    sendBatch(right);
  };
  const auto flush = [&]() {
    if (batch.isEmpty())
      return;
    sendBatch(batch);
    batch = QJsonArray();
    estimatedBytes = kFrameEnvelopeBytes;
  };

  for (const Engine::Op &op : allOps) {
    const QJsonObject opJson = op.toJson();
    // Serializing each op once for its size keeps the estimate O(n); the
    // previous code built one giant JSON document and then dropped it.
    const qsizetype opBytes =
        QJsonDocument(opJson).toJson(QJsonDocument::Compact).size() + 1;
    if (!batch.isEmpty() &&
        (batch.size() >= kMaxOpsPerFrame ||
         estimatedBytes + opBytes > static_cast<qsizetype>(kMaxFrameSize)))
      flush();
    batch.append(opJson);
    estimatedBytes += opBytes;
  }
  // Frames are written in order; TCP preserves that order to the peer.
  flush();
}

void SyncManager::broadcast() {
  if (m_stopped)
    return;
  for (QTcpSocket *socket : m_peers)
    sendOps(socket);
}

void SyncManager::onReadyRead() {
  ScopedTimer timing(QStringLiteral("SyncManager::onReadyRead"), [](double ms) {
    if (ms >= 1.0)
      qInfo().noquote() << QStringLiteral("[SYNC_TIMING] onReadyRead: %1 ms")
                               .arg(ms, 0, 'f', 3);
  });
  if (m_stopped)
    return;
  auto *socket = qobject_cast<QTcpSocket *>(sender());
  if (!socket || !m_peers.contains(socket))
    return;
  QByteArray &buffer = m_buffers[socket];
  buffer.append(socket->readAll());
  while (buffer.size() >= static_cast<int>(kHeaderSize)) {
    quint32 size = 0;
    for (int i = 0; i < kHeaderSize; ++i)
      size = (size << 8) | static_cast<quint8>(buffer.at(i));
    if (size > kMaxFrameSize) {
      rejectPeer(socket, QStringLiteral("Oversized sync frame"));
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

    if (message.value(QStringLiteral("type")).toString() ==
        QLatin1String("auth")) {
      auto it = m_handshakes.find(socket);
      if (it == m_handshakes.end()) {
        beginHandshake(socket);
        it = m_handshakes.find(socket);
        if (it == m_handshakes.end())
          return;
      }
      Handshake &hs = it.value();
      const QByteArray peerNonce =
          message.value(QStringLiteral("nonce")).toString().toLatin1();
      if (peerNonce.size() != 32) {
        rejectPeer(socket, QStringLiteral("Invalid sync handshake nonce"));
        return;
      }
      if (!hs.peerNonce.isEmpty() && hs.peerNonce != peerNonce) {
        rejectPeer(socket, QStringLiteral("Sync handshake nonce changed"));
        return;
      }
      hs.peerNonce = peerNonce;
      const QByteArray mac =
          message.value(QStringLiteral("mac")).toString().toLatin1();
      if (mac.isEmpty()) {
        // First leg: prove we know the token over the two nonces.
        if (m_authToken.isEmpty()) {
          rejectPeer(socket, QStringLiteral("Sync token is not configured"));
          return;
        }
        hs.sentMac = true;
        QJsonObject reply;
        reply.insert(QStringLiteral("type"), QStringLiteral("auth"));
        reply.insert(QStringLiteral("v"), 2);
        reply.insert(QStringLiteral("nonce"), QString::fromLatin1(hs.nonce));
        reply.insert(QStringLiteral("mac"), QString::fromLatin1(proof(hs)));
        sendFrame(socket,
                  QJsonDocument(reply).toJson(QJsonDocument::Compact));
        continue;
      }
      if (!verifyPeer(hs, mac)) {
        rejectPeer(socket, QStringLiteral("Sync peer failed authentication"));
        return;
      }
      if (!m_authenticated.contains(socket)) {
        m_authenticated.insert(socket);
        sendOps(socket);
      }
      continue;
    }

    if (!m_authenticated.contains(socket)) {
      rejectPeer(socket, QStringLiteral("Unauthenticated sync frame ignored"));
      return;
    }
    processMessage(message);
  }
}

void SyncManager::processMessage(const QJsonObject &message) {
  // The frame was already decoded once in onReadyRead(); do not parse it again.
  if (message.value(QStringLiteral("type")).toString() != QLatin1String("ops"))
    return;
  const QJsonArray ops = message.value(QStringLiteral("ops")).toArray();
  if (ops.size() > kMaxOpsPerFrame) {
    emit syncError(QStringLiteral("Sync frame contains too many operations"));
    return;
  }
  QVector<Engine::Op> remote;
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
