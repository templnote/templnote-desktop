#include "ChangeFeed.h"
#include "EditorSession.h"

#include <algorithm>

namespace Templnote::Engine {

ChangeFeed::ChangeFeed(EditorSession *session, QObject *parent)
    : QObject(parent), m_session(session) {
  if (!m_session)
    return;
  connect(m_session, &EditorSession::patchApplied, this,
          [this](const Patch &patch) {
            Event event{patch, false};
            m_events.append(event);
            while (m_events.size() > kMaxEvents)
              m_events.removeFirst();
            emit patchSeen(patch, false);
            if (patch.actor.isAgent())
              emit agentChange(patch);
            emit feedChanged();
          });
  connect(m_session, &EditorSession::remoteApplied, this,
          [this](const Patch &patch) {
            Event event{patch, true};
            m_events.append(event);
            while (m_events.size() > kMaxEvents)
              m_events.removeFirst();
            emit patchSeen(patch, true);
            if (patch.actor.isAgent())
              emit agentChange(patch);
            emit feedChanged();
          });
}

QVector<ChangeFeed::Event> ChangeFeed::recent(int limit) const {
  if (limit <= 0 || limit >= m_events.size())
    return m_events;
  return QVector<Event>(m_events.constEnd() - limit, m_events.constEnd());
}

QVector<ChangeFeed::Event> ChangeFeed::byActor(const QString &actorId) const {
  QVector<Event> result;
  for (const Event &event : m_events) {
    if (event.patch.actor.id == actorId)
      result.append(event);
  }
  return result;
}

void ChangeFeed::clear() {
  m_events.clear();
  emit feedChanged();
}

} // namespace Templnote::Engine
