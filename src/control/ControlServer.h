#pragma once

// Loopback control surface for an external agent harness. Newline-delimited
// JSON in, newline-delimited JSON out. GUI-only: the headless CLI keeps using
// the engine/store directly and never links this class.

#include "bridge/LiveBridge.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTcpServer>
#include <QTimer>

class QTcpSocket;

namespace Templnote {

class ControlServer : public QObject {
  Q_OBJECT
public:
  explicit ControlServer(LiveBridge *bridge, const QString &configDir,
                         QObject *parent = nullptr);
  ~ControlServer() override;

  bool start(quint16 port = 0, QString *error = nullptr);
  void stop();
  bool isRunning() const { return m_server->isListening(); }
  quint16 port() const { return m_server->serverPort(); }
  QString token() const { return m_token; }
  QString endpointPath() const { return m_endpointPath; }

signals:
  void errorOccurred(const QString &message);

private slots:
  void onNewConnection();
  void onReadyRead();
  void onSocketDisconnected();
  bool flushStream();

private:
  void send(QTcpSocket *socket, const QJsonObject &message);
  void publishPatch(const QJsonObject &patch);
  QJsonObject handle(QTcpSocket *socket, const QJsonObject &request);
  QJsonObject context() const;
  bool authorized(const QJsonObject &request) const;
  bool applyCommands(const QJsonArray &commands, const QString &label,
                     QString *error);
  bool syncViewToDisk(QString *error);
  QJsonObject agentActor() const;
  QString lastBlockId() const;

  LiveBridge *m_bridge = nullptr;
  QString m_configDir;
  QString m_endpointPath;
  QTcpServer *m_server = nullptr;
  QSet<QTcpSocket *> m_sockets;
  QSet<QTcpSocket *> m_subscribers;
  QHash<QTcpSocket *, QByteArray> m_buffers;
  QString m_token;
  QTimer m_flushTimer;

  bool m_streaming = false;
  QString m_streamBlockId;
  int m_streamPosition = 0;
  QString m_streamBuffer;

  static constexpr int kMaxLineBytes = 1024 * 1024;
  static constexpr int kFlushIntervalMs = 60;
  static constexpr int kImmediateFlushBytes = 4096;
  static constexpr int kMaxStreamBufferBytes = 1024 * 1024;
};

} // namespace Templnote
