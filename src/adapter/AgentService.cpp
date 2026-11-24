#include "AgentService.h"

#include "ai/ProviderRegistry.h"
#include "ai/SkillFile.h"
#include "engine/ChangeFeed.h"
#include "engine/EditorSession.h"
#include "model/BlockModel.h"
#include "plugins/PluginManager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTime>
#include <QUuid>
#include <QVariantMap>

namespace Templnote::AI {

AgentService::AgentService(::Templnote::BlockModel *model,
                           ::Templnote::PluginManager *plugins,
                           ProviderRegistry *registry, AgentRegistry *agents,
                           QObject *parent)
    : QObject(parent), m_model(model), m_plugins(plugins), m_registry(registry),
      m_agents(agents),
      m_transcriptModel({QStringLiteral("role"), QStringLiteral("text"),
                         QStringLiteral("agent"), QStringLiteral("time"),
                         QStringLiteral("streaming"), QStringLiteral("kind"),
                         QStringLiteral("status"), QStringLiteral("id")}),
      m_stepsModel({QStringLiteral("agent"), QStringLiteral("label"),
                    QStringLiteral("kind"), QStringLiteral("active"),
                    QStringLiteral("detail")}),
      m_sessionsModel({QStringLiteral("id"), QStringLiteral("title"),
                       QStringLiteral("running"), QStringLiteral("updated"),
                       QStringLiteral("model"), QStringLiteral("unread")}),
      m_subagentModel({QStringLiteral("id"), QStringLiteral("title"),
                       QStringLiteral("running"), QStringLiteral("status"),
                       QStringLiteral("updated")}),
      m_permissionModel({QStringLiteral("id"), QStringLiteral("action"),
                         QStringLiteral("resource"), QStringLiteral("state")}),
      m_skillModel({QStringLiteral("id"), QStringLiteral("name"),
                    QStringLiteral("description")}) {
  m_router = new ModelRouter(this);
  m_router->setRegistry(m_registry);
  m_store = new SessionStore(this);
  m_store->open();
  m_manager = new SessionManager(this);

  const auto portsForSession =
      [this](QObject *owner, const QString &) -> SessionManager::SessionPorts {
    auto *preview = new Engine::EditorSession(owner);
    preview->setLightweightHistory(true);
    auto *feed = new Engine::ChangeFeed(preview, owner);
    auto *document = new Adapter::SessionDocumentPort(preview, owner);
    auto *changes = new Adapter::FeedChangePort(feed, owner);
    // Retrieve from this session's own preview, never from the shared live
    // editor (another conversation may be streaming into it).
    auto *retrieval = new Adapter::LiveRetrievalPort(
        [preview] {
          return QString::fromUtf8(
              QJsonDocument(preview->toJson()).toJson(QJsonDocument::Compact));
        },
        owner);
    return {document, changes, retrieval};
  };

  QVector<AgentRun> runs;
  AgentRun fallback;
  fallback.id = QStringLiteral("ai-agent");
  runs.append(fallback);
  m_manager->configure(m_router, m_store, runs, BudgetOptions{},
                       SessionManager::PortFactory{portsForSession});

  // Non-blocking approval: an unapproved ask is refused with an actionable
  // error; the panel shows a request and the model's repair round retries it
  // once the user allows (or always allows) the action.
  m_manager->setAskHandler(
      [this](const QString &action, const QString &resource) {
        const QString id = QString::number(++m_approvalCounter);
        QVariantMap row;
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("action"), action);
        row.insert(QStringLiteral("resource"), resource);
        row.insert(QStringLiteral("state"), QStringLiteral("pending"));
        m_pendingApprovals.insert(id, qMakePair(action, resource));
        m_permissionModel.append(row);
        emit toolApprovalRequested(id, action, resource);
        emit permissionChanged();
        return false;
      });
  connect(m_manager, &SessionManager::compacted, this,
          &AgentService::onCompacted);
  connect(m_manager, &SessionManager::usageChanged, this,
          [this](const QString &sessionId) {
            if (sessionId == m_activeId)
              emit usageChanged();
          });
  refreshSkills();

  connect(m_registry, &ProviderRegistry::connectivityChanged, this,
          &AgentService::onRegistryChanged);
  connect(m_registry, &ProviderRegistry::modelsChanged, this,
          &AgentService::onRegistryChanged);
  connect(m_registry, &ProviderRegistry::error, this,
          &AgentService::onProviderError);
  connect(m_registry, &ProviderRegistry::connected, this,
          &AgentService::onProviderConnected);
  if (m_agents) {
    connect(m_agents, &AgentRegistry::agentsChanged, this,
            &AgentService::onAgentRegistryChanged);
    connect(m_agents, &AgentRegistry::activeAgentsChanged, this,
            &AgentService::onAgentRegistryChanged);
  }
  connect(m_manager, &SessionManager::finished, this,
          &AgentService::onSessionFinished);
  connect(m_manager, &SessionManager::sessionUpdated, this,
          &AgentService::onSessionUpdated);
  connect(m_manager, &SessionManager::backgroundResult, this,
          [this](const QString &sessionId, const QString &text) {
            if (sessionId == m_activeId && active())
              active()->addBackgroundNotice(text);
          });
  connect(m_manager, &SessionManager::sessionAdded, this,
          &AgentService::rebuildSessions);
  connect(m_manager, &SessionManager::sessionRemoved, this,
          &AgentService::rebuildSessions);

  loadSessions();
  if (m_activeId.isEmpty())
    createSession(QStringLiteral("New chat"));
}

AgentService::~AgentService() = default;

AgentSession *AgentService::active() const {
  auto it = m_sessions.constFind(m_activeId);
  return it == m_sessions.constEnd() ? nullptr : it.value();
}

void AgentService::bindSession(const QString &id) {
  for (const QMetaObject::Connection &connection : m_connections)
    disconnect(connection);
  m_connections.clear();
  AgentSession *session = active();
  if (!session)
    return;
  auto relay = [this, session](auto signal, auto slot) {
    m_connections.append(connect(session, signal, this, slot));
  };
  relay(&AgentSession::runningChanged, &AgentService::runningChanged);
  relay(&AgentSession::agentChanged, &AgentService::agentChanged);
  relay(&AgentSession::lastErrorChanged, &AgentService::lastErrorChanged);
  relay(&AgentSession::usageChanged, &AgentService::usageChanged);
  relay(&AgentSession::finishedChanged, &AgentService::finishedChanged);
  relay(&AgentSession::promptStarted, &AgentService::onPromptStarted);
  relay(&AgentSession::pendingChanged, &AgentService::pendingChanged);
  relay(&AgentSession::agentRunsChanged, &AgentService::agentRunsChanged);
  relay(&AgentSession::answerChanged, &AgentService::answerChanged);
  relay(&AgentSession::stepsChanged, &AgentService::onStepsChanged);
  relay(&AgentSession::transcriptChanged, &AgentService::onTranscriptChanged);
  relay(&AgentSession::restoreEditorCaret, &AgentService::restoreEditorCaret);
  m_connections.append(
      connect(session, &AgentSession::runningChanged, this,
              &AgentService::updateBusy));
  for (AgentSession *other : m_sessions)
    other->setInteractive(other == session);
  updateBusy();
  refreshModels();
}

void AgentService::refreshModels() {
  AgentSession *session = active();
  m_transcriptModel.setRows(session ? session->transcript() : QVariantList{});
  m_stepsModel.setRows(session ? session->steps() : QVariantList{});

  QVariantList subagents;
  const QVector<ChatSession> stored =
      m_store ? m_store->sessions() : QVector<ChatSession>{};
  for (const ChatSession &entry : stored) {
    if (entry.parentID != m_activeId)
      continue;
    QVariantMap row;
    row.insert(QStringLiteral("id"), entry.id);
    row.insert(QStringLiteral("title"), entry.title);
    PipelineRunner *runner = m_manager ? m_manager->runner(entry.id) : nullptr;
    row.insert(QStringLiteral("running"), runner && runner->running());
    row.insert(QStringLiteral("status"),
               entry.running ? QStringLiteral("running")
                             : QStringLiteral("idle"));
    row.insert(QStringLiteral("updated"), entry.updatedMs);
    subagents.append(row);
  }
  m_subagentModel.setRows(subagents);
  rebuildSessions();
}

void AgentService::onPromptStarted() {
  if (m_compacted) {
    m_compacted = false;
    emit compactedChanged();
  }
  emit promptStarted(QString());
}

void AgentService::onTranscriptChanged() {
  AgentSession *session = active();
  const QVariantList rows = session ? session->transcript() : QVariantList{};
  const QVariantList old = m_transcriptModel.rows();
  if (rows.size() < old.size()) {
    m_transcriptModel.setRows(rows);
  } else if (rows.size() == old.size() && rows != old) {
    for (int i = 0; i < rows.size(); ++i)
      if (old.at(i) != rows.at(i))
        m_transcriptModel.updateRow(i, rows.at(i).toMap());
  } else {
    for (int i = old.size(); i < rows.size(); ++i)
      m_transcriptModel.append(rows.at(i).toMap());
    for (int i = 0; i < qMin(old.size(), rows.size()); ++i) {
      const QVariantMap before = old.at(i).toMap();
      const QVariantMap after = rows.at(i).toMap();
      if (before != after)
        m_transcriptModel.updateRow(i, after);
    }
  }
  emit transcriptChanged();
}

void AgentService::onStepsChanged() {
  AgentSession *session = active();
  const QVariantList rows = session ? session->steps() : QVariantList{};
  const QVariantList old = m_stepsModel.rows();
  if (rows.size() < old.size()) {
    m_stepsModel.setRows(rows);
  } else if (rows.size() == old.size() && rows != old) {
    for (int i = 0; i < rows.size(); ++i)
      if (old.at(i) != rows.at(i))
        m_stepsModel.updateRow(i, rows.at(i).toMap());
  } else {
    for (int i = old.size(); i < rows.size(); ++i)
      m_stepsModel.append(rows.at(i).toMap());
    for (int i = 0; i < qMin(old.size(), rows.size()); ++i) {
      const QVariantMap before = old.at(i).toMap();
      const QVariantMap after = rows.at(i).toMap();
      if (before != after)
        m_stepsModel.updateRow(i, after);
    }
  }
  emit stepsChanged();
}

void AgentService::updateBusy() {
  if (!m_model)
    return;
  bool anyRunning = false;
  for (AgentSession *session : m_sessions)
    anyRunning = anyRunning || (session && session->running());
  m_model->setAgentBusy(anyRunning);
}

void AgentService::setActiveSession(const QString &id) {
  if (m_activeId == id && active())
    return;
  m_activeId = id;
  bindSession(id);
  emit activeSessionChanged();
  emit runningChanged();
  emit agentChanged();
  emit pendingChanged();
  emit agentRunsChanged();
  emit answerChanged();
  emit stepsChanged();
  emit transcriptChanged();
  emit usageChanged();
}

void AgentService::rebuildSessions() {
  QVariantList out;
  const QVector<ChatSession> stored = m_store->sessions();
  for (const ChatSession &session : stored) {
    QVariantMap entry;
    entry.insert(QStringLiteral("id"), session.id);
    entry.insert(QStringLiteral("title"), session.title);
    entry.insert(QStringLiteral("running"),
                 m_manager->runner(session.id) &&
                     m_manager->runner(session.id)->running());
    entry.insert(QStringLiteral("updated"), session.updatedMs);
    entry.insert(QStringLiteral("model"), session.modelRef);
    // A stored compaction/report summary is a useful visual hint.
    const int unread = session.summary.isEmpty() ? 0 : 1;
    entry.insert(QStringLiteral("unread"), unread);
    out.append(entry);
  }
  m_sessionsList = out;
  m_sessionsModel.setRows(out);
  emit sessionsChanged();
}

QString AgentService::createSession(const QString &title) {
  const QString documentPath = m_model ? m_model->currentFilePath() : QString();
  const QString id = m_manager->createSession(title, documentPath);
  auto *session = new AgentSession(m_model, m_plugins, m_registry, m_agents,
                                   m_manager, id, this);
  connect(session, &AgentSession::runningChanged, this,
          &AgentService::updateBusy);
  m_sessions.insert(id, session);
  // A new chat must become visible immediately, even when other sessions
  // already exist (the QML "New conversation" button relies on this).
  setActiveSession(id);
  rebuildSessions();
  return id;
}

bool AgentService::switchSession(const QString &sessionId) {
  if (!m_sessions.contains(sessionId))
    return false;
  setActiveSession(sessionId);
  // Bring a background chat's reviewed preview into the editor when switching
  // back to it, provided the document has not changed underneath.
  m_sessions.value(sessionId)->showPendingInEditor();
  updateBusy();
  return true;
}

bool AgentService::removeSession(const QString &sessionId) {
  if (m_sessions.isEmpty())
    return false;
  if (m_sessions.size() == 1)
    return false; // keep at least one chat available
  if (!m_sessions.contains(sessionId))
    return false;
  if (m_activeId == sessionId) {
    // Switch away before deleting so active() never points at a dead object.
    for (const QString &id : m_sessions.keys()) {
      if (id != sessionId) {
        setActiveSession(id);
        break;
      }
    }
  }
  m_manager->remove(sessionId);
  delete m_sessions.take(sessionId);
  rebuildSessions();
  return true;
}

void AgentService::renameSession(const QString &sessionId,
                                 const QString &title) {
  if (!m_manager->rename(sessionId, title))
    return;
  if (auto *widget = m_sessions.value(sessionId))
    widget->setTitle(title);
  rebuildSessions();
}

void AgentService::loadSessions() {
  const QVector<ChatSession> stored = m_store->sessions();
  for (const ChatSession &session : stored) {
    if (m_sessions.contains(session.id))
      continue;
    if (!m_manager->loadSession(session.id))
      continue;
    auto *widget = new AgentSession(m_model, m_plugins, m_registry, m_agents,
                                    m_manager, session.id, this);
    connect(widget, &AgentSession::runningChanged, this,
            &AgentService::updateBusy);
    widget->setTitle(session.title);
    m_sessions.insert(session.id, widget);
  }
  if (!m_sessions.isEmpty()) {
    // Prefer the most recently updated conversation, not a QHash-order key.
    const QString recent =
        stored.isEmpty() ? QString() : stored.first().id;
    setActiveSession(m_sessions.contains(recent) ? recent
                                                 : m_sessions.keys().value(0));
  }
  rebuildSessions();
}

void AgentService::onSessionFinished(const QString &sessionId,
                                     const PipelineResult &result) {
  // Background chats report into the active conversation instead of stealing
  // focus: the user sees that work finished and can switch to review it.
  ChatSession child;
  const bool isSubagent =
      m_store->load(sessionId, &child) && !child.parentID.isEmpty();
  AgentSession *current = active();
  if (current && sessionId != m_activeId && result.ok && !isSubagent) {
    QVariantMap note;
    note.insert(QStringLiteral("role"), QStringLiteral("assistant"));
    note.insert(QStringLiteral("agent"), QStringLiteral("Background chat"));
    note.insert(QStringLiteral("text"),
                QStringLiteral("Conversation \"%1\" finished: %2")
                    .arg(sessionId.left(8), result.answer().left(240)));
    current->addBackgroundNotice(note.value(QStringLiteral("text")).toString());
  }
  rebuildSessions();
}

void AgentService::onSessionUpdated(const QString &) { rebuildSessions(); }

void AgentService::onProviderError(const QString &message) {
  if (m_connectionError == message)
    return;
  m_connectionError = message;
  emit connectionErrorChanged();
}

void AgentService::onProviderConnected(const QString &) {
  if (m_connectionError.isEmpty())
    return;
  m_connectionError.clear();
  emit connectionErrorChanged();
}

void AgentService::onRegistryChanged() { emit readyChanged(); }

void AgentService::onAgentRegistryChanged() {
  for (AgentSession *session : m_sessions)
    session->refreshAgentRuns();
}

bool AgentService::running() const { return active() && active()->running(); }
QString AgentService::currentAgent() const {
  return active() ? active()->currentAgent() : QString();
}
QString AgentService::lastError() const {
  return active() ? active()->lastError() : m_connectionError;
}
bool AgentService::ready() const { return m_registry && m_registry->anyConnected(); }
int AgentService::totalTokens() const { return active() ? active()->totalTokens() : 0; }
double AgentService::estimatedCostUsd() const {
  return active() ? active()->estimatedCostUsd() : 0.0;
}
QString AgentService::lastResult() const {
  return active() ? active()->lastResult() : QString();
}
QStringList AgentService::agentHistory() const {
  return active() ? active()->agentHistory() : QStringList();
}
bool AgentService::hasPending() const { return active() && active()->hasPending(); }
QVariantList AgentService::agentRuns() const {
  return active() ? active()->agentRuns() : QVariantList{};
}
QVariantList AgentService::pendingDiff() const {
  return active() ? active()->pendingDiff() : QVariantList{};
}
QVariantList AgentService::answers() const {
  return active() ? active()->answers() : QVariantList{};
}
QString AgentService::lastAnswer() const {
  return active() ? active()->lastAnswer() : QString();
}
QVariantList AgentService::steps() const {
  return active() ? active()->steps() : QVariantList{};
}
QVariantList AgentService::transcript() const {
  return active() ? active()->transcript() : QVariantList{};
}
QVariantList AgentService::sessions() const { return m_sessionsList; }
QString AgentService::activeSessionTitle() const {
  return active() ? active()->title() : QString();
}

void AgentService::clearTranscript() {
  if (active())
    active()->clearTranscript();
}
QString AgentService::previewJson() const {
  return active() ? active()->previewJson() : QString();
}
QString AgentService::originalJson() const {
  return active() ? active()->originalJson() : QString();
}
bool AgentService::commit() { return active() && active()->commit(); }
void AgentService::reject() {
  if (active())
    active()->reject();
}
void AgentService::setAgentAccepted(int index, bool accepted) {
  if (active())
    active()->setAgentAccepted(index, accepted);
}
void AgentService::clearErrors() {
  if (active())
    active()->clearErrors();
  if (!m_connectionError.isEmpty()) {
    m_connectionError.clear();
    emit connectionErrorChanged();
  }
}
void AgentService::sendPrompt(const QString &prompt) {
  if (active())
    active()->sendPrompt(prompt);
  else
    createSession();
}
void AgentService::cancel() {
  if (active())
    active()->cancel();
}

void AgentService::onCompacted(const QString &sessionId,
                               const QString &summary) {
  if (sessionId != m_activeId)
    return;
  m_compacted = true;
  emit compactedChanged();
  if (active())
    active()->appendNotice(QStringLiteral("system"),
                           QStringLiteral("Context compacted: ") + summary,
                           QStringLiteral("compaction"));
}

bool AgentService::steerCurrent(const QString &text) {
  if (text.trimmed().isEmpty() || m_activeId.isEmpty())
    return false;
  if (!m_manager->steer(m_activeId, text.trimmed()))
    return false;
  if (active())
    active()->appendNotice(QStringLiteral("user"), text.trimmed(),
                           QStringLiteral("steer"));
  return true;
}

void AgentService::approveTool(const QString &requestId, bool always) {
  const auto it = m_pendingApprovals.constFind(requestId);
  if (it == m_pendingApprovals.constEnd())
    return;
  // The ask handler does not carry a session id, so apply the saved approval
  // to every runner; a later retry in any conversation then succeeds.
  if (m_manager) {
    for (const QString &id : m_manager->sessionIds()) {
      PipelineRunner *runner = m_manager->runner(id);
      if (!runner || !runner->policy())
        continue;
      runner->policy()->remember(it.value().first, it.value().second);
      if (always)
        runner->policy()->remember(it.value().first, QStringLiteral("*"));
    }
  }
  m_pendingApprovals.remove(requestId);
  removePermissionRow(requestId);
  emit permissionChanged();
}

void AgentService::denyTool(const QString &requestId) {
  m_pendingApprovals.remove(requestId);
  removePermissionRow(requestId);
  emit permissionChanged();
}

void AgentService::removePermissionRow(const QString &requestId) {
  QVariantList rows = m_permissionModel.rows();
  for (int i = rows.size() - 1; i >= 0; --i) {
    if (rows.at(i).toMap().value(QStringLiteral("id")).toString() == requestId) {
      rows.removeAt(i);
      break;
    }
  }
  m_permissionModel.setRows(rows);
}

QVariantMap AgentService::firstPermission() const {
  return m_permissionModel.row(0);
}

int AgentService::pendingPermissionCount() const {
  return m_pendingApprovals.size();
}

bool AgentService::loadSkill(const QString &id) {
  PipelineRunner *runner = m_manager ? m_manager->runner(m_activeId) : nullptr;
  if (!runner || !runner->loadSkill(id))
    return false;
  const SkillInfo skill = m_manager->skills()->get(id);
  if (active())
    active()->appendNotice(QStringLiteral("system"),
                           QStringLiteral("Loaded skill: ") +
                               (skill.name.isEmpty() ? id : skill.name),
                           QStringLiteral("skill"));
  return true;
}

void AgentService::refreshSkills() {
  if (m_manager && m_manager->skills())
    m_manager->skills()->refresh();
  QVariantList rows;
  if (m_manager && m_manager->skills()) {
    for (const SkillInfo &skill : m_manager->skills()->available()) {
      QVariantMap row;
      row.insert(QStringLiteral("id"), skill.id);
      row.insert(QStringLiteral("name"), skill.name);
      row.insert(QStringLiteral("description"), skill.description);
      rows.append(row);
    }
  }
  m_skillModel.setRows(rows);
}

void AgentService::acceptAll() {
  if (!active())
    return;
  const QVariantList runs = active()->agentRuns();
  for (int i = 0; i < runs.size(); ++i)
    active()->setAgentAccepted(i, true);
  emit agentRunsChanged();
}

void AgentService::rejectAll() {
  if (!active())
    return;
  const QVariantList runs = active()->agentRuns();
  for (int i = 0; i < runs.size(); ++i)
    active()->setAgentAccepted(i, false);
  emit agentRunsChanged();
}

int AgentService::liveTokens() const {
  PipelineRunner *runner = m_manager ? m_manager->runner(m_activeId) : nullptr;
  if (!runner)
    return active() ? active()->totalTokens() : 0;
  return runner->session().promptTokens + runner->session().completionTokens;
}

double AgentService::liveCostUsd() const {
  PipelineRunner *runner = m_manager ? m_manager->runner(m_activeId) : nullptr;
  if (!runner)
    return active() ? active()->estimatedCostUsd() : 0.0;
  const ChatSession &session = runner->session();
  return (session.promptTokens / 1e6 * 0.15) +
         (session.completionTokens / 1e6 * 0.60);
}

} // namespace Templnote::AI
