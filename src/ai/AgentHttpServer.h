#pragma once

// Authenticated local control API, built on Qt's standards-compliant
// QHttpServer (handles chunked bodies, keep-alive, header/body size limits).
//
//   GET  /document /changes /status /tools /providers /models /events (SSE)
//   POST /command /prompt /cancel /plugin-tool /provider/connect
//        /provider/disconnect /model/toggle /model/default
//
// Security contract: loopback bind by default, bearer token on every request,
// no wildcard CORS, caller-supplied actor/lamport ignored, plugin approval
// never reachable, endpoint URLs restricted to HTTPS/loopback before a key is
// sent. See docs/HEADLESS_ENGINE.md.

#include "PipelineRunner.h"

#include <QHttpServerResponder>
#include <QObject>
#include <QString>

#include <memory>

QT_BEGIN_NAMESPACE
class QHttpServer;
class QHttpServerRequest;
class QHttpServerResponse;
class QTcpServer;
QT_END_NAMESPACE

namespace Templnote::Engine {
class ChangeFeed;
class EditorSession;
struct Patch;
} // namespace Templnote::Engine

namespace Templnote {
class PluginManager;
}

namespace Templnote::AI {
class ProviderRegistry;

class AgentHttpServer : public QObject {
  Q_OBJECT
public:
  explicit AgentHttpServer(QObject *parent = nullptr);
  ~AgentHttpServer() override;

  bool listen(quint16 port = 8787,
              const QString &host = QStringLiteral("127.0.0.1"));
  void close();
  quint16 port() const { return m_port; }
  QString authToken() const { return m_authToken; }
  void setAuthToken(const QString &token);
  // When set, this exact origin is reflected in CORS responses. When empty,
  // no CORS headers are emitted at all.
  void setAllowedOrigin(const QString &origin) { m_allowedOrigin = origin; }

  void setPipeline(PipelineRunner *pipeline);
  void setDocumentProvider(Engine::EditorSession *session) {
    m_session = session;
  }
  void setChangesProvider(Engine::ChangeFeed *feed) { m_feed = feed; }
  void setPluginManager(::Templnote::PluginManager *manager) {
    m_pluginManager = manager;
  }
  void setProviderRegistry(ProviderRegistry *registry) {
    m_providerRegistry = registry;
  }

signals:
  void requestReceived(const QString &method, const QString &path);
  void serverError(const QString &message);
  void commandCommitted(const Engine::Patch &patch);

private:
  // Auth/header helpers shared by every route.
  void noteRequest(const QHttpServerRequest &request);
  bool authorised(const QHttpServerRequest &request) const;
  QHttpServerResponse unauthorised() const;
  static bool constantTimeEquals(const QByteArray &a, const QByteArray &b);
  void broadcastSse(const QByteArray &event, const QByteArray &data);

  QHttpServer *m_httpServer = nullptr;
  QTcpServer *m_tcpServer = nullptr;
  quint16 m_port = 0;
  QString m_authToken;
  QString m_allowedOrigin;
  QList<std::shared_ptr<QHttpServerResponder>> m_sseClients;

  PipelineRunner *m_pipeline = nullptr;
  Engine::EditorSession *m_session = nullptr;
  Engine::ChangeFeed *m_feed = nullptr;
  ::Templnote::PluginManager *m_pluginManager = nullptr;
  ProviderRegistry *m_providerRegistry = nullptr;
};

} // namespace Templnote::AI
