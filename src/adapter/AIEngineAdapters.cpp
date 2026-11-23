#include "AIEngineAdapters.h"

#include "engine/EditorSession.h"
#include "engine/Types.h"
#include "plugins/PluginManager.h"

namespace Templnote::Adapter {

SessionDocumentPort::SessionDocumentPort(Engine::EditorSession *session,
                                         QObject *parent)
    : AI::DocumentPort(parent), m_session(session) {}

int SessionDocumentPort::revision() const {
  return m_session ? m_session->revision() : 0;
}

QString SessionDocumentPort::documentJson() const {
  return m_session ? m_session->documentJson() : QStringLiteral("{}");
}

bool SessionDocumentPort::applyCommand(const QJsonObject &command,
                                       const QString &actorId,
                                       QJsonObject *patch, QString *error) {
  if (!m_session) {
    if (error)
      *error = QStringLiteral("No open document.");
    return false;
  }
  QString commandError;
  Engine::Command cmd = Engine::Command::fromJson(command, &commandError);
  if (!commandError.isEmpty()) {
    if (error)
      *error = commandError;
    return false;
  }
  // The actor is stamped by the host, never by the model or HTTP caller.
  cmd.actor = Engine::Actor::pipeline(actorId, actorId);
  const auto result = m_session->apply(cmd);
  if (!result.ok()) {
    if (error)
      *error = result.error();
    return false;
  }
  if (patch)
    *patch = result.value().toJson();
  return true;
}

FeedChangePort::FeedChangePort(Engine::ChangeFeed *feed, QObject *parent)
    : QObject(parent), m_feed(feed) {}

QJsonArray FeedChangePort::toJson(const QVector<Engine::ChangeFeed::Event> &events) {
  QJsonArray array;
  for (const auto &event : events)
    array.append(event.patch.toJson());
  return array;
}

QJsonArray FeedChangePort::all() const {
  return m_feed ? toJson(m_feed->events()) : QJsonArray();
}

QJsonArray FeedChangePort::recent(int count) const {
  if (!m_feed)
    return {};
  const QVector<Engine::ChangeFeed::Event> events = m_feed->recent(count);
  QJsonArray array;
  for (const auto &event : events) {
    QJsonObject entry;
    entry.insert(QStringLiteral("actor"), event.patch.actor.id);
    entry.insert(QStringLiteral("stage"), event.patch.actor.displayName);
    entry.insert(QStringLiteral("revision"), event.patch.revision);
    entry.insert(QStringLiteral("ops"),
                 event.patch.toJson().value(QStringLiteral("ops")).toArray());
    array.append(entry);
  }
  return array;
}

PluginManagerToolPort::PluginManagerToolPort(PluginManager *manager,
                                             QObject *parent)
    : QObject(parent), m_manager(manager) {}

QJsonObject PluginManagerToolPort::applyPluginTool(const QJsonObject &call) {
  return m_manager ? m_manager->applyPluginTool(call) : QJsonObject{};
}

} // namespace Templnote::Adapter
