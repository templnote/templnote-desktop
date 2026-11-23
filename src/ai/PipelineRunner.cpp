#include "PipelineRunner.h"

#include "ToolSchema.h"
#include "engine/ChangeFeed.h"
#include "plugins/PluginManager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

namespace Templnote::AI {

PipelineRunner::PipelineRunner(QObject *parent) : QObject(parent) {}

QVector<PipelineStage> PipelineRunner::defaultStages() {
  QVector<PipelineStage> stages;
  stages.append({QStringLiteral("write"),
                 QStringLiteral("You are a writer. Edit the document using "
                                 "the provided tools to fulfill the user's "
                                 "request. Make all changes by calling tools; "
                                 "never describe edits in prose."),
                 true});
  stages.append({QStringLiteral("grammar-1"),
                 QStringLiteral("You are a grammar checker. Fix spelling and "
                                 "grammar errors using edit tools only."),
                 true});
  stages.append({QStringLiteral("humanizer"),
                 QStringLiteral("You are an editor. Make the text sound "
                                 "natural and human using edit tools only."),
                 true});
  stages.append({QStringLiteral("grammar-2"),
                 QStringLiteral("You are a proofreader. Do a final grammar "
                                 "and consistency pass with edit tools."),
                 true});
  stages.append({QStringLiteral("validation"),
                 QStringLiteral("You are a validator. Verify the document "
                                 "satisfies the request; fix any remaining "
                                 "issues with edit tools."),
                 true});
  return stages;
}

void PipelineRunner::start(const QString &userPrompt) {
  if (m_running || !m_session || !m_router)
    return;
  m_running = true;
  m_cancelled = false;
  m_userPrompt = userPrompt;
  m_queue = m_stages;
  m_completed.clear();
  m_revisionsAtStart = m_session->revision();
  m_promptTokens = 0;
  m_completionTokens = 0;
  m_toolRounds = 0;
  emit runningChanged();
  connect(m_router, &ModelRouter::finished, this,
          &PipelineRunner::onChatFinished, Qt::UniqueConnection);
  runNextStage();
}

void PipelineRunner::cancel() {
  if (!m_running)
    return;
  m_cancelled = true;
  m_queue.clear();
  m_currentStage.clear();
  emit stageChanged();
}

void PipelineRunner::finish(bool ok, const QString &error) {
  PipelineResult result;
  result.ok = ok && !m_cancelled;
  result.error = error;
  result.revisionsAdvanced = m_session ? m_session->revision() - m_revisionsAtStart : 0;
  result.completedStages = m_completed;
  result.promptTokens = m_promptTokens;
  result.completionTokens = m_completionTokens;
  result.toolRounds = m_toolRounds;
  m_running = false;
  m_currentStage.clear();
  emit stageChanged();
  emit runningChanged();
  emit finished(result);
}

void PipelineRunner::runNextStage() {
  if (m_cancelled || m_queue.isEmpty()) {
    finish(true);
    return;
  }
  if (m_session && m_session->revision() - m_revisionsAtStart >= m_maxRevisions) {
    finish(false, QStringLiteral("Revision budget reached; stopping the pipeline."));
    return;
  }

  const PipelineStage stage = m_queue.takeFirst();
  if (!stage.enabled) {
    runNextStage();
    return;
  }
  m_currentStage = stage.id;
  emit stageChanged();

  m_messages.clear();
  m_messages.append({QStringLiteral("system"), stage.systemPrompt});
  m_messages.append({QStringLiteral("system"), documentContext(stage.id)});
  if (stage.id != QStringLiteral("write"))
    m_messages.append(
        {QStringLiteral("system"),
         QStringLiteral("Earlier stages already edited the document. Only "
                        "make further changes if they are still needed.")});
  m_messages.append({QStringLiteral("user"), m_userPrompt});
  m_round = 0;

  sendStageRequest();
}

QString PipelineRunner::documentContext(const QString &stageId) const {
  Q_UNUSED(stageId)
  // The full document is sent once per stage so the model has exact block ids
  // and offsets. Tool results between rounds carry the deltas, so this is not
  // repeated on every tool round. A compact provenance tail tells later stages
  // which blocks were already touched without shipping the whole journal.
  QString context = QStringLiteral("Current document JSON: ") +
                    (m_session ? m_session->documentJson() : QStringLiteral("{}"));
  if (m_feed && !m_feed->events().isEmpty()) {
    QJsonArray changes;
    for (const auto &event : m_feed->recent(12)) {
      QJsonObject entry;
      entry.insert(QStringLiteral("actor"), event.patch.actor.id);
      entry.insert(QStringLiteral("stage"), event.patch.actor.displayName);
      entry.insert(QStringLiteral("revision"), event.patch.revision);
      QJsonArray ops;
      for (const QJsonValue &op : event.patch.toJson()
                                      .value(QStringLiteral("ops")).toArray())
        ops.append(op);
      entry.insert(QStringLiteral("ops"), ops);
      changes.append(entry);
    }
    context += QStringLiteral("\nRecent changes (newest last): ") +
               QString::fromUtf8(QJsonDocument(changes).toJson(
                   QJsonDocument::Compact));
  }
  return context;
}

void PipelineRunner::sendStageRequest() {
  ChatRequest request;
  request.messages = m_messages;
  request.temperature = 0.4;
  m_tools = ToolSchema::definitions();
  if (m_pluginManager) {
    const QJsonArray pluginTools = ToolSchema::pluginDefinitions();
    for (const QJsonValue &tool : pluginTools)
      m_tools.append(tool);
  }
  request.tools = m_tools;
  m_router->chat(request);
}

void PipelineRunner::onChatFinished(const ChatResponse &response) {
  if (!m_running)
    return;
  m_promptTokens += response.promptTokens;
  m_completionTokens += response.completionTokens;

  if (m_cancelled) {
    finish(false);
    return;
  }
  if (!response.ok) {
    finish(false, response.error);
    return;
  }

  if (!response.toolCalls.isEmpty() && m_round < m_maxToolRounds) {
    // Assistant turn: keep the exact tool calls so the next request is a valid
    // OpenAI/Anthropic/Ollama tool-use conversation.
    ChatMessage assistant;
    assistant.role = QStringLiteral("assistant");
    assistant.content = response.text;
    assistant.toolCalls = response.toolCalls;
    m_messages.append(assistant);

    QJsonArray results;
    const int applied = applyToolCalls(response.toolCalls, m_currentStage, results);
    for (const QJsonValue &value : results) {
      const QJsonObject result = value.toObject();
      ChatMessage toolMessage;
      toolMessage.role = QStringLiteral("tool");
      toolMessage.toolCallId = result.value(QStringLiteral("id")).toString();
      toolMessage.name = result.value(QStringLiteral("name")).toString();
      toolMessage.content = result.value(QStringLiteral("content")).toString();
      m_messages.append(toolMessage);
    }

    ++m_round;
    ++m_toolRounds;
    // A stage that applied no valid tools is finished: asking again would just
    // burn tokens re-reading the same document.
    if (applied == 0) {
      m_completed.append(m_currentStage);
      emit stageFinished(m_currentStage);
      runNextStage();
      return;
    }
    if (m_session && m_session->revision() - m_revisionsAtStart >= m_maxRevisions) {
      finish(false, QStringLiteral("Revision budget reached; stopping the pipeline."));
      return;
    }
    sendStageRequest();
    return;
  }

  m_completed.append(m_currentStage);
  emit stageFinished(m_currentStage);
  runNextStage();
}

int PipelineRunner::applyToolCalls(const QJsonArray &calls,
                                   const QString &stageId,
                                   QJsonArray &results) {
  int applied = 0;
  int index = 0;
  for (const QJsonValue &value : calls) {
    const QJsonObject call = value.toObject();
    const QJsonObject fn = call.value(QStringLiteral("function")).toObject();
    const QString toolName = call.value(QStringLiteral("name")).toString(
        fn.value(QStringLiteral("name")).toString());
    const QString callId = call.value(QStringLiteral("id")).toString(
        QStringLiteral("call-%1").arg(index++));

    // Only these plugin tools are available to a model. Approval is a user
    // action in Settings and must never be reachable from an agent.
    static const QStringList kPluginTools{
        QStringLiteral("createPlugin"), QStringLiteral("writePluginFile"),
        QStringLiteral("validatePlugin"), QStringLiteral("rescanPlugins")};
    if (m_pluginManager && kPluginTools.contains(toolName)) {
      const QJsonObject result = m_pluginManager->applyPluginTool(call);
      const bool ok = result.value(QStringLiteral("ok")).toBool();
      QJsonObject feedback;
      feedback.insert(QStringLiteral("id"), callId);
      feedback.insert(QStringLiteral("name"), toolName);
      feedback.insert(
          QStringLiteral("content"),
          ok ? QStringLiteral("ok")
             : result.value(QStringLiteral("error")).toString(
                   QStringLiteral("tool failed")));
      results.append(feedback);
      if (ok)
        ++applied;
      continue;
    }

    QString error;
    auto parsed = ToolSchema::fromToolCall(call, &error);
    if (!error.isEmpty() || !parsed.ok()) {
      QJsonObject feedback;
      feedback.insert(QStringLiteral("id"), callId);
      feedback.insert(QStringLiteral("name"), toolName);
      feedback.insert(QStringLiteral("content"), error.isEmpty()
                                                     ? QStringLiteral("invalid tool call")
                                                     : error);
      results.append(feedback);
      continue;
    }
    Engine::Command stamped = parsed.value();
    // The actor is assigned by the trusted pipeline, never by the model.
    stamped.actor = Engine::Actor::pipeline(stageId, stageId);
    const auto result = m_session->apply(stamped);
    QJsonObject feedback;
    feedback.insert(QStringLiteral("id"), callId);
    feedback.insert(QStringLiteral("name"), toolName);
    if (result.ok()) {
      feedback.insert(QStringLiteral("content"), QStringLiteral("applied"));
      emit stageApplied(stageId, result.value().toJson());
      ++applied;
    } else {
      feedback.insert(QStringLiteral("content"), result.error());
    }
    results.append(feedback);
  }
  return applied;
}

} // namespace Templnote::AI
