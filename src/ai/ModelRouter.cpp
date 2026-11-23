#include "ModelRouter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace Templnote::AI {
namespace {

// OpenAI-compatible and Ollama chats share the same message shape. Keeping the
// assistant's tool_calls and the matching tool_call_id is what makes a
// multi-round tool conversation valid instead of a single-shot edit.
QJsonArray openAiMessages(const QVector<ChatMessage> &messages) {
  QJsonArray result;
  for (const ChatMessage &message : messages) {
    QJsonObject object;
    object.insert(QStringLiteral("role"), message.role);
    object.insert(QStringLiteral("content"), message.content);
    if (!message.toolCalls.isEmpty())
      object.insert(QStringLiteral("tool_calls"), message.toolCalls);
    if (!message.toolCallId.isEmpty())
      object.insert(QStringLiteral("tool_call_id"), message.toolCallId);
    if (!message.name.isEmpty())
      object.insert(QStringLiteral("name"), message.name);
    result.append(object);
  }
  return result;
}

// Anthropic uses content blocks instead of a top-level tool_calls field and
// expects tool results as a user turn.
QJsonArray anthropicMessages(const QVector<ChatMessage> &messages,
                             QString *systemText) {
  QJsonArray result;
  for (const ChatMessage &message : messages) {
    if (message.role == QLatin1String("system")) {
      if (!systemText->isEmpty())
        systemText->append(QLatin1Char('\n'));
      systemText->append(message.content);
      continue;
    }
    if (message.role == QLatin1String("tool")) {
      QJsonObject block;
      block.insert(QStringLiteral("type"), QStringLiteral("tool_result"));
      block.insert(QStringLiteral("tool_use_id"), message.toolCallId);
      block.insert(QStringLiteral("content"), message.content);
      QJsonArray content;
      content.append(block);
      QJsonObject turn;
      turn.insert(QStringLiteral("role"), QStringLiteral("user"));
      turn.insert(QStringLiteral("content"), content);
      result.append(turn);
      continue;
    }
    if (!message.toolCalls.isEmpty()) {
      QJsonArray content;
      if (!message.content.isEmpty()) {
        QJsonObject text;
        text.insert(QStringLiteral("type"), QStringLiteral("text"));
        text.insert(QStringLiteral("text"), message.content);
        content.append(text);
      }
      for (const QJsonValue &value : message.toolCalls) {
        const QJsonObject call = value.toObject();
        const QJsonObject fn = call.value(QStringLiteral("function")).toObject();
        QJsonObject block;
        block.insert(QStringLiteral("type"), QStringLiteral("tool_use"));
        block.insert(QStringLiteral("id"), call.value(QStringLiteral("id")));
        block.insert(QStringLiteral("name"), fn.value(QStringLiteral("name")));
        block.insert(QStringLiteral("input"),
                     call.contains(QStringLiteral("arguments"))
                         ? call.value(QStringLiteral("arguments"))
                         : fn.value(QStringLiteral("arguments")));
        content.append(block);
      }
      QJsonObject turn;
      turn.insert(QStringLiteral("role"), QStringLiteral("assistant"));
      turn.insert(QStringLiteral("content"), content);
      result.append(turn);
      continue;
    }
    QJsonObject turn;
    turn.insert(QStringLiteral("role"), message.role);
    turn.insert(QStringLiteral("content"), message.content);
    result.append(turn);
  }
  return result;
}

void addUsage(ChatResponse *response, const QJsonObject &object,
              ProviderKind kind) {
  const QJsonObject usage = object.value(QStringLiteral("usage")).toObject();
  if (kind == ProviderKind::Anthropic) {
    response->promptTokens = usage.value(QStringLiteral("input_tokens")).toInt();
    response->completionTokens =
        usage.value(QStringLiteral("output_tokens")).toInt();
  } else if (kind == ProviderKind::Local) {
    response->promptTokens =
        object.value(QStringLiteral("prompt_eval_count")).toInt();
    response->completionTokens = object.value(QStringLiteral("eval_count")).toInt();
  } else {
    response->promptTokens = usage.value(QStringLiteral("prompt_tokens")).toInt();
    response->completionTokens =
        usage.value(QStringLiteral("completion_tokens")).toInt();
  }
}

} // namespace

ModelRouter::ModelRouter(QObject *parent)
    : QObject(parent), m_manager(new QNetworkAccessManager(this)) {}

void ModelRouter::setCloudEnabled(bool enabled) {
  if (m_cloudEnabled == enabled)
    return;
  m_cloudEnabled = enabled;
  emit cloudEnabledChanged();
  emit providersChanged();
}

void ModelRouter::detectLocalModels() {
  QNetworkRequest request(QUrl(m_localEndpoint + QStringLiteral("/api/tags")));
  request.setTransferTimeout(1500);
  QNetworkReply *reply = m_manager->get(request);
  connect(reply, &QNetworkReply::finished, this,
          &ModelRouter::onDetectFinished);
}

void ModelRouter::onDetectFinished() {
  auto *reply = qobject_cast<QNetworkReply *>(sender());
  if (!reply)
    return;
  const QByteArray data = reply->readAll();
  reply->deleteLater();
  const bool wasAvailable = localAvailable();
  m_localModels.clear();
  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
  if (error.error == QJsonParseError::NoError && doc.isObject()) {
    const QJsonArray models = doc.object().value(QStringLiteral("models")).toArray();
    for (const QJsonValue &value : models) {
      const QJsonObject object = value.toObject();
      ModelInfo info;
      info.name = object.value(QStringLiteral("name")).toString();
      info.id = info.name;
      info.provider = QStringLiteral("ollama");
      if (!info.id.isEmpty())
        m_localModels.append(info);
    }
  }
  if (wasAvailable != localAvailable())
    emit providersChanged();
}

QString ModelRouter::chooseProvider() const {
  if (localAvailable())
    return QStringLiteral("local");
  if (m_cloudEnabled && !m_openRouterKey.isEmpty())
    return QStringLiteral("cloud");
  return {};
}

QNetworkRequest ModelRouter::requestFor(const QString &provider) const {
  QUrl url;
  if (provider == QLatin1String("local")) {
    url = QUrl(m_localEndpoint + QStringLiteral("/api/chat"));
  } else {
    url = QUrl(QStringLiteral("https://openrouter.ai/api/v1/chat/completions"));
  }
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader,
                    QStringLiteral("application/json"));
  if (provider == QLatin1String("cloud")) {
    request.setRawHeader("Authorization",
                         QByteArray("Bearer ") + m_openRouterKey.toUtf8());
    request.setRawHeader("HTTP-Referer",
                         QByteArray("https://templnote.local"));
    request.setRawHeader("X-Title", QByteArray("Templnote"));
  }
  request.setTransferTimeout(60000);
  return request;
}

QJsonObject ModelRouter::buildBody(const ChatRequest &request,
                                   const QString &provider) const {
  QJsonObject body;
  body.insert(QStringLiteral("messages"), openAiMessages(request.messages));
  if (!request.tools.isEmpty()) {
    if (provider == QLatin1String("cloud")) {
      QJsonArray tools;
      for (const QJsonValue &tool : request.tools) {
        const QJsonObject t = tool.toObject();
        QJsonObject fn = t.value(QStringLiteral("function")).toObject();
        QJsonObject converted;
        converted.insert(QStringLiteral("type"), QStringLiteral("function"));
        converted.insert(QStringLiteral("function"), fn);
        tools.append(converted);
      }
      body.insert(QStringLiteral("tools"), tools);
      body.insert(QStringLiteral("tool_choice"), QStringLiteral("auto"));
    } else {
      body.insert(QStringLiteral("tools"), request.tools);
    }
  }
  body.insert(QStringLiteral("temperature"), request.temperature);
  if (request.maxTokens > 0)
    body.insert(QStringLiteral("max_tokens"), request.maxTokens);
  if (provider == QLatin1String("local"))
    body.insert(QStringLiteral("model"), m_localModel);
  else
    body.insert(QStringLiteral("model"), m_cloudModel);
  body.insert(QStringLiteral("stream"), false);
  return body;
}

void ModelRouter::chat(const ChatRequest &request) {
  if (m_registry) {
    const ModelRef model = m_registry->defaultModel();
    if (!model.isValid()) {
      ChatResponse failure;
      failure.error = m_registry->anyConnected()
                          ? QStringLiteral("No default model selected.")
                          : QStringLiteral("Connect a provider before using AI.");
      emit finished(failure);
      return;
    }
    chatWithModel(model, request);
    return;
  }
  ChatResponse failure;
  const QString provider = chooseProvider();
  if (provider.isEmpty()) {
    failure.error = m_cloudEnabled
                        ? QStringLiteral("No model available.")
                        : QStringLiteral("No local model; cloud is disabled.");
    emit finished(failure);
    return;
  }
  if (m_chatReply) {
    failure.error = QStringLiteral("A request is already in flight.");
    emit finished(failure);
    return;
  }
  m_activeProvider = provider;
  m_useRegistry = false;
  emit providersChanged();
  m_retryAttempt = 0;
  m_pendingBody = buildBody(request, provider);
  m_pendingRequest = requestFor(provider);
  m_chatReply = m_manager->post(
      m_pendingRequest, QJsonDocument(m_pendingBody).toJson(QJsonDocument::Compact));
  connect(m_chatReply, &QNetworkReply::finished, this,
          &ModelRouter::onChatFinished);
}

void ModelRouter::chatWithModel(const ModelRef &model,
                                const ChatRequest &request) {
  ChatResponse failure;
  if (m_chatReply) {
    failure.error = QStringLiteral("A request is already in flight.");
    emit finished(failure);
    return;
  }
  if (!m_registry) {
    failure.error = QStringLiteral("No provider registry configured.");
    emit finished(failure);
    return;
  }
  QString error;
  const ResolvedModel resolved = m_registry->resolve(model, &error);
  if (!resolved.ok) {
    failure.error = error;
    emit finished(failure);
    return;
  }

  m_resolved = resolved;
  m_useRegistry = true;
  m_activeProvider = resolved.displayName;
  emit providersChanged();
  m_retryAttempt = 0;
  m_pendingBody = buildBodyFor(request, resolved);
  m_pendingRequest = requestForResolved(resolved);
  m_chatReply = m_manager->post(
      m_pendingRequest,
      QJsonDocument(m_pendingBody).toJson(QJsonDocument::Compact));
  connect(m_chatReply, &QNetworkReply::finished, this,
          &ModelRouter::onChatFinished);
}

QNetworkRequest ModelRouter::requestForResolved(const ResolvedModel &resolved) const {
  QNetworkRequest request(QUrl(resolved.chatUrl));
  request.setHeader(QNetworkRequest::ContentTypeHeader,
                    QStringLiteral("application/json"));
  if (resolved.kind == ProviderKind::Anthropic) {
    request.setRawHeader("x-api-key", resolved.apiKey.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");
  } else if (!resolved.apiKey.isEmpty() &&
             resolved.kind != ProviderKind::Local) {
    request.setRawHeader("Authorization",
                         QByteArray("Bearer ") + resolved.apiKey.toUtf8());
    request.setRawHeader("HTTP-Referer", "https://templnote.local");
    request.setRawHeader("X-Title", "Templnote");
  }
  request.setTransferTimeout(60000);
  return request;
}

QJsonObject ModelRouter::buildBodyFor(const ChatRequest &request,
                                      const ResolvedModel &resolved) const {
  QJsonObject body;
  if (resolved.kind == ProviderKind::Anthropic) {
    QString systemText;
    body.insert(QStringLiteral("messages"),
                anthropicMessages(request.messages, &systemText));
    if (!systemText.isEmpty())
      body.insert(QStringLiteral("system"), systemText);
    if (!request.tools.isEmpty()) {
      QJsonArray tools;
      for (const QJsonValue &value : request.tools) {
        const QJsonObject t = value.toObject();
        const QJsonObject fn = t.value(QStringLiteral("function")).toObject();
        QJsonObject tool;
        tool.insert(QStringLiteral("name"), fn.value(QStringLiteral("name")));
        tool.insert(QStringLiteral("description"),
                    fn.value(QStringLiteral("description")));
        tool.insert(QStringLiteral("input_schema"),
                    fn.value(QStringLiteral("parameters")));
        tools.append(tool);
      }
      body.insert(QStringLiteral("tools"), tools);
    }
    body.insert(QStringLiteral("max_tokens"),
                request.maxTokens > 0 ? request.maxTokens : 2048);
  } else {
    body.insert(QStringLiteral("messages"), openAiMessages(request.messages));
    if (!request.tools.isEmpty()) {
      QJsonArray tools;
      for (const QJsonValue &value : request.tools) {
        const QJsonObject t = value.toObject();
        const QJsonObject fn = t.value(QStringLiteral("function")).toObject();
        if (fn.isEmpty()) {
          tools.append(t); // Ollama native tool format
          continue;
        }
        QJsonObject converted;
        converted.insert(QStringLiteral("type"), QStringLiteral("function"));
        converted.insert(QStringLiteral("function"), fn);
        tools.append(converted);
      }
      body.insert(QStringLiteral("tools"), tools);
      body.insert(QStringLiteral("tool_choice"), QStringLiteral("auto"));
    }
    if (request.maxTokens > 0)
      body.insert(QStringLiteral("max_tokens"), request.maxTokens);
  }
  body.insert(QStringLiteral("model"), resolved.modelId);
  if (resolved.kind != ProviderKind::Anthropic)
    body.insert(QStringLiteral("temperature"), request.temperature);
  body.insert(QStringLiteral("stream"), false);
  return body;
}

void ModelRouter::onChatFinished() {
  if (!m_chatReply)
    return;
  QNetworkReply *reply = m_chatReply;
  m_chatReply = nullptr;
  const QByteArray data = reply->readAll();
  const QString provider = m_activeProvider;
  const bool networkError = reply->error() != QNetworkReply::NoError;
  const bool cancelled = reply->error() == QNetworkReply::OperationCanceledError;
  const QString errorText = reply->errorString();
  const int status =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  reply->deleteLater();

  const bool retryable = networkError ? !cancelled : (status == 429 || status >= 500);
  if (retryable && m_retryAttempt < m_maxRetries) {
    scheduleRetry(networkError ? errorText
                               : QStringLiteral("HTTP %1").arg(status));
    return;
  }
  if (networkError || status >= 400) {
    ChatResponse response;
    response.error = networkError ? errorText
                                  : QStringLiteral("HTTP %1").arg(status);
    response.provider = provider;
    emit finished(response);
    return;
  }
  if (m_useRegistry)
    emit finished(parseResponseFor(data, m_resolved));
  else
    emit finished(parseResponse(data, provider));
}

ChatResponse ModelRouter::parseResponseFor(const QByteArray &data,
                                           const ResolvedModel &resolved) const {
  ChatResponse response;
  response.ok = true;
  response.provider = resolved.displayName;
  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject()) {
    response.ok = false;
    response.error = error.errorString();
    return response;
  }
  const QJsonObject object = doc.object();
  response.model = object.value(QStringLiteral("model")).toString(resolved.modelId);
  addUsage(&response, object, resolved.kind);
  if (resolved.kind == ProviderKind::Anthropic) {
    QJsonArray calls;
    for (const QJsonValue &value : object.value(QStringLiteral("content")).toArray()) {
      const QJsonObject block = value.toObject();
      const QString type = block.value(QStringLiteral("type")).toString();
      if (type == QLatin1String("text")) {
        response.text += block.value(QStringLiteral("text")).toString();
      } else if (type == QLatin1String("tool_use")) {
        QJsonObject fn;
        fn.insert(QStringLiteral("name"), block.value(QStringLiteral("name")));
        QJsonObject call;
        call.insert(QStringLiteral("id"), block.value(QStringLiteral("id")));
        call.insert(QStringLiteral("type"), QStringLiteral("function"));
        call.insert(QStringLiteral("function"), fn);
        call.insert(QStringLiteral("arguments"), block.value(QStringLiteral("input")));
        calls.append(call);
      }
    }
    response.toolCalls = calls;
    return response;
  }
  if (resolved.kind == ProviderKind::Local) {
    const QJsonObject message = object.value(QStringLiteral("message")).toObject();
    response.text = message.value(QStringLiteral("content")).toString();
    if (message.contains(QStringLiteral("tool_calls")))
      response.toolCalls = message.value(QStringLiteral("tool_calls")).toArray();
  } else {
    const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
    if (!choices.isEmpty()) {
      const QJsonObject message = choices.first().toObject()
                                      .value(QStringLiteral("message")).toObject();
      response.text = message.value(QStringLiteral("content")).toString();
      if (message.contains(QStringLiteral("tool_calls")))
        response.toolCalls = message.value(QStringLiteral("tool_calls")).toArray();
    }
  }
  return response;
}

ChatResponse ModelRouter::parseResponse(const QByteArray &data,
                                        const QString &provider) const {
  ChatResponse response;
  response.ok = true;
  response.provider = provider;
  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject()) {
    response.ok = false;
    response.error = error.errorString();
    return response;
  }
  const QJsonObject object = doc.object();
  response.model = object.value(QStringLiteral("model")).toString();
  addUsage(&response, object, provider == QLatin1String("local")
                                  ? ProviderKind::Local
                                  : ProviderKind::OpenAICompatible);
  if (provider == QLatin1String("local")) {
    const QJsonObject message = object.value(QStringLiteral("message")).toObject();
    response.text = message.value(QStringLiteral("content")).toString();
    if (message.contains(QStringLiteral("tool_calls")))
      response.toolCalls = message.value(QStringLiteral("tool_calls")).toArray();
  } else {
    const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
    if (!choices.isEmpty()) {
      const QJsonObject message = choices.first().toObject()
                                      .value(QStringLiteral("message")).toObject();
      response.text = message.value(QStringLiteral("content")).toString();
      if (message.contains(QStringLiteral("tool_calls")))
        response.toolCalls = message.value(QStringLiteral("tool_calls")).toArray();
    }
  }
  return response;
}

void ModelRouter::scheduleRetry(const QString &error) {
  ++m_retryAttempt;
  const int delay = 250 * (1 << qMin(m_retryAttempt - 1, 4));
  QTimer::singleShot(delay, this, &ModelRouter::retryNow);
  Q_UNUSED(error)
}

void ModelRouter::retryNow() {
  if (m_pendingBody.isEmpty())
    return;
  m_chatReply = m_manager->post(
      m_pendingRequest,
      QJsonDocument(m_pendingBody).toJson(QJsonDocument::Compact));
  connect(m_chatReply, &QNetworkReply::finished, this,
          &ModelRouter::onChatFinished);
}

} // namespace Templnote::AI
