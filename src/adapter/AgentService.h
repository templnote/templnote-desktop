#pragma once

// In-process AI service for the desktop application, now with first-class
// concurrent conversations.
//
// Each conversation owns its own preview document, change feed, review state
// and transcript (see AgentSession). SessionManager runs them concurrently on
// one router; the properties below always reflect the active conversation, so
// existing QML bindings keep working. New QML can list and switch sessions.

#include "AgentSession.h"
#include "PanelListModel.h"
#include "ai/AgentRegistry.h"
#include "ai/PipelineRunner.h"
#include "ai/SessionManager.h"
#include "ai/SessionStore.h"

#include <QHash>
#include <QJsonObject>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

namespace Templnote {
class BlockModel;
class PluginManager;
} // namespace Templnote

namespace Templnote::AI {

class ProviderRegistry;

class AgentService : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool running READ running NOTIFY runningChanged)
  Q_PROPERTY(QString currentAgent READ currentAgent NOTIFY agentChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
  Q_PROPERTY(QString connectionError READ connectionError NOTIFY
                 connectionErrorChanged)
  Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
  Q_PROPERTY(int totalTokens READ totalTokens NOTIFY usageChanged)
  Q_PROPERTY(double estimatedCostUsd READ estimatedCostUsd NOTIFY usageChanged)
  Q_PROPERTY(QString lastResult READ lastResult NOTIFY finishedChanged)
  Q_PROPERTY(bool hasPending READ hasPending NOTIFY pendingChanged)
  Q_PROPERTY(QVariantList agentRuns READ agentRuns NOTIFY agentRunsChanged)
  Q_PROPERTY(QVariantList pendingDiff READ pendingDiff NOTIFY pendingChanged)
  Q_PROPERTY(QVariantList answers READ answers NOTIFY answerChanged)
  Q_PROPERTY(QString lastAnswer READ lastAnswer NOTIFY answerChanged)
  Q_PROPERTY(QVariantList steps READ steps NOTIFY stepsChanged)
  Q_PROPERTY(QVariantList transcript READ transcript NOTIFY transcriptChanged)
  // Multi-session surface.
  Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
  Q_PROPERTY(QString activeSessionId READ activeSessionId NOTIFY
                 activeSessionChanged)
  Q_PROPERTY(QString activeSessionTitle READ activeSessionTitle NOTIFY
                 activeSessionChanged)
  // Panel models: incremental, delegate-friendly replacements for the value lists.
  Q_PROPERTY(PanelListModel *transcriptModel READ transcriptModel CONSTANT)
  Q_PROPERTY(PanelListModel *stepsModel READ stepsModel CONSTANT)
  Q_PROPERTY(PanelListModel *sessionsModel READ sessionsModel CONSTANT)
  Q_PROPERTY(PanelListModel *subagentModel READ subagentModel CONSTANT)
  Q_PROPERTY(PanelListModel *permissionModel READ permissionModel CONSTANT)
  Q_PROPERTY(PanelListModel *skillModel READ skillModel CONSTANT)
  Q_PROPERTY(bool compacted READ compacted NOTIFY compactedChanged)
  Q_PROPERTY(int liveTokens READ liveTokens NOTIFY usageChanged)
  Q_PROPERTY(double liveCostUsd READ liveCostUsd NOTIFY usageChanged)
  Q_PROPERTY(QVariantMap firstPermission READ firstPermission NOTIFY permissionChanged)
  Q_PROPERTY(int pendingPermissionCount READ pendingPermissionCount NOTIFY
                 permissionChanged)

public:
  explicit AgentService(::Templnote::BlockModel *model,
                        ::Templnote::PluginManager *plugins,
                        ProviderRegistry *registry, AgentRegistry *agents,
                        QObject *parent = nullptr);
  ~AgentService() override;

  Templnote::AI::ProviderRegistry *registry() const { return m_registry; }
  Templnote::AI::AgentRegistry *agentRegistry() const { return m_agents; }
  SessionManager *sessionManager() const { return m_manager; }
  SessionStore *sessionStore() const { return m_store; }

  bool running() const;
  QString currentAgent() const;
  QString lastError() const;
  QString connectionError() const { return m_connectionError; }
  bool ready() const;
  int totalTokens() const;
  double estimatedCostUsd() const;
  QString lastResult() const;
  QStringList agentHistory() const;
  bool hasPending() const;
  QVariantList agentRuns() const;
  QVariantList pendingDiff() const;
  QVariantList answers() const;
  QString lastAnswer() const;
  QVariantList steps() const;
  QVariantList transcript() const;
  QVariantList sessions() const;
  QString activeSessionId() const { return m_activeId; }
  QString activeSessionTitle() const;

  Q_INVOKABLE void clearTranscript();
  Q_INVOKABLE QString previewJson() const;
  Q_INVOKABLE QString originalJson() const;
  Q_INVOKABLE bool commit();
  Q_INVOKABLE void reject();
  Q_INVOKABLE void setAgentAccepted(int index, bool accepted);
  Q_INVOKABLE void clearErrors();
  Q_INVOKABLE QString createSession(const QString &title = QString());
  Q_INVOKABLE bool switchSession(const QString &sessionId);
  Q_INVOKABLE bool removeSession(const QString &sessionId);
  Q_INVOKABLE void renameSession(const QString &sessionId,
                                 const QString &title);
  Q_INVOKABLE void loadSessions();
  Q_INVOKABLE bool steerCurrent(const QString &text);
  Q_INVOKABLE void approveTool(const QString &requestId, bool always = false);
  Q_INVOKABLE void denyTool(const QString &requestId);
  Q_INVOKABLE bool loadSkill(const QString &id);
  Q_INVOKABLE void refreshSkills();
  Q_INVOKABLE void acceptAll();
  Q_INVOKABLE void rejectAll();

  PanelListModel *transcriptModel() { return &m_transcriptModel; }
  PanelListModel *stepsModel() { return &m_stepsModel; }
  PanelListModel *sessionsModel() { return &m_sessionsModel; }
  PanelListModel *subagentModel() { return &m_subagentModel; }
  PanelListModel *permissionModel() { return &m_permissionModel; }
  PanelListModel *skillModel() { return &m_skillModel; }
  bool compacted() const { return m_compacted; }
  int liveTokens() const;
  double liveCostUsd() const;
  QVariantMap firstPermission() const;
  int pendingPermissionCount() const;

public slots:
  void sendPrompt(const QString &prompt);
  void cancel();

signals:
  void runningChanged();
  void agentChanged();
  void lastErrorChanged(const QString &error);
  void connectionErrorChanged();
  void readyChanged();
  void usageChanged();
  void finishedChanged();
  void promptStarted(const QString &prompt);
  void pendingChanged();
  void agentRunsChanged();
  void answerChanged();
  void stepsChanged();
  void transcriptChanged();
  void restoreEditorCaret(const QString &blockId, int position);
  void sessionsChanged();
  void activeSessionChanged();
  void compactedChanged();
  void toolApprovalRequested(const QString &requestId, const QString &action,
                             const QString &resource);
  void permissionChanged();

private slots:
  void onProviderError(const QString &message);
  void onProviderConnected(const QString &providerId);
  void onRegistryChanged();
  void onAgentRegistryChanged();
  void onSessionFinished(const QString &sessionId, const PipelineResult &result);
  void onSessionUpdated(const QString &sessionId);
  void onCompacted(const QString &sessionId, const QString &summary);
  void onPromptStarted();
  void onTranscriptChanged();
  void onStepsChanged();
  void updateBusy();
  void refreshModels();
  void removePermissionRow(const QString &requestId);

private:
  AgentSession *active() const;
  void bindSession(const QString &id);
  void setActiveSession(const QString &id);
  void rebuildSessions();

  ::Templnote::BlockModel *m_model = nullptr;
  ::Templnote::PluginManager *m_plugins = nullptr;
  ProviderRegistry *m_registry = nullptr;
  AgentRegistry *m_agents = nullptr;
  ModelRouter *m_router = nullptr;
  SessionStore *m_store = nullptr;
  SessionManager *m_manager = nullptr;
  QHash<QString, AgentSession *> m_sessions;
  QList<QMetaObject::Connection> m_connections;
  QVariantList m_sessionsList;
  PanelListModel m_transcriptModel;
  PanelListModel m_stepsModel;
  PanelListModel m_sessionsModel;
  PanelListModel m_subagentModel;
  PanelListModel m_permissionModel;
  PanelListModel m_skillModel;
  QHash<QString, QPair<QString, QString>> m_pendingApprovals;
  int m_approvalCounter = 0;
  bool m_compacted = false;
  QString m_activeId;
  QString m_connectionError;
};

} // namespace Templnote::AI
