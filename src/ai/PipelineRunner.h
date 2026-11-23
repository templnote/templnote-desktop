#pragma once

// Multi-agent pipeline runner. Each stage runs a real tool-use loop: the model
// receives the stage prompt and the current document, its tool calls are
// validated and applied under a distinct actor id, and the results are fed back
// as tool messages until the model stops calling tools (or a budget is hit).
// Stages run sequentially over a revision chain.

#include "engine/EditorSession.h"
#include "ModelRouter.h"

namespace Templnote {
class PluginManager;
}
namespace Templnote::Engine {
class ChangeFeed;
}

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

namespace Templnote::AI {

namespace Engine = ::Templnote::Engine;

struct PipelineStage {
  QString id;
  QString systemPrompt;
  bool enabled = true;
};

struct PipelineResult {
  bool ok = false;
  int revisionsAdvanced = 0;
  QString error;
  QVector<QString> completedStages;
  int promptTokens = 0;
  int completionTokens = 0;
  int toolRounds = 0;
};

class PipelineRunner : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool running READ running NOTIFY runningChanged)
  Q_PROPERTY(QString currentStage READ currentStage NOTIFY stageChanged)

public:
  explicit PipelineRunner(QObject *parent = nullptr);

  void setSession(Engine::EditorSession *session) { m_session = session; }
  void setRouter(ModelRouter *router) { m_router = router; }
  void setPluginManager(::Templnote::PluginManager *manager) {
    m_pluginManager = manager;
  }
  // Supplies recent provenance so later stages see compact diffs in addition
  // to the current document snapshot.
  void setChangeFeed(Engine::ChangeFeed *feed) { m_feed = feed; }
  void setStages(const QVector<PipelineStage> &stages) { m_stages = stages; }

  // Budgets keep a runaway model from editing forever or burning tokens.
  void setMaxToolRounds(int rounds) { m_maxToolRounds = qMax(1, rounds); }
  void setMaxRevisions(int revisions) { m_maxRevisions = qMax(1, revisions); }

  static QVector<PipelineStage> defaultStages();

  bool running() const { return m_running; }
  QString currentStage() const { return m_currentStage; }

  void start(const QString &userPrompt);
  // Returns false when a run is already in flight, so the control API can
  // report a conflict instead of silently dropping the request.
  bool startAuthorised(const QString &userPrompt) {
    if (m_running)
      return false;
    start(userPrompt);
    return true;
  }
  void cancel();

signals:
  void runningChanged();
  void stageChanged();
  void finished(const PipelineResult &result);
  void stageApplied(const QString &stageId, const QJsonObject &patch);
  void stageFinished(const QString &stageId);

private:
  void finish(bool ok, const QString &error = {});
  void runNextStage();
  void sendStageRequest();
  void onChatFinished(const ChatResponse &response);
  int applyToolCalls(const QJsonArray &calls, const QString &stageId,
                     QJsonArray &results);
  QString documentContext(const QString &stageId) const;

  Engine::EditorSession *m_session = nullptr;
  ModelRouter *m_router = nullptr;
  ::Templnote::PluginManager *m_pluginManager = nullptr;
  Engine::ChangeFeed *m_feed = nullptr;
  QVector<PipelineStage> m_stages;
  QVector<PipelineStage> m_queue;
  QVector<ChatMessage> m_messages;
  QJsonArray m_tools;
  QString m_userPrompt;
  QString m_currentStage;
  bool m_running = false;
  bool m_cancelled = false;
  int m_revisionsAtStart = 0;
  int m_round = 0;
  int m_toolRounds = 0;
  int m_maxToolRounds = 4;
  int m_maxRevisions = 200;
  int m_promptTokens = 0;
  int m_completionTokens = 0;
  QVector<QString> m_completed;
};

} // namespace Templnote::AI
