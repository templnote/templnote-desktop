#pragma once

// LAN document sync. Peers exchange their CRDT op sets over a length-framed
// TCP protocol and merge them. No cloud service required: run a peer on the
// desktop and another on a phone/home node on the same network (or over a
// WireGuard tunnel for remote access).
//
// Authentication uses a mutual HMAC challenge/response. The shared token is
// never transmitted on the wire, so sniffing the (unencrypted) LAN traffic
// does not reveal it. Peers must be running this protocol version.

#include "engine/Types.h"

namespace Templnote::Engine {
class CrdtDocumentState;
}

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

QT_BEGIN_NAMESPACE
class QTcpServer;
class QTcpSocket;
QT_END_NAMESPACE

namespace Templnote::Sync {

class SyncManager : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool running READ running NOTIFY runningChanged)
  Q_PROPERTY(int port READ port CONSTANT)
  Q_PROPERTY(int peerCount READ peerCount NOTIFY peersChanged)

 public:
  explicit SyncManager(int port = 45210, QObject *parent = nullptr);
  ~SyncManager() override;

  bool running() const;
  int port() const { return m_port; }
  // Actual bound port (may differ from port() when configured with 0).
  quint16 serverPort() const;
  int peerCount() const;

  // Provide the CRDT state to sync. The manager does not own it.
  void setState(Engine::CrdtDocumentState *state);

  // Peers authenticate with a mutual HMAC challenge. Bind to loopback unless
  // LAN access is explicitly requested; use a VPN tunnel for remote peers.
  void setAuthToken(const QString &token) { m_authToken = token.toUtf8(); }
  void setHost(const QString &host) { m_host = host; }

  bool startServer();
  void stop();
  void connectToPeer(const QString &host, int port = 0);

  // Broadcast the current op set to all connected peers.
  void broadcast();

 signals:
  void runningChanged();
  void peersChanged();
  void remoteOpsMerged(int count);
  void syncError(const QString &message);

 private slots:
  void onNewConnection();
  void onReadyRead();
  void onPeerDisconnected();
  void onPeerConnected();

 private:
  struct Handshake {
    QByteArray nonce;     // our random nonce for this connection
    QByteArray peerNonce; // nonce received from the peer
    bool sentMac = false; // we have sent our proof for these two nonces
  };

  void sendFrame(QTcpSocket *socket, const QByteArray &payload);
  void sendOps(QTcpSocket *socket);
  // Takes the already-parsed frame from onReadyRead() so the payload is not
  // JSON-decoded twice on the receive path.
  void processMessage(const QJsonObject &message);
  void beginHandshake(QTcpSocket *socket);
  QByteArray challengeMac(const Handshake &hs) const;
  QByteArray proof(const Handshake &hs) const;
  bool verifyPeer(const Handshake &hs, const QByteArray &mac) const;
  void rejectPeer(QTcpSocket *socket, const QString &reason);

  int m_port = 45210;
  QString m_host = QStringLiteral("127.0.0.1");
  QByteArray m_authToken;
  QTcpServer *m_server = nullptr;
  QVector<QTcpSocket *> m_peers;
  QSet<QTcpSocket *> m_authenticated;
  QHash<QTcpSocket *, QByteArray> m_buffers;
  QHash<QTcpSocket *, Handshake> m_handshakes;
  Engine::CrdtDocumentState *m_state = nullptr;
  bool m_stopped = false;

  static constexpr quint32 kMaxFrameSize = 4 * 1024 * 1024;
  static constexpr int kMaxOpsPerFrame = 4096;
};

} // namespace Templnote::Sync
