#include "AgentHttpServer.h"

#include "ToolSchema.h"
#include "ProviderRegistry.h"
#include "engine/ChangeFeed.h"
#include "engine/EditorSession.h"
#include "engine/Types.h"
#include "plugins/PluginManager.h"

#include <QHttpHeaders>
#include <QHttpServer>
#include <QHttpServerConfiguration>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantList>

#include <memory>

namespace Templnote::AI {
namespace {

using Status = QHttpServerResponse::StatusCode;

QHttpServerResponse jsonReply(const QJsonObject &object,
                              Status status = Status::Ok) {
  return QHttpServerResponse(
      QByteArrayLiteral("application/json"),
      QJsonDocument(object).toJson(QJsonDocument::Compact), status);
}

QHttpServerResponse jsonReply(const QJsonArray &array,
                              Status status = Status::Ok) {
  return QHttpServerResponse(
      QByteArrayLiteral("application/json"),
      QJsonDocument(array).toJson(QJsonDocument::Compact), status);
}

QHttpServerResponse errorReply(const QString &message, Status status) {
  QJsonObject object;
  object.insert(QStringLiteral("error"), message);
  return jsonReply(object, status);
}

QByteArray methodName(QHttpServerRequest::Method method) {
  switch (method) {
  case QHttpServerRequest::Method::Get:
    return "GET";
  case QHttpServerRequest::Method::Post:
    return "POST";
  case QHttpServerRequest::Method::Put:
    return "PUT";
  case QHttpServerRequest::Method::Delete:
    return "DELETE";
  case QHttpServerRequest::Method::Options:
    return "OPTIONS";
  default:
    return "UNKNOWN";
  }
}

QByteArray randomToken(int bytes = 32) {
  QByteArray result;
  result.reserve(bytes * 2);
  for (int i = 0; i < bytes; ++i) {
    const quint8 value =
        static_cast<quint8>(QRandomGenerator::global()->bounded(256));
    result.append(QByteArray::number(value, 16).rightJustified(2, '0'));
  }
  return result;
}

bool safeBaseUrl(const QString &baseUrl) {
  const QUrl url(baseUrl);
  if (!url.isValid() || url.scheme() == QLatin1String("file"))
    return false;
  if (url.scheme() == QLatin1String("https"))
    return true;
  if (url.scheme() != QLatin1String("http"))
    return false;
  const QString host = url.host().toLower();
  return host == QStringLiteral("127.0.0.1") ||
         host == QStringLiteral("localhost") || host == QStringLiteral("::1");
}

} // namespace

AgentHttpServer::AgentHttpServer(QObject *parent) : QObject(parent) {}

AgentHttpServer::~AgentHttpServer() { close(); }

void AgentHttpServer::setAuthToken(const QString &token) { m_authToken = token; }

void AgentHttpServer::setPipeline(PipelineRunner *pipeline) {
  m_pipeline = pipeline;
  if (!pipeline)
    return;
  connect(pipeline, &PipelineRunner::stageChanged, this, [this] {
    QJsonObject data;
    data.insert(QStringLiteral("stage"), m_pipeline->currentStage());
    broadcastSse("stage", QJsonDocument(data).toJson(QJsonDocument::Compact));
  });
  connect(pipeline, &PipelineRunner::runningChanged, this, [this] {
    QJsonObject data;
    data.insert(QStringLiteral("running"), m_pipeline->running());
    broadcastSse("running", QJsonDocument(data).toJson(QJsonDocument::Compact));
  });
  connect(pipeline, &PipelineRunner::finished, this,
          [this](const PipelineResult &result) {
            QJsonObject data;
            data.insert(QStringLiteral("ok"), result.ok);
            data.insert(QStringLiteral("stages"),
                        static_cast<int>(result.completedStages.size()));
            data.insert(QStringLiteral("revisions"), result.revisionsAdvanced);
            if (!result.error.isEmpty())
              data.insert(QStringLiteral("error"), result.error);
            broadcastSse("finished",
                         QJsonDocument(data).toJson(QJsonDocument::Compact));
          });
}

void AgentHttpServer::noteRequest(const QHttpServerRequest &request) {
  emit requestReceived(QString::fromLatin1(methodName(request.method())),
                       request.url().path());
}

bool AgentHttpServer::authorised(const QHttpServerRequest &request) const {
  const QByteArray header = request.value(QByteArrayLiteral("authorization")).toLower();
  const QByteArray prefix = QByteArrayLiteral("bearer ");
  if (!header.startsWith(prefix))
    return false;
  return constantTimeEquals(header.mid(prefix.size()).trimmed(),
                            m_authToken.toUtf8());
}

bool AgentHttpServer::constantTimeEquals(const QByteArray &a,
                                         const QByteArray &b) {
  if (a.size() != b.size())
    return false;
  volatile unsigned char diff = 0;
  for (int i = 0; i < a.size(); ++i)
    diff |= static_cast<unsigned char>(a.at(i)) ^
            static_cast<unsigned char>(b.at(i));
  return diff == 0;
}

QHttpServerResponse AgentHttpServer::unauthorised() const {
  QHttpServerResponse response =
      errorReply(QStringLiteral("unauthorized"), Status::Unauthorized);
  QHttpHeaders headers = response.headers();
  headers.append(QByteArrayLiteral("WWW-Authenticate"),
                 QByteArrayLiteral("Bearer realm=\"templnote\""));
  response.setHeaders(headers);
  return response;
}

void AgentHttpServer::broadcastSse(const QByteArray &event,
                                   const QByteArray &data) {
  const QByteArray message =
      "event: " + event + "\ndata: " + data + "\n\n";
  for (auto it = m_sseClients.begin(); it != m_sseClients.end();) {
    if ((*it)->isResponseCanceled()) {
      it = m_sseClients.erase(it);
      continue;
    }
    (*it)->writeChunk(message);
    ++it;
  }
}

void AgentHttpServer::close() {
  m_sseClients.clear();
  if (m_tcpServer) {
    m_tcpServer->close();
    m_tcpServer = nullptr; // owned by m_httpServer (QObject parent)
  }
  if (m_httpServer) {
    m_httpServer->deleteLater();
    m_httpServer = nullptr;
  }
}

bool AgentHttpServer::listen(quint16 port, const QString &host) {
  if (m_authToken.isEmpty())
    m_authToken = QString::fromLatin1(randomToken());
  if (!m_httpServer) {
    m_httpServer = new QHttpServer(this);

    QHttpServerConfiguration config;
    config.setMaximumBodySize(8 * 1024 * 1024);
    config.setMaximumTotalHeaderSize(16 * 1024);
    config.setMaximumHeaderFieldSize(8 * 1024);
    config.setMaximumUrlSize(8 * 1024);
    config.setMaximumConnectionsPerHost(8);
    config.setRateLimitPerSecond(200);
    m_httpServer->setConfiguration(config);

    // Security and cache headers on every response. CORS is never wildcard.
    m_httpServer->addAfterRequestHandler(
        this, [this](const QHttpServerRequest &, QHttpServerResponse &response) {
          QHttpHeaders headers = response.headers();
          headers.append(QHttpHeaders::WellKnownHeader::CacheControl, "no-store");
          headers.append(QByteArrayLiteral("X-Content-Type-Options"), "nosniff");
          if (!m_allowedOrigin.isEmpty()) {
            headers.append(QHttpHeaders::WellKnownHeader::AccessControlAllowOrigin,
                           m_allowedOrigin.toUtf8());
            headers.append(QByteArrayLiteral("Vary"), "Origin");
            headers.append(
                QHttpHeaders::WellKnownHeader::AccessControlAllowMethods,
                "GET, POST, OPTIONS");
            headers.append(
                QHttpHeaders::WellKnownHeader::AccessControlAllowHeaders,
                "Authorization, Content-Type");
          }
          response.setHeaders(headers);
        });

    using Method = QHttpServerRequest::Method;

    m_httpServer->route("/document", Method::Get, this,
                        [this](const QHttpServerRequest &request) {
                          noteRequest(request);
                          if (!authorised(request))
                            return unauthorised();
                          if (!m_session)
                            return errorReply(QStringLiteral("no session"),
                                              Status::ServiceUnavailable);
                          return jsonReply(m_session->toJson());
                        });
    m_httpServer->route("/changes", Method::Get, this,
                        [this](const QHttpServerRequest &request) {
                          noteRequest(request);
                          if (!authorised(request))
                            return unauthorised();
                          QJsonArray array;
                          if (m_feed) {
                            for (const auto &event : m_feed->events())
                              array.append(event.patch.toJson());
                          }
                          return jsonReply(array);
                        });
    m_httpServer->route("/status", Method::Get, this,
                        [this](const QHttpServerRequest &request) {
                          noteRequest(request);
                          if (!authorised(request))
                            return unauthorised();
                          QJsonObject status;
                          status.insert(QStringLiteral("running"),
                                        m_pipeline && m_pipeline->running());
                          status.insert(QStringLiteral("stage"),
                                        m_pipeline ? m_pipeline->currentStage()
                                                   : QString());
                          status.insert(QStringLiteral("revision"),
                                        m_session ? m_session->revision() : 0);
                          return jsonReply(status);
                        });
    m_httpServer->route("/tools", Method::Get, this,
                        [this](const QHttpServerRequest &request) {
                          noteRequest(request);
                          if (!authorised(request))
                            return unauthorised();
                          QJsonArray tools = ToolSchema::definitions();
                          for (const QJsonValue &tool :
                               ToolSchema::pluginDefinitions())
                            tools.append(tool);
                          return jsonReply(tools);
                        });
    m_httpServer->route("/providers", Method::Get, this,
                        [this](const QHttpServerRequest &request) {
                          noteRequest(request);
                          if (!authorised(request))
                            return unauthorised();
                          if (!m_providerRegistry)
                            return errorReply(
                                QStringLiteral("no provider registry"),
                                Status::ServiceUnavailable);
                          QJsonArray array;
                          for (const QVariant &value :
                               m_providerRegistry->providers())
                            array.append(QJsonValue::fromVariant(value));
                          return jsonReply(array);
                        });
    m_httpServer->route("/models", Method::Get, this,
                        [this](const QHttpServerRequest &request) {
                          noteRequest(request);
                          if (!authorised(request))
                            return unauthorised();
                          if (!m_providerRegistry)
                            return errorReply(
                                QStringLiteral("no provider registry"),
                                Status::ServiceUnavailable);
                          const QString providerId =
                              request.query().queryItemValue(
                                  QStringLiteral("provider"));
                          const QVariantList models =
                              providerId.isEmpty()
                                  ? m_providerRegistry->models()
                                  : m_providerRegistry->modelsFor(providerId);
                          QJsonArray array;
                          for (const QVariant &value : models)
                            array.append(QJsonValue::fromVariant(value));
                          return jsonReply(array);
                        });
    m_httpServer->route("/events", Method::Get, this,
                        [this](const QHttpServerRequest &request,
                               QHttpServerResponder &&responder) {
                          noteRequest(request);
                          if (!authorised(request)) {
                            responder.sendResponse(unauthorised());
                            return;
                          }
                          if (!m_pipeline) {
                            responder.sendResponse(errorReply(
                                QStringLiteral("no pipeline"),
                                Status::ServiceUnavailable));
                            return;
                          }
                          auto stream =
                              std::make_shared<QHttpServerResponder>(
                                  std::move(responder));
                          stream->writeBeginChunked(
                              QByteArrayLiteral("text/event-stream"),
                              Status::Ok);
                          stream->writeChunk(": connected\n\n");
                          m_sseClients.append(stream);
                        });

    m_httpServer->route("/command", Method::Post, this,
                        [this](const QHttpServerRequest &request) {
                          noteRequest(request);
                          if (!authorised(request))
                            return unauthorised();
                          if (!m_session)
                            return errorReply(QStringLiteral("no session"),
                                              Status::ServiceUnavailable);
                          QJsonParseError error;
                          const QJsonDocument doc =
                              QJsonDocument::fromJson(request.body(), &error);
                          if (error.error != QJsonParseError::NoError ||
                              !doc.isObject())
                            return errorReply(QStringLiteral("invalid JSON"),
                                              Status::BadRequest);
                          // Identity and causality are server-controlled.
                          QJsonObject object = doc.object();
                          object.remove(QStringLiteral("actor"));
                          object.remove(QStringLiteral("lamport"));
                          QString commandError;
                          const Engine::Command cmd =
                              Engine::Command::fromJson(object, &commandError);
                          if (!commandError.isEmpty())
                            return errorReply(commandError, Status::BadRequest);
                          const auto result = m_session->apply(cmd);
                          if (!result.ok())
                            return errorReply(result.error(), Status::Forbidden);
                          emit commandCommitted(result.value());
                          return jsonReply(result.value().toJson());
                        });
    m_httpServer->route("/prompt", Method::Post, this,
                        [this](const QHttpServerRequest &request) {
                          noteRequest(request);
                          if (!authorised(request))
                            return unauthorised();
                          if (!m_pipeline)
                            return errorReply(QStringLiteral("no pipeline"),
                                              Status::ServiceUnavailable);
                          const QJsonObject args =
                              QJsonDocument::fromJson(request.body()).object();
                          const QString prompt =
                              args.value(QStringLiteral("prompt")).toString();
                          if (prompt.trimmed().isEmpty())
                            return errorReply(QStringLiteral("missing prompt"),
                                              Status::BadRequest);
                          if (!m_pipeline->startAuthorised(prompt))
                            return errorReply(
                                QStringLiteral("a pipeline is already running"),
                                Status::Conflict);
                          QJsonObject result;
                          result.insert(QStringLiteral("started"), true);
                          result.insert(QStringLiteral("prompt"), prompt);
                          return jsonReply(result);
                        });
    m_httpServer->route("/cancel", Method::Post, this,
                        [this](const QHttpServerRequest &request) {
                          noteRequest(request);
                          if (!authorised(request))
                            return unauthorised();
                          if (m_pipeline)
                            m_pipeline->cancel();
                          QJsonObject result;
                          result.insert(QStringLiteral("ok"), true);
                          return jsonReply(result);
                        });
    m_httpServer->route("/plugin-tool", Method::Post, this,
                        [this](const QHttpServerRequest &request) {
                          noteRequest(request);
                          if (!authorised(request))
                            return unauthorised();
                          if (!m_pluginManager)
                            return errorReply(
                                QStringLiteral("no plugin manager"),
                                Status::ServiceUnavailable);
                          QJsonParseError error;
                          const QJsonDocument doc =
                              QJsonDocument::fromJson(request.body(), &error);
                          if (error.error != QJsonParseError::NoError ||
                              !doc.isObject())
                            return errorReply(QStringLiteral("invalid JSON"),
                                              Status::BadRequest);
                          const QJsonObject result =
                              m_pluginManager->applyPluginTool(doc.object());
                          return jsonReply(
                              result,
                              result.value(QStringLiteral("ok")).toBool()
                                  ? Status::Ok
                                  : Status::BadRequest);
                        });
    m_httpServer->route("/provider/connect", Method::Post, this,
                        [this](const QHttpServerRequest &request,
                               QHttpServerResponder &&responder) {
                          noteRequest(request);
                          if (!authorised(request)) {
                            responder.sendResponse(unauthorised());
                            return;
                          }
                          if (!m_providerRegistry) {
                            responder.sendResponse(errorReply(
                                QStringLiteral("no provider registry"),
                                Status::ServiceUnavailable));
                            return;
                          }
                          QJsonParseError error;
                          const QJsonObject args =
                              QJsonDocument::fromJson(request.body(), &error)
                                  .object();
                          const QString id =
                              args.value(QStringLiteral("id")).toString();
                          const QString key =
                              args.value(QStringLiteral("apiKey")).toString();
                          const QString baseUrl =
                              args.value(QStringLiteral("baseUrl")).toString();
                          if (error.error != QJsonParseError::NoError ||
                              id.isEmpty() || key.isEmpty() ||
                              (!baseUrl.isEmpty() && !safeBaseUrl(baseUrl))) {
                            responder.sendResponse(errorReply(
                                QStringLiteral("id, apiKey and a loopback/https baseUrl are required"),
                                Status::BadRequest));
                            return;
                          }
                          // Async validation: never block the server's event
                          // loop while the provider lists its models.
                          auto stream = std::make_shared<QHttpServerResponder>(
                              std::move(responder));
                          auto finished = std::make_shared<bool>(false);
                          auto complete = [stream, finished](bool ok,
                                                              const QString &message) {
                            if (*finished)
                              return;
                            *finished = true;
                            if (ok) {
                              QJsonObject result;
                              result.insert(QStringLiteral("ok"), true);
                              stream->sendResponse(jsonReply(result));
                            } else {
                              stream->sendResponse(errorReply(
                                  message.isEmpty()
                                      ? QStringLiteral("Connection failed")
                                      : message,
                                  Status::BadRequest));
                            }
                          };
                          connect(m_providerRegistry,
                                  &ProviderRegistry::connected, this,
                                  [id, complete](const QString &connectedId) {
                                    if (connectedId == id)
                                      complete(true, QString());
                                  });
                          connect(m_providerRegistry, &ProviderRegistry::error,
                                  this, [complete](const QString &message) {
                                    complete(false, message);
                                  });
                          QTimer::singleShot(12000, this, [complete] {
                            complete(false,
                                     QStringLiteral("Connection timed out"));
                          });
                          m_providerRegistry->connectProvider(id, key, baseUrl);
                        });
    m_httpServer->route("/provider/disconnect", Method::Post, this,
                        [this](const QHttpServerRequest &request) {
                          noteRequest(request);
                          if (!authorised(request))
                            return unauthorised();
                          if (!m_providerRegistry)
                            return errorReply(
                                QStringLiteral("no provider registry"),
                                Status::ServiceUnavailable);
                          const QJsonObject args =
                              QJsonDocument::fromJson(request.body()).object();
                          m_providerRegistry->disconnectProvider(
                              args.value(QStringLiteral("id")).toString());
                          QJsonObject result;
                          result.insert(QStringLiteral("ok"), true);
                          return jsonReply(result);
                        });
    m_httpServer->route("/model/toggle", Method::Post, this,
                        [this](const QHttpServerRequest &request) {
                          noteRequest(request);
                          if (!authorised(request))
                            return unauthorised();
                          if (!m_providerRegistry)
                            return errorReply(
                                QStringLiteral("no provider registry"),
                                Status::ServiceUnavailable);
                          const QJsonObject args =
                              QJsonDocument::fromJson(request.body()).object();
                          const bool ok = m_providerRegistry->setModelEnabled(
                              args.value(QStringLiteral("providerId")).toString(),
                              args.value(QStringLiteral("modelId")).toString(),
                              args.value(QStringLiteral("enabled")).toBool());
                          return ok ? jsonReply(QJsonObject{{QStringLiteral("ok"), true}})
                                    : errorReply(QStringLiteral("unknown model"),
                                                 Status::BadRequest);
                        });
    m_httpServer->route("/model/default", Method::Post, this,
                        [this](const QHttpServerRequest &request) {
                          noteRequest(request);
                          if (!authorised(request))
                            return unauthorised();
                          if (!m_providerRegistry)
                            return errorReply(
                                QStringLiteral("no provider registry"),
                                Status::ServiceUnavailable);
                          const QJsonObject args =
                              QJsonDocument::fromJson(request.body()).object();
                          m_providerRegistry->setDefaultModelKey(
                              args.value(QStringLiteral("key")).toString());
                          QJsonObject result;
                          result.insert(QStringLiteral("ok"), true);
                          return jsonReply(result);
                        });
  }

  const QHostAddress address = host.isEmpty() ? QHostAddress::LocalHost
                                              : QHostAddress(host);
  if (address.isNull()) {
    emit serverError(QStringLiteral("Invalid bind address"));
    return false;
  }
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
  if (!m_tcpServer)
    m_tcpServer = new QTcpServer(m_httpServer);
  if (!m_tcpServer->listen(address, port)) {
    emit serverError(m_tcpServer->errorString());
    return false;
  }
  if (!m_httpServer->bind(m_tcpServer)) {
    emit serverError(QStringLiteral("Could not attach HTTP server to socket"));
    return false;
  }
  m_port = m_tcpServer->serverPort();
#else
  m_port = m_httpServer->listen(address, port);
  if (!m_port) {
    emit serverError(QStringLiteral("Could not listen on HTTP port"));
    return false;
  }
#endif
  return true;
}

} // namespace Templnote::AI
