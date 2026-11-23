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
#include <QVector>

namespace Templnote {
class PluginManager;
namespace Engine {
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

private:
  Engine::EditorSession *m_session = nullptr;
};

class FeedChangePort : public QObject, public AI::ChangePort {
  Q_OBJECT
public:
  explicit FeedChangePort(Engine::ChangeFeed *feed, QObject *parent = nullptr);

  QJsonArray all() const override;
  QJsonArray recent(int count) const override;

private:
  static QJsonArray toJson(const QVector<Engine::ChangeFeed::Event> &events);

  Engine::ChangeFeed *m_feed = nullptr;
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
