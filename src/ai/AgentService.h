#pragma once

// In-process AI service for the desktop application.
//
// The agent never writes into the open editor model directly. It works on a
// preview EditorSession seeded from the model's live document, then mirrors the
// resulting snapshot through BlockModel::applyAgentDocument(). This keeps the
// QML delegates, caret and autosave path unchanged while giving the agent a
// real, gated session with full provenance.

#include "PipelineRunner.h"

#include <QObject>
#include <QString>
#include <QStringList>

namespace Templnote {
class BlockModel;
class PluginManager;
} // namespace Templnote

namespace Templnote::Engine {
class CapabilityGate;
class ChangeFeed;
} // namespace Templnote::Engine

namespace Templnote::AI {

class ProviderRegistry;

class AgentService : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool running READ running NOTIFY runningChanged)
  Q_PROPERTY(QString currentStage READ currentStage NOTIFY stageChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
  Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
  Q_PROPERTY(int totalTokens READ totalTokens NOTIFY usageChanged)
  Q_PROPERTY(double estimatedCostUsd READ estimatedCostUsd NOTIFY usageChanged)
  Q_PROPERTY(QString lastResult READ lastResult NOTIFY finishedChanged)

public:
  explicit AgentService(::Templnote::BlockModel *model,
                        ::Templnote::PluginManager *plugins,
                        Templnote::AI::ProviderRegistry *registry,
                        QObject *parent = nullptr);

  Templnote::AI::ProviderRegistry *registry() const { return m_registry; }

  bool running() const;
  QString currentStage() const;
  QString lastError() const;
  bool ready() const;
  int totalTokens() const { return m_totalTokens; }
  double estimatedCostUsd() const { return m_estimatedCost; }
  QString lastResult() const { return m_lastResult; }
  QStringList stageHistory() const { return m_stageHistory; }

  // The preview document, useful for UI diffing before committing a result.
  Q_INVOKABLE QString previewJson() const;

public slots:
  void sendPrompt(const QString &prompt);
  void cancel();

signals:
  void runningChanged();
  void stageChanged();
  void lastErrorChanged(const QString &error);
  void readyChanged();
  void usageChanged();
  void finishedChanged();
  void promptStarted(const QString &prompt);

private slots:
  void onStageApplied(const QString &stageId, const QJsonObject &patch);
  void onFinished(const PipelineResult &result);
  void onRegistryChanged();

private:
  void installGate();
  void mirrorToEditor();

  ::Templnote::BlockModel *m_model = nullptr;
  ::Templnote::PluginManager *m_plugins = nullptr;
  ProviderRegistry *m_registry = nullptr;
  ModelRouter *m_router = nullptr;
  PipelineRunner *m_pipeline = nullptr;
  Engine::EditorSession *m_preview = nullptr;
  Engine::ChangeFeed *m_feed = nullptr;
  Engine::CapabilityGate *m_gate = nullptr;

  int m_totalTokens = 0;
  double m_estimatedCost = 0.0;
  QString m_lastResult;
  QString m_lastError;
  QStringList m_stageHistory;
  bool m_running = false;
  bool m_dirty = false;
};

} // namespace Templnote::AI
