#include "AgentService.h"

#include "engine/CapabilityGate.h"
#include "engine/ChangeFeed.h"
#include "engine/EditorSession.h"
#include "model/BlockModel.h"
#include "plugins/PluginManager.h"
#include "ai/ProviderRegistry.h"
#include "ai/SecretStore.h"
#include "ai/ToolSchema.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Templnote::AI {

AgentService::AgentService(::Templnote::BlockModel *model,
                           ::Templnote::PluginManager *plugins,
                           ProviderRegistry *registry, QObject *parent)
    : QObject(parent), m_model(model), m_plugins(plugins), m_registry(registry) {
  m_router = new ModelRouter(this);
  m_router->setRegistry(m_registry);
  m_pipeline = new PipelineRunner(this);
  m_pipeline->setRouter(m_router);
  m_pluginPort = new Adapter::PluginManagerToolPort(plugins, this);
  m_pipeline->setPluginTools(m_pluginPort);
  m_pipeline->setStages(PipelineRunner::defaultStages());

  m_preview = new Engine::EditorSession(this);
  m_preview->setLightweightHistory(true);
  m_feed = new Engine::ChangeFeed(m_preview, this);
  m_documentPort = new Adapter::SessionDocumentPort(m_preview, this);
  m_changePort = new Adapter::FeedChangePort(m_feed, this);
  m_pipeline->setDocument(m_documentPort);
  m_pipeline->setChangeFeed(m_changePort);
  m_gate = new Engine::CapabilityGate(this);
  installGate();

  connect(m_pipeline, &PipelineRunner::runningChanged, this, [this] {
    if (m_running == m_pipeline->running())
      return;
    m_running = m_pipeline->running();
    emit runningChanged();
  });
  connect(m_pipeline, &PipelineRunner::stageChanged, this, [this] {
    if (m_stageHistory.isEmpty() ||
        m_stageHistory.last() != m_pipeline->currentStage()) {
      if (!m_pipeline->currentStage().isEmpty())
        m_stageHistory.append(m_pipeline->currentStage());
    }
    emit stageChanged();
  });
  connect(m_pipeline, &PipelineRunner::stageApplied, this,
          &AgentService::onStageApplied);
  connect(m_pipeline, &PipelineRunner::finished, this,
          &AgentService::onFinished);
  connect(m_registry, &ProviderRegistry::connectivityChanged, this,
          &AgentService::onRegistryChanged);
  connect(m_registry, &ProviderRegistry::modelsChanged, this,
          &AgentService::onRegistryChanged);
}

void AgentService::installGate() {
  // These ids are assigned by our own pipeline, never by a remote caller, so
  // granting them owner rights does not create an escalation path.
  const QStringList trusted{"local-owner", "ai-agent", "write", "grammar-1",
                            "humanizer", "grammar-2", "validation"};
  for (const QString &id : trusted)
    m_gate->setRole(id, Engine::CapabilityGate::Role::Owner);
  m_preview->setActor(Engine::Actor::human(QStringLiteral("local-owner")));
  m_preview->setCapabilityGate(m_gate);
}

bool AgentService::running() const { return m_running; }
QString AgentService::currentStage() const { return m_pipeline->currentStage(); }
QString AgentService::lastError() const { return m_lastError; }
bool AgentService::ready() const { return m_registry->anyConnected(); }

QString AgentService::previewJson() const {
  return m_preview ? m_preview->documentJson() : QString{};
}

void AgentService::onRegistryChanged() { emit readyChanged(); }

void AgentService::sendPrompt(const QString &prompt) {
  const QString text = prompt.trimmed();
  if (text.isEmpty() || m_running)
    return;
  if (!m_model || !m_preview) {
    m_lastError = QStringLiteral("No open document.");
    emit lastErrorChanged(m_lastError);
    return;
  }
  if (!ready()) {
    m_lastError = QStringLiteral("Connect a provider in Settings before using AI.");
    emit lastErrorChanged(m_lastError);
    return;
  }

  // Seed the preview from the editor's *live* document (not from disk) so
  // unsaved user edits are included and nothing is lost.
  m_preview->loadJson(m_model->engineSession()->toJson());
  m_feed->clear();
  m_dirty = false;
  m_lastError.clear();
  m_stageHistory.clear();
  m_totalTokens = 0;
  m_estimatedCost = 0.0;
  emit lastErrorChanged(m_lastError);
  emit usageChanged();
  emit promptStarted(text);
  m_pipeline->start(text);
}

void AgentService::cancel() { m_pipeline->cancel(); }

void AgentService::onStageApplied(const QString &, const QJsonObject &) {
  m_dirty = true;
}

void AgentService::onFinished(const PipelineResult &result) {
  if (m_dirty)
    mirrorToEditor();

  m_totalTokens = result.promptTokens + result.completionTokens;
  // Rough public list-price estimate; the UI labels it as an estimate.
  m_estimatedCost = (result.promptTokens / 1e6 * 0.15) +
                    (result.completionTokens / 1e6 * 0.60);
  emit usageChanged();

  if (!result.ok && !result.error.isEmpty()) {
    m_lastError = result.error;
    emit lastErrorChanged(m_lastError);
  }

  QStringList completed;
  for (const QString &stage : result.completedStages)
    completed.append(stage);
  m_lastResult = QStringLiteral("%1 stage(s), %2 revision(s)%3")
                     .arg(completed.size())
                     .arg(result.revisionsAdvanced)
                     .arg(result.ok ? QString() : QStringLiteral(" (stopped)"));
  emit finishedChanged();
}

void AgentService::mirrorToEditor() {
  QJsonObject patchJson;
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
    patchJson.insert(QStringLiteral("actor"),
                     events.last().patch.actor.toJson());
    patchJson.insert(QStringLiteral("after"),
                     events.last().patch.after.toJson());
  }
  m_model->applyAgentDocument(m_preview->toJson(), patchJson);
  m_dirty = false;
}

} // namespace Templnote::AI
