#include "AgentSession.h"

#include "ai/AgentRegistry.h"
#include "ai/ProviderRegistry.h"
#include "ai/SessionManager.h"
#include "ai/SkillFile.h"
#include "ai/ToolSchema.h"
#include "engine/CapabilityGate.h"
#include "engine/ChangeFeed.h"
#include "engine/EditorSession.h"
#include "model/BlockModel.h"
#include "plugins/PluginManager.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTime>
#include <QUuid>
#include <QVariantMap>

namespace Templnote::AI {

AgentSession::AgentSession(::Templnote::BlockModel *model,
                           ::Templnote::PluginManager *plugins,
                           ProviderRegistry *registry, AgentRegistry *agents,
                           SessionManager *manager, const QString &sessionId,
                           QObject *parent)
    : QObject(parent), m_model(model), m_plugins(plugins), m_registry(registry),
      m_agents(agents), m_manager(manager), m_id(sessionId) {
  m_pipeline = m_manager->runner(sessionId);
  const SessionManager::SessionPorts ports = m_manager->ports(sessionId);
  m_documentPort = qobject_cast<Adapter::SessionDocumentPort *>(ports.document);
  m_changePort = dynamic_cast<Adapter::FeedChangePort *>(ports.changes);
  if (m_documentPort)
    m_preview = m_documentPort->session();
  if (m_changePort)
    m_feed = m_changePort->feed();

  m_pluginPort = new Adapter::PluginManagerToolPort(plugins, this);
  m_pipeline->setPluginTools(m_pluginPort);
  m_gate = new Engine::CapabilityGate(this);
  installGate();
  refreshAgentRuns();

  m_streamDebounce.setSingleShot(true);
  m_streamDebounce.setInterval(320);
  connect(&m_streamDebounce, &QTimer::timeout, this,
          &AgentSession::onStreamDebounce);
  m_streamTimer.setInterval(12);
  connect(&m_streamTimer, &QTimer::timeout, this, &AgentSession::onStreamTick);

  connect(m_pipeline, &PipelineRunner::runningChanged, this, [this] {
    emit runningChanged();
  });
  connect(m_pipeline, &PipelineRunner::agentChanged, this, [this] {
    const QString id = m_pipeline->currentAgent();
    if (!id.isEmpty() &&
        (m_agentHistory.isEmpty() || m_agentHistory.last() != id))
      m_agentHistory.append(id);
    emit agentChanged();
  });
  connect(m_pipeline, &PipelineRunner::agentChanged, this,
          &AgentSession::onPipelineAgentChanged);
  connect(m_pipeline, &PipelineRunner::agentFinished, this,
          &AgentSession::onPipelineAgentFinished);
  connect(m_documentPort, &Adapter::SessionDocumentPort::commandApplied, this,
          &AgentSession::onCommandApplied);
  connect(m_pipeline, &PipelineRunner::agentApplied, this,
          &AgentSession::onAgentApplied);
  connect(m_pipeline, &PipelineRunner::finished, this, &AgentSession::onFinished);
  connect(m_pipeline, &PipelineRunner::step, this, &AgentSession::onPipelineStep);
  connect(m_pipeline, &PipelineRunner::streamed, this, &AgentSession::onStreamed);
}

AgentSession::~AgentSession() = default;

void AgentSession::setInteractive(bool interactive) {
  if (m_interactive == interactive)
    return;
  m_interactive = interactive;
  if (!interactive) {
    // A session that was animating when the user switched away must stop
    // writing into the shared editor immediately.
    stopStreaming();
  }
}

void AgentSession::installGate() {
  QStringList trusted{"local-owner", "ai-agent"};
  if (m_agents)
    trusted.append(m_agents->agentIds());
  for (const QString &id : trusted)
    m_gate->setRole(id, Engine::CapabilityGate::Role::Owner);
  if (m_preview) {
    m_preview->setActor(Engine::Actor::human(QStringLiteral("local-owner")));
    m_preview->setTrustedActorIds(trusted);
    m_preview->setCapabilityGate(m_gate);
  }
}

QVector<AgentRun> AgentSession::buildAgentRuns(int *skipped) const {
  QVector<AgentRun> runs;
  if (skipped)
    *skipped = 0;
  const QString instructions = SkillFile::readOrCreate();
  if (m_agents) {
    for (const AgentDefinition &def : m_agents->activeAgentDefs()) {
      if (!def.document.trimmed().isEmpty()) {
        if (skipped)
          ++(*skipped);
        continue;
      }
      AgentRun run;
      run.id = def.id;
      run.instructions = def.instructions;
      run.enabled = true;
      run.instructionBlocks = {instructions};
      runs.append(run);
    }
  }
  if (runs.isEmpty()) {
    AgentRun run;
    run.id = QStringLiteral("ai-agent");
    run.instructionBlocks = {instructions};
    runs.append(run);
  }
  return runs;
}

void AgentSession::refreshAgentRuns() {
  if (m_manager)
    m_manager->setAgentRuns(buildAgentRuns(nullptr));
  installGate();
}

void AgentSession::setError(const QString &error) {
  m_lastError = error;
  emit lastErrorChanged(m_lastError);
}

bool AgentSession::running() const { return m_pipeline && m_pipeline->running(); }
QString AgentSession::currentAgent() const {
  return m_pipeline ? m_pipeline->currentAgent() : QString();
}
QString AgentSession::previewJson() const {
  return m_preview ? m_preview->documentJson() : QString();
}

QVariantList AgentSession::agentRuns() const {
  QVariantList out;
  const QString live = m_preview ? m_preview->documentJson() : m_originalJson;
  for (const AgentRecord &record : m_agentRecords) {
    const QString after = record.after.isEmpty() ? live : record.after;
    QVariantMap entry;
    entry.insert(QStringLiteral("id"), record.id);
    entry.insert(QStringLiteral("label"), record.name);
    entry.insert(QStringLiteral("edits"), record.editCount);
    entry.insert(QStringLiteral("changed"), record.changed);
    entry.insert(QStringLiteral("accepted"), record.accepted);
    entry.insert(QStringLiteral("running"), running() && record.id == m_activeAgentId);
    entry.insert(QStringLiteral("diff"), Adapter::diffDocuments(record.before, after));
    out.append(entry);
  }
  return out;
}

QVariantList AgentSession::pendingDiff() const {
  const QString current = m_preview ? m_preview->documentJson() : m_originalJson;
  return Adapter::diffDocuments(m_originalJson, current);
}

void AgentSession::onPipelineStep(const PipelineStep &step) {
  for (QVariant &entry : m_steps) {
    QVariantMap item = entry.toMap();
    item.insert(QStringLiteral("active"), false);
    entry = item;
  }
  QVariantMap item;
  item.insert(QStringLiteral("agent"), step.agentId);
  item.insert(QStringLiteral("label"), step.label);
  item.insert(QStringLiteral("kind"), step.kind);
  item.insert(QStringLiteral("active"), step.active);
  if (!step.detail.isEmpty())
    item.insert(QStringLiteral("detail"), step.detail);
  m_steps.append(item);
  emit stepsChanged();
}

void AgentSession::onStreamed(const StreamDelta &delta) {
  if (delta.type != QLatin1String("text") || delta.text.isEmpty())
    return;
  QVariantMap entry;
  if (!m_transcript.isEmpty()) {
    entry = m_transcript.last().toMap();
    if (entry.value(QStringLiteral("role")).toString() == QLatin1String("assistant") &&
        entry.value(QStringLiteral("streaming")).toBool()) {
      entry.insert(QStringLiteral("text"),
                   entry.value(QStringLiteral("text")).toString() + delta.text);
      m_transcript.replace(m_transcript.size() - 1, entry);
      emit transcriptChanged();
      return;
    }
  }
  entry.insert(QStringLiteral("role"), QStringLiteral("assistant"));
  entry.insert(QStringLiteral("text"), delta.text);
  entry.insert(QStringLiteral("agent"), agentName(delta.agentId));
  entry.insert(QStringLiteral("streaming"), true);
  entry.insert(QStringLiteral("time"),
               QTime::currentTime().toString(QStringLiteral("h:mm AP")));
  m_transcript.append(entry);
  emit transcriptChanged();
}

void AgentSession::clearTranscript() {
  if (m_transcript.isEmpty())
    return;
  m_transcript.clear();
  emit transcriptChanged();
}

void AgentSession::clearErrors() {
  if (!m_lastError.isEmpty())
    setError({});
}

void AgentSession::appendNotice(const QString &role, const QString &text,
                                const QString &kind) {
  if (text.trimmed().isEmpty())
    return;
  QVariantMap row;
  row.insert(QStringLiteral("role"), role);
  row.insert(QStringLiteral("text"), text.trimmed());
  row.insert(QStringLiteral("kind"), kind);
  row.insert(QStringLiteral("time"),
             QTime::currentTime().toString(QStringLiteral("h:mm AP")));
  row.insert(QStringLiteral("id"),
             QUuid::createUuid().toString(QUuid::WithoutBraces));
  m_transcript.append(row);
  emit transcriptChanged();
}

void AgentSession::addBackgroundNotice(const QString &text) {
  QVariantMap note;
  note.insert(QStringLiteral("role"), QStringLiteral("assistant"));
  note.insert(QStringLiteral("agent"), QStringLiteral("Background chat"));
  note.insert(QStringLiteral("text"), text);
  note.insert(QStringLiteral("time"),
              QTime::currentTime().toString(QStringLiteral("h:mm AP")));
  m_transcript.append(note);
  emit transcriptChanged();
}

void AgentSession::captureCaret() {
  m_caretBlockId = m_model ? m_model->lastHistoryBlockId() : QString();
  m_caretPosition = m_model ? m_model->lastHistoryCursorPosition() : -1;
}

void AgentSession::restoreCaret() {
  // The busy flag is owned by AgentService (any running session keeps the
  // editor read-only), not by an individual session.
  emit restoreEditorCaret(m_caretBlockId, m_caretPosition);
}

static QVector<QPair<QString, QString>> documentTexts(const QJsonObject &doc) {
  QVector<QPair<QString, QString>> out;
  const QJsonArray blocks = doc.value(QStringLiteral("blocks")).toArray();
  for (const QJsonValue &value : blocks) {
    const QJsonObject block = value.toObject();
    out.append({block.value(QStringLiteral("id")).toString(),
                block.value(QStringLiteral("text")).toString()});
  }
  return out;
}

void AgentSession::beginStreaming() {
  // A background chat must not write into the shared editor. Its preview and
  // review records are still complete; switching to it can show them later.
  if (!m_interactive) {
    m_streaming = false;
    m_streamSteps.clear();
    // Keep the starting snapshot so a later commit/rollback can still verify
    // that the live editor has not been changed underneath.
    m_streamTargetJson = m_preview ? m_preview->documentJson() : QString();
    return;
  }
  m_streaming = true;
  m_wasModified = m_model ? m_model->isModified() : false;
  m_streamSteps.clear();
  m_streamTargetJson = m_preview ? m_preview->documentJson() : QString();
}

void AgentSession::updateStreamTarget() {
  if (!m_streaming || !m_interactive || !m_model || !m_preview)
    return;
  m_streamTargetJson = m_preview->documentJson();
  m_streamDebounce.start();
}

void AgentSession::onStreamDebounce() { reconcileStreamTargets(); }

void AgentSession::reconcileStreamTargets() {
  if (!m_streaming || !m_interactive || !m_model || !m_preview)
    return;
  const auto target =
      documentTexts(QJsonDocument::fromJson(m_streamTargetJson.toUtf8()).object());
  const auto display = documentTexts(m_model->engineSession()->toJson());
  bool structural = target.size() != display.size();
  if (!structural) {
    for (int i = 0; i < target.size(); ++i) {
      if (target.at(i).first != display.at(i).first) {
        structural = true;
        break;
      }
    }
  }
  if (structural) {
    m_streamSteps.clear();
    m_model->applyRemoteDocument(
        QJsonDocument::fromJson(m_streamTargetJson.toUtf8()).object(),
        QJsonObject{});
    if (m_wasModified && !m_model->isModified())
      m_model->markAgentDocumentModified();
    return;
  }
  for (int i = 0; i < target.size(); ++i) {
    if (target.at(i).second == display.at(i).second)
      continue;
    const QString id = target.at(i).first;
    bool found = false;
    for (StreamStep &step : m_streamSteps) {
      if (step.blockId == id) {
        step.target = target.at(i).second;
        found = true;
        break;
      }
    }
    if (!found)
      m_streamSteps.append(StreamStep{id, display.at(i).second, target.at(i).second});
  }
  if (!m_streamSteps.isEmpty() && !m_streamTimer.isActive())
    m_streamTimer.start();
}

void AgentSession::onStreamTick() {
  if (!m_interactive) {
    stopStreaming();
    return;
  }
  if (m_streamSteps.isEmpty()) {
    m_streamTimer.stop();
    return;
  }
  StreamStep &step = m_streamSteps.first();
  // Advance a small batch per tick and emit one engine command for the batch.
  int budget = 24;
  bool changed = false;
  while (budget-- > 0 && step.current != step.target) {
    changed = true;
    int diff = 0;
    const int common = qMin(step.current.size(), step.target.size());
    while (diff < common && step.current.at(diff) == step.target.at(diff))
      ++diff;
    if (step.current.size() > step.target.size() &&
        step.current.mid(diff + 1) == step.target.mid(diff)) {
      step.current.remove(diff, 1);
    } else {
      step.current.insert(diff, step.target.at(diff));
    }
  }
  if (changed) {
    // One engine command per animation tick, not one per character.
    m_model->applyStreamedText(step.blockId, step.current);
  }
  if (step.current == step.target)
    m_streamSteps.removeFirst();
  if (m_streamSteps.isEmpty())
    m_streamTimer.stop();
}

void AgentSession::applyStreamTarget() {
  m_streamDebounce.stop();
  m_streamTimer.stop();
  m_streamSteps.clear();
  if (!m_model || !m_preview)
    return;
  m_model->applyRemoteDocument(m_preview->toJson(), agentPatchFromFeed());
  if (m_wasModified && !m_model->isModified())
    m_model->markAgentDocumentModified();
}

void AgentSession::stopStreaming() {
  m_streamDebounce.stop();
  m_streamTimer.stop();
  m_streamSteps.clear();
  m_streaming = false;
}

bool AgentSession::liveDocumentMatchesSnapshot() const {
  if (!m_model)
    return false;
  const QString current = QString::fromUtf8(
      QJsonDocument(m_model->engineSession()->toJson()).toJson(
          QJsonDocument::Compact));
  // It is safe to restore or mirror only when the editor still holds exactly
  // what this session streamed (or its untouched starting point).
  return current == m_streamTargetJson || current == m_originalJson;
}

void AgentSession::rollbackAndUnfreeze() {
  stopStreaming();
  if (m_model && !m_originalJson.isEmpty()) {
    if (liveDocumentMatchesSnapshot()) {
      const QJsonObject before =
          QJsonDocument::fromJson(m_originalJson.toUtf8()).object();
      m_model->applyAgentDocument(before, QJsonObject{});
    } else {
      // Another conversation edited the document meanwhile; reverting to our
      // older snapshot would silently discard their work.
      setError(QStringLiteral("Could not roll back: the document changed in "
                              "another conversation while this one ran."));
    }
  }
  restoreCaret();
}

void AgentSession::showPendingInEditor() {
  if (!m_hasPending || !m_model || !m_preview || m_interactive)
    return;
  if (!liveDocumentMatchesSnapshot() && !m_originalJson.isEmpty()) {
    setError(QStringLiteral("Switch back at a safe point: the document changed "
                            "since these edits were previewed."));
    return;
  }
  m_wasModified = m_model->isModified();
  m_model->applyRemoteDocument(m_preview->toJson(), agentPatchFromFeed());
  m_streamTargetJson = m_preview->documentJson();
  if (m_wasModified && !m_model->isModified())
    m_model->markAgentDocumentModified();
}

void AgentSession::sendPrompt(const QString &prompt) {
  const QString text = prompt.trimmed();
  if (text.isEmpty() || running() || !m_model || !m_preview)
    return;
  if (m_hasPending) {
    setError(QStringLiteral("Review or discard the pending AI changes first."));
    return;
  }
  if (m_registry && !m_registry->anyConnected()) {
    setError(QStringLiteral("Connect a provider in Settings before using AI."));
    return;
  }

  int skipped = 0;
  const QVector<AgentRun> runs = buildAgentRuns(&skipped);
  if (m_manager)
    m_manager->setAgentRuns(runs);

  m_preview->loadJson(m_model->engineSession()->toJson());
  m_originalJson = m_preview->documentJson();
  captureCaret();
  beginStreaming();
  if (m_feed)
    m_feed->clear();
  m_agentRecords.clear();
  m_activeAgentId.clear();
  m_stageBeforeJson.clear();
  m_dirty = false;
  m_lastError.clear();
  m_lastResult.clear();
  m_answers.clear();
  m_lastAnswer.clear();
  emit answerChanged();
  emit lastErrorChanged(m_lastError);
  m_agentHistory.clear();
  m_totalTokens = 0;
  m_estimatedCost = 0.0;
  emit usageChanged();

  QVariantMap userTurn;
  userTurn.insert(QStringLiteral("role"), QStringLiteral("user"));
  userTurn.insert(QStringLiteral("text"), text);
  userTurn.insert(QStringLiteral("time"),
                  QTime::currentTime().toString(QStringLiteral("h:mm AP")));
  m_transcript.append(userTurn);
  emit transcriptChanged();

  m_steps.clear();
  emit stepsChanged();
  emit promptStarted(text);
  emit pendingChanged();
  emit agentRunsChanged();
  m_runTimer.restart();
  // Go through the session manager so the inbox/coordinator own the busy
  // period, queuing, steering and cancellation.
  if (!m_manager->send(m_id, text)) {
    stopStreaming();
    setError(QStringLiteral("The conversation could not be started."));
  }

  if (skipped > 0)
    m_lastResult = QStringLiteral("%1 document-scoped agent(s) skipped; this "
                                  "document has a single active view.")
                       .arg(skipped);
}

void AgentSession::cancel() {
  if (m_manager)
    m_manager->cancel(m_id);
  else if (m_pipeline)
    m_pipeline->cancel();
}

void AgentSession::beginAgent(const QString &agentId, const QString &name) {
  m_activeAgentId = agentId;
  m_stageBeforeJson = m_preview ? m_preview->documentJson() : QString();
  if (agentIndex(agentId) < 0) {
    AgentRecord record;
    record.id = agentId;
    record.name = name;
    record.before = m_stageBeforeJson;
    m_agentRecords.append(record);
  }
}

void AgentSession::finalizeAgent() {
  if (m_activeAgentId.isEmpty())
    return;
  const int index = agentIndex(m_activeAgentId);
  if (index >= 0)
    m_agentRecords[index].after = m_preview ? m_preview->documentJson() : QString();
  m_activeAgentId.clear();
  m_stageBeforeJson.clear();
}

void AgentSession::finalizeActiveAgent() { finalizeAgent(); }

void AgentSession::onPipelineAgentChanged() {
  const QString id = m_pipeline->currentAgent();
  if (id.isEmpty() || id == m_activeAgentId)
    return;
  finalizeAgent();
  beginAgent(id, agentName(id));
  emit agentRunsChanged();
}

void AgentSession::onPipelineAgentFinished(const QString &agentId) {
  if (agentId != m_activeAgentId)
    return;
  finalizeAgent();
  emit agentRunsChanged();
}

void AgentSession::onCommandApplied(const QJsonObject &command,
                                    const QString &actorId,
                                    const QJsonObject &) {
  if (m_activeAgentId.isEmpty() || actorId != m_activeAgentId)
    return;
  const int index = agentIndex(m_activeAgentId);
  if (index < 0)
    return;
  AgentRecord &record = m_agentRecords[index];
  record.commands.append({command, actorId});
  ++record.editCount;
  record.changed = true;
  if (!m_hasPending) {
    m_hasPending = true;
    emit pendingChanged();
  }
  updateStreamTarget();
  emit agentRunsChanged();
}

void AgentSession::onAgentApplied(const QString &, const QJsonObject &) {
  m_dirty = true;
  updateStreamTarget();
}

void AgentSession::onFinished(const PipelineResult &result) {
  finalizeActiveAgent();
  if (m_interactive) {
    if (m_streaming && !result.ok) {
      rollbackAndUnfreeze();
    } else if (m_streaming) {
      applyStreamTarget();
      if (!m_dirty) {
        stopStreaming();
        restoreCaret();
      }
    }
  }

  m_totalTokens = result.promptTokens + result.completionTokens;
  m_estimatedCost = (result.promptTokens / 1e6 * 0.15) +
                    (result.completionTokens / 1e6 * 0.60);
  qInfo().nospace() << "[BENCHMARK] agent-run " << m_id.left(8) << ": "
                    << m_runTimer.elapsed() << " ms, "
                    << result.toolRounds << " rounds, " << m_totalTokens
                    << " tokens, compacted=" << result.compacted;
  emit usageChanged();
  if (!result.ok && !result.error.isEmpty())
    setError(result.error);

  // Drop any live streaming bubbles; the final answers replace them.
  for (int i = m_transcript.size() - 1; i >= 0; --i) {
    if (m_transcript.at(i).toMap().value(QStringLiteral("streaming")).toBool())
      m_transcript.removeAt(i);
  }
  m_answers.clear();
  for (const AgentResponse &response : result.responses) {
    if (response.text.trimmed().isEmpty())
      continue;
    QVariantMap entry;
    entry.insert(QStringLiteral("id"), response.agentId);
    entry.insert(QStringLiteral("label"), agentName(response.agentId));
    entry.insert(QStringLiteral("text"), response.text.trimmed());
    m_answers.append(entry);

    QVariantMap assistantTurn;
    assistantTurn.insert(QStringLiteral("role"), QStringLiteral("assistant"));
    assistantTurn.insert(QStringLiteral("text"), response.text.trimmed());
    assistantTurn.insert(QStringLiteral("agent"), agentName(response.agentId));
    assistantTurn.insert(
        QStringLiteral("time"),
        QTime::currentTime().toString(QStringLiteral("h:mm AP")));
    m_transcript.append(assistantTurn);
  }
  emit transcriptChanged();
  for (QVariant &entry : m_steps) {
    QVariantMap item = entry.toMap();
    item.insert(QStringLiteral("active"), false);
    entry = item;
  }
  emit stepsChanged();
  m_lastAnswer = result.answer();
  emit answerChanged();

  const int completed = result.completedAgents.size();
  const QString summary = QStringLiteral("%1 agent(s), %2 revision(s)%3")
                              .arg(completed)
                              .arg(result.revisionsAdvanced)
                              .arg(result.ok ? QString() : QStringLiteral(" (stopped)"));
  m_lastResult = m_lastResult.isEmpty() ? summary : m_lastResult + QStringLiteral(" • ") + summary;
  if (m_dirty) {
    m_hasPending = true;
    m_lastResult += QStringLiteral(" - pending review");
  }
  emit pendingChanged();
  emit agentRunsChanged();
  emit finishedChanged();
}

QString AgentSession::agentName(const QString &agentId) const {
  if (agentId == QLatin1String("ai-agent"))
    return QStringLiteral("Assistant");
  if (m_agents) {
    for (const AgentDefinition &def : m_agents->allAgents())
      if (def.id == agentId)
        return def.name;
  }
  return agentId;
}

int AgentSession::agentIndex(const QString &agentId) const {
  for (int i = 0; i < m_agentRecords.size(); ++i)
    if (m_agentRecords.at(i).id == agentId)
      return i;
  return -1;
}

void AgentSession::setAgentAccepted(int index, bool accepted) {
  if (index < 0 || index >= m_agentRecords.size())
    return;
  if (m_agentRecords[index].accepted == accepted)
    return;
  m_agentRecords[index].accepted = accepted;
  emit agentRunsChanged();
}

void AgentSession::clearReview() {
  m_agentRecords.clear();
  m_activeAgentId.clear();
  m_stageBeforeJson.clear();
  m_originalJson.clear();
  m_hasPending = false;
  m_dirty = false;
  if (m_feed)
    m_feed->clear();
  emit pendingChanged();
  emit agentRunsChanged();
}

QJsonObject AgentSession::agentPatchFromFeed() const {
  QJsonObject patchJson;
  if (!m_feed)
    return patchJson;
  const auto events = m_feed->events();
  if (!events.isEmpty()) {
    QJsonArray ops;
    for (const auto &event : events) {
      const QJsonObject patch = event.patch.toJson();
      const QJsonArray patchOps = patch.value(QStringLiteral("ops")).toArray();
      for (const QJsonValue &op : patchOps)
        ops.append(op);
    }
    patchJson.insert(QStringLiteral("ops"), ops);
    patchJson.insert(QStringLiteral("actor"), events.last().patch.actor.toJson());
    patchJson.insert(QStringLiteral("after"), events.last().patch.after.toJson());
  }
  return patchJson;
}

bool AgentSession::mirrorToEditor() {
  if (!m_model || !m_preview)
    return false;
  const bool ok =
      m_model->applyAgentDocument(m_preview->toJson(), agentPatchFromFeed());
  if (ok)
    m_dirty = false;
  return ok;
}

bool AgentSession::commit() {
  if (!m_hasPending || !m_model) {
    setError(QStringLiteral("There are no AI changes to commit."));
    return false;
  }
  if (running()) {
    setError(QStringLiteral("Wait for the agents to finish before committing."));
    return false;
  }
  if (!liveDocumentMatchesSnapshot()) {
    setError(QStringLiteral("The document changed since these edits were "
                            "previewed. Review again before committing."));
    return false;
  }
  bool allAccepted = true;
  for (const AgentRecord &record : m_agentRecords) {
    if (record.changed && !record.accepted) {
      allAccepted = false;
      break;
    }
  }
  bool ok = false;
  if (allAccepted) {
    ok = mirrorToEditor();
  } else {
    QVector<Adapter::StagedCommand> commands;
    for (const AgentRecord &record : m_agentRecords) {
      if (!record.changed || !record.accepted)
        continue;
      for (const Adapter::StagedCommand &command : record.commands)
        commands.append(command);
    }
    const Adapter::ReplayResult replay =
        Adapter::replayCommands(m_originalJson, commands, m_gate);
    if (!replay.ok) {
      setError(replay.error);
      rollbackAndUnfreeze();
      return false;
    }
    ok = m_model->applyAgentDocument(replay.document, replay.patch);
  }
  if (!ok) {
    setError(QStringLiteral("The AI changes could not be applied to the editor."));
    rollbackAndUnfreeze();
    return false;
  }
  stopStreaming();
  setError({});
  m_lastResult += QStringLiteral(" - committed");
  clearReview();
  restoreCaret();
  emit finishedChanged();
  return true;
}

void AgentSession::reject() {
  if (!m_hasPending)
    return;
  rollbackAndUnfreeze();
  clearReview();
  setError({});
  m_lastResult = QStringLiteral("AI changes discarded.");
  emit finishedChanged();
}

} // namespace Templnote::AI
