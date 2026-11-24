#pragma once

// Engine-side implementations of the AI layer's ports. These are the only
// bridge between templnote-ai and the document engine / plugin manager; the AI
// library itself knows nothing about either.

#include "ai/AIPorts.h"
#include "engine/ChangeFeed.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

#include <functional>

namespace Templnote {
class PluginManager;
namespace Engine {
class CapabilityGate;
class ChangeFeed;
class EditorSession;
} // namespace Engine
} // namespace Templnote

namespace Templnote::Adapter {

class SessionDocumentPort : public AI::DocumentPort {
  Q_OBJECT
public:
  explicit SessionDocumentPort(Engine::EditorSession *session,
                               QObject *parent = nullptr);

  int revision() const override;
  QString documentJson() const override;
  bool applyCommand(const QJsonObject &command, const QString &actorId,
                    QJsonObject *patch, QString *error) override;

  Engine::EditorSession *session() const { return m_session; }

signals:
  // Emitted after an agent command has been validated and applied. The review
  // layer records these so it can replay only the stages the user accepts.
  void commandApplied(const QJsonObject &command, const QString &actorId,
                      const QJsonObject &patch);

private:
  Engine::EditorSession *m_session = nullptr;
};

// A validated tool command together with the pipeline stage that produced it.
struct StagedCommand {
  QJsonObject command;
  QString stageId;
};

struct ReplayResult {
  bool ok = false;
  QString error;
  QJsonObject document;
  QJsonObject patch; // {ops: [...], actor, after}
};

// Replay accepted stages onto a fresh session seeded from `originalJson`.
// This is what lets the user accept some stages and reject others without
// touching the live editor until they explicitly commit.
ReplayResult replayCommands(const QString &originalJson,
                            const QVector<StagedCommand> &commands,
                            Engine::CapabilityGate *gate = nullptr);

// Human label for an agent id when no configured name is available.
QString agentLabel(const QString &agentId);

// Block-level diff of two serialized documents. Returns QML-friendly maps:
// {id, kind: added|removed|changed, before, after, type}.
QVariantList diffDocuments(const QString &beforeJson,
                           const QString &afterJson);

class FeedChangePort : public QObject, public AI::ChangePort {
  Q_OBJECT
public:
  explicit FeedChangePort(Engine::ChangeFeed *feed, QObject *parent = nullptr);

  QJsonArray all() const override;
  QJsonArray recent(int count) const override;

  Engine::ChangeFeed *feed() const { return m_feed; }

private:
  static QJsonArray toJson(const QVector<Engine::ChangeFeed::Event> &events);

  Engine::ChangeFeed *m_feed = nullptr;
};

// Read-only retrieval over the live document. Keeps AI prompts proportional to
// the number of matching blocks instead of the size of the whole note.
class LiveRetrievalPort : public QObject, public AI::RetrievalPort {
  Q_OBJECT
public:
  explicit LiveRetrievalPort(std::function<QString()> documentJson,
                             QObject *parent = nullptr);

  QJsonArray relevantBlocks(const QString &query, int limit) const override;
  QJsonObject block(const QString &id) const override;

private:
  std::function<QString()> m_documentJson;
};

class PluginManagerToolPort : public QObject, public AI::PluginToolPort {
  Q_OBJECT
public:
  explicit PluginManagerToolPort(PluginManager *manager,
                                 QObject *parent = nullptr);

  QJsonObject applyPluginTool(const QJsonObject &call) override;

private:
  PluginManager *m_manager = nullptr;
};

} // namespace Templnote::Adapter
