#pragma once

// Append-only, in-memory view of every patch a session has seen. This is the
// provenance stream the UI subscribes to for "what did the AI change" and the
// live-update bridge uses to push changes into an open document.

#include "Types.h"

#include <QObject>
#include <QVector>

namespace Templnote::Engine {

class EditorSession;

class ChangeFeed : public QObject {
  Q_OBJECT
  Q_PROPERTY(int count READ count NOTIFY feedChanged)

public:
  struct Event {
    Patch patch;
    bool remote = false;
  };

  explicit ChangeFeed(EditorSession *session, QObject *parent = nullptr);

  int count() const { return m_events.size(); }
  const QVector<Event> &events() const { return m_events; }
  QVector<Event> recent(int limit) const;
  QVector<Event> byActor(const QString &actorId) const;
  void clear();

signals:
  void patchSeen(const Patch &patch, bool remote);
  // Emitted for patches whose actor is an Agent or Pipeline. This is the
  // signal the provenance/review panel binds to.
  void agentChange(const Patch &patch);
  void feedChanged();

private:
  EditorSession *m_session = nullptr;
  QVector<Event> m_events;
  static constexpr int kMaxEvents = 500;
};

} // namespace Templnote::Engine
