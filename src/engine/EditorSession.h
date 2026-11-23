#pragma once

// EditorSession is the single public entry point of the headless editor.
//
// It owns a DocumentState, assigns causality metadata to every command,
// produces a Patch for each applied transaction, maintains a selection and a
// bounded undo/redo history, and emits signals that GUI clients (and the
// change feed in P3) subscribe to.

#include "DocumentState.h"
#include "Types.h"

#include <QObject>
#include <QVector>
#include <deque>

namespace Templnote::Engine {

class CapabilityGate;

class EditorSession : public QObject {
  Q_OBJECT
  Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
  Q_PROPERTY(QString documentJson READ documentJson CONSTANT)

public:
  explicit EditorSession(QObject *parent = nullptr);

  // ---- transaction ---------------------------------------------------------
  Result<Patch> apply(const Command &cmd);
  bool canApply(const Command &cmd, QString *reason = nullptr) const;

  // Apply a remote patch (from sync / AI / another user). Replays each op into
  // the state backend and emits patchApplied so views can update.
  Result<Patch> applyRemote(const Patch &patch);

  // ---- undo / redo ---------------------------------------------------------
  bool undo();
  bool redo();
  bool canUndo() const { return !m_undo.empty(); }
  bool canRedo() const { return !m_redo.empty(); }
  void clearHistory();

  // ---- selection & actor ---------------------------------------------------
  Selection selection() const { return m_selection; }
  void setSelection(const Selection &selection);
  Actor actor() const { return m_actor; }
  void setActor(const Actor &actor) { m_actor = actor; }

  // Optional role gate. When installed, every command (local, remote, agent
  // or phone-originated) is authorised here before it can mutate the document.
  // Leaving it unset preserves the permissive behaviour used by unit tests and
  // embedded single-user clients.
  void setCapabilityGate(CapabilityGate *gate) { m_gate = gate; }
  CapabilityGate *capabilityGate() const { return m_gate; }

  // ---- document access -----------------------------------------------------
  const Document &document() const { return m_state->document(); }
  QJsonObject toJson() const { return m_state->serialize(); }
  QString documentJson() const;
  bool loadJson(const QJsonObject &json, QString *error = nullptr);
  void setDocumentState(DocumentState *state); // takes ownership
  DocumentState *documentState() const { return m_state; }

  int revision() const { return m_revision; }
  quint64 lamport() const { return m_state->lamport(); }

  // When the view keeps its own undo stack, the engine doesn't need to
  // serialize the whole document before/after every command. Skipping that
  // makes per-keystroke application O(changed block) instead of O(document).
  void setLightweightHistory(bool enabled) { m_lightweightHistory = enabled; }
  bool lightweightHistory() const { return m_lightweightHistory; }

signals:
  void patchApplied(const Patch &patch);
  void revisionChanged(int revision);
  void selectionChanged(const Selection &selection);
  void historyChanged();
  void documentReplaced();
  void remoteApplied(const Patch &patch);

private:
  struct HistoryEntry {
    QJsonObject before;
    QJsonObject after;
    Selection beforeSelection;
    Selection afterSelection;
    int revisionBefore = 0;
    int revisionAfter = 0;
    Patch patch;
  };

  void stamp(Command &cmd) const;
  void pushHistory(HistoryEntry &&entry);

  JsonDocumentState m_defaultState;
  DocumentState *m_state = &m_defaultState;
  CapabilityGate *m_gate = nullptr;
  int m_revision = 0;
  Actor m_actor;
  Selection m_selection;
  std::deque<HistoryEntry> m_undo;
  std::deque<HistoryEntry> m_redo;
  bool m_lightweightHistory = false;
  static constexpr int kMaxHistory = 200;
};

} // namespace Templnote::Engine
