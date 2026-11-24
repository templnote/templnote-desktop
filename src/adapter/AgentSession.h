#pragma once

// Desktop-side state for one AI conversation.
//
// Owns the preview document, change feed, review records and transcript for a
// single session. AgentService keeps one AgentSession active in the UI while
// the others keep running in the background through SessionManager.

#include "AIEngineAdapters.h"
#include "ai/PipelineRunner.h"
#include "ai/SessionManager.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVector>

namespace Templnote {
class BlockModel;
class PluginManager;
} // namespace Templnote

namespace Templnote::Engine {
class CapabilityGate;
class ChangeFeed;
class EditorSession;
} // namespace Templnote::Engine

namespace Templnote::AI {

class ProviderRegistry;
class AgentRegistry;
class SessionManager;

class AgentSession : public QObject {
  Q_OBJECT
public:
  struct AgentRecord {
    QString id;
    QString name;
    QString before;
    QString after;
    QVector<Adapter::StagedCommand> commands;
    int editCount = 0;
    bool changed = false;
    bool accepted = true;
  };

  struct StreamStep {
    QString blockId;
    QString current;
    QString target;
  };

  AgentSession(::Templnote::BlockModel *model,
               ::Templnote::PluginManager *plugins, ProviderRegistry *registry,
               AgentRegistry *agents, SessionManager *manager,
               const QString &sessionId, QObject *parent = nullptr);
  ~AgentSession() override;

  QString id() const { return m_id; }
  QString title() const { return m_title; }
  void setTitle(const QString &title) { m_title = title; }

  bool running() const;
  QString currentAgent() const;
  QString lastError() const { return m_lastError; }
  QString lastResult() const { return m_lastResult; }
  bool hasPending() const { return m_hasPending; }
  QVariantList agentRuns() const;
  QVariantList pendingDiff() const;
  QVariantList answers() const { return m_answers; }
  QString lastAnswer() const { return m_lastAnswer; }
  QVariantList steps() const { return m_steps; }
  QVariantList transcript() const { return m_transcript; }
  QString previewJson() const;
  QString originalJson() const { return m_originalJson; }
  int totalTokens() const { return m_totalTokens; }
  double estimatedCostUsd() const { return m_estimatedCost; }
  QStringList agentHistory() const { return m_agentHistory; }

  // Only the active conversation may animate edits into the live editor.
  // Background sessions still run concurrently but keep their work in their
  // own preview until the user switches to them.
  void setInteractive(bool interactive);
  bool interactive() const { return m_interactive; }
  void showPendingInEditor();

  void setAgentAccepted(int index, bool accepted);
  void refreshAgentRuns();
  void addBackgroundNotice(const QString &text);
  void appendNotice(const QString &role, const QString &text,
                    const QString &kind = QString());
  void clearErrors();
  void clearTranscript();
  bool commit();
  void reject();

public slots:
  void sendPrompt(const QString &prompt);
  void cancel();

signals:
  void runningChanged();
  void agentChanged();
  void lastErrorChanged(const QString &error);
  void usageChanged();
  void finishedChanged();
  void promptStarted(const QString &prompt);
  void pendingChanged();
  void agentRunsChanged();
  void answerChanged();
  void stepsChanged();
  void transcriptChanged();
  void restoreEditorCaret(const QString &blockId, int position);

private slots:
  void onAgentApplied(const QString &agentId, const QJsonObject &patch);
  void onPipelineAgentChanged();
  void onPipelineAgentFinished(const QString &agentId);
  void onCommandApplied(const QJsonObject &command, const QString &actorId,
                        const QJsonObject &patch);
  void onFinished(const PipelineResult &result);
  void onPipelineStep(const PipelineStep &step);
  void onStreamed(const StreamDelta &delta);
  void onStreamDebounce();
  void onStreamTick();

private:
  void installGate();
  QVector<AgentRun> buildAgentRuns(int *skipped) const;
  void captureCaret();
  void restoreCaret();
  void beginStreaming();
  void updateStreamTarget();
  void reconcileStreamTargets();
  void applyStreamTarget();
  void stopStreaming();
  void rollbackAndUnfreeze();
  void setError(const QString &error);
  QJsonObject agentPatchFromFeed() const;
  bool liveDocumentMatchesSnapshot() const;
  QString agentName(const QString &agentId) const;
  bool mirrorToEditor();
  int agentIndex(const QString &agentId) const;
  void beginAgent(const QString &agentId, const QString &name);
  void finalizeAgent();
  void finalizeActiveAgent();
  void clearReview();

  ::Templnote::BlockModel *m_model = nullptr;
  ::Templnote::PluginManager *m_plugins = nullptr;
  ProviderRegistry *m_registry = nullptr;
  AgentRegistry *m_agents = nullptr;
  SessionManager *m_manager = nullptr;
  QString m_id;
  QString m_title;
  PipelineRunner *m_pipeline = nullptr;
  Engine::EditorSession *m_preview = nullptr;
  Engine::ChangeFeed *m_feed = nullptr;
  Engine::CapabilityGate *m_gate = nullptr;
  Adapter::SessionDocumentPort *m_documentPort = nullptr;
  Adapter::FeedChangePort *m_changePort = nullptr;
  Adapter::PluginManagerToolPort *m_pluginPort = nullptr;

  int m_totalTokens = 0;
  double m_estimatedCost = 0.0;
  QString m_lastResult;
  QString m_lastError;
  QStringList m_agentHistory;
  bool m_hasPending = false;
  bool m_dirty = false;

  QString m_originalJson;
  QString m_caretBlockId;
  int m_caretPosition = -1;
  bool m_wasModified = false;
  bool m_streaming = false;
  QString m_streamTargetJson;
  QTimer m_streamDebounce;
  QTimer m_streamTimer;
  QVector<StreamStep> m_streamSteps;
  QString m_lastAnswer;
  QVariantList m_answers;
  QVariantList m_steps;
  QVariantList m_transcript;
  QString m_activeAgentId;
  QString m_stageBeforeJson;
  QVector<AgentRecord> m_agentRecords;
  QElapsedTimer m_runTimer;
  bool m_interactive = false;
};

} // namespace Templnote::AI
