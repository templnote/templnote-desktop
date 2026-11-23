#pragma once

// LAN document sync. Peers exchange their CRDT op sets over a length-framed
// TCP protocol and merge them. No cloud service required: run a peer on the
// desktop and another on a phone/home node on the same network (or over a
// WireGuard tunnel for remote access).

#include "engine/Types.h"

namespace Templnote::Engine {
class CrdtDocumentState;
}

#include <QHash>
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
  int peerCount() const;

  // Provide the CRDT state to sync. The manager does not own it.
  void setState(Engine::CrdtDocumentState *state);

  // Peers authenticate with the first frame. Bind to loopback unless LAN
  // access is explicitly requested; use a VPN tunnel for remote peers.
  void setAuthToken(const QString &token) { m_authToken = token; }
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
  void sendFrame(QTcpSocket *socket, const QByteArray &payload);
  void sendOps(QTcpSocket *socket);
  void processMessage(const QByteArray &payload);

  int m_port = 45210;
  QString m_host = QStringLiteral("127.0.0.1");
  QString m_authToken;
  QTcpServer *m_server = nullptr;
  QVector<QTcpSocket *> m_peers;
  QSet<QTcpSocket *> m_authenticated;
  QHash<QTcpSocket *, QByteArray> m_buffers;
  class Engine::CrdtDocumentState *m_state = nullptr;
  static constexpr quint32 kMaxFrameSize = 32 * 1024 * 1024;
};

} // namespace Templnote::Sync
