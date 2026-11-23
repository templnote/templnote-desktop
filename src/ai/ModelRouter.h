#pragma once

// Model routing: prefer a local Ollama/LM Studio model when reachable, fall
// back to OpenRouter. The document never leaves the device in local mode;
// OpenRouter is used only when explicitly enabled or when no local model is
// available and the user allowed cloud fallback.

#include "ProviderRegistry.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QVector>

QT_BEGIN_NAMESPACE
class QNetworkReply;
QT_END_NAMESPACE

namespace Templnote::AI {

struct ChatMessage {
  QString role;    // system / user / assistant / tool
  QString content;
  // Assistant messages carry the exact tool calls the model emitted; tool
  // result messages carry the matching id so providers can pair them up.
  QJsonArray toolCalls;
  QString toolCallId;
  QString name;
};

struct ChatRequest {
  QVector<ChatMessage> messages;
  QJsonArray tools;
  double temperature = 0.7;
  int maxTokens = -1;
};

struct ChatResponse {
  bool ok = false;
  QString text;
  QJsonArray toolCalls;
  QString provider;
  QString model;
  QString error;
  int promptTokens = 0;
  int completionTokens = 0;
};

struct ModelInfo {
  QString id;
  QString name;
  QString provider;
};

class ModelRouter : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool localAvailable READ localAvailable NOTIFY providersChanged)
  Q_PROPERTY(bool cloudEnabled READ cloudEnabled WRITE setCloudEnabled NOTIFY
                 cloudEnabledChanged)
  Q_PROPERTY(QString activeProvider READ activeProvider NOTIFY
                 providersChanged)

public:
  explicit ModelRouter(QObject *parent = nullptr);

  bool localAvailable() const { return !m_localModels.isEmpty(); }
  bool cloudEnabled() const { return m_cloudEnabled; }
  void setCloudEnabled(bool enabled);
  QString activeProvider() const { return m_activeProvider; }

  QVector<ModelInfo> localModels() const { return m_localModels; }

  // Probe localhost:11434 (Ollama) and localhost:1234 (LM Studio) and refresh
  // the local model list. Emits providersChanged when availability changes.
  void detectLocalModels();
  void setLocalEndpoint(const QString &url) { m_localEndpoint = url; }
  void setOpenRouterKey(const QString &key) { m_openRouterKey = key; }
  void setLocalModel(const QString &model) { m_localModel = model; }
  void setCloudModel(const QString &model) { m_cloudModel = model; }

  // When a registry is set, chat() uses its default model and refuses to run
  // when no provider is connected. Without one it keeps the legacy local-first
  // behaviour.
  void setRegistry(ProviderRegistry *registry) { m_registry = registry; }
  ProviderRegistry *registry() const { return m_registry; }

  // Transient network/5xx failures are retried with exponential backoff.
  void setMaxRetries(int retries) { m_maxRetries = qBound(0, retries, 5); }
  int maxRetries() const { return m_maxRetries; }

  // Perform a chat completion against an explicit model from the registry.
  void chatWithModel(const ModelRef &model, const ChatRequest &request);

  // Perform a chat completion. Chooses the registry's default model, or local
  // or cloud by legacy policy.
  virtual void chat(const ChatRequest &request);

signals:
  void providersChanged();
  void cloudEnabledChanged();
  void finished(const ChatResponse &response);

private:
  void onDetectFinished();
  void onChatFinished();
  void scheduleRetry(const QString &error);
  void retryNow();
  QString chooseProvider() const;
  QNetworkRequest requestFor(const QString &provider) const;
  QJsonObject buildBody(const ChatRequest &request,
                        const QString &provider) const;
  ChatResponse parseResponse(const QByteArray &data,
                             const QString &provider) const;

  QNetworkRequest requestForResolved(const ResolvedModel &resolved) const;
  QJsonObject buildBodyFor(const ChatRequest &request,
                           const ResolvedModel &resolved) const;
  ChatResponse parseResponseFor(const QByteArray &data,
                                const ResolvedModel &resolved) const;

  QNetworkAccessManager *m_manager = nullptr;
  QVector<ModelInfo> m_localModels;
  QString m_localEndpoint = QStringLiteral("http://127.0.0.1:11434");
  QString m_openRouterKey;
  QString m_localModel = QStringLiteral("llama3.2");
  QString m_cloudModel = QStringLiteral("openai/gpt-4o-mini");
  bool m_cloudEnabled = false;
  QString m_activeProvider;
  QNetworkReply *m_chatReply = nullptr;
  ProviderRegistry *m_registry = nullptr;
  ResolvedModel m_resolved;
  bool m_useRegistry = false;
  QJsonObject m_pendingBody;
  QNetworkRequest m_pendingRequest;
  int m_retryAttempt = 0;
  int m_maxRetries = 2;
};

} // namespace Templnote::AI
