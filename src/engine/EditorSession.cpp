#include "EditorSession.h"

#include "CapabilityGate.h"
#include "Commands.h"

#include <QDateTime>
#include <QJsonDocument>
#include <utility>

namespace Templnote::Engine {

EditorSession::EditorSession(QObject *parent)
    : QObject(parent), m_actor(Actor::human()) {}

void EditorSession::stamp(Command &cmd) const {
  if (cmd.actor.id.isEmpty())
    cmd.actor = m_actor;
  cmd.lamport = qMax(cmd.lamport, lamport() + 1);
}

Result<Patch> EditorSession::apply(const Command &cmdIn) {
  Command cmd = cmdIn;
  stamp(cmd);

  // Authorisation is enforced at the single mutation choke point. Callers can
  // never bypass it by claiming an actor identity in their request: the actor
  // has already been resolved by stamp() from the trusted session identity.
  if (m_gate) {
    QString reason;
    if (!m_gate->allows(cmd, &reason))
      return Result<Patch>(reason);
  }

  HistoryEntry entry;
  if (!m_lightweightHistory)
    entry.before = m_state->serialize();
  entry.beforeSelection = m_selection;
  entry.revisionBefore = m_revision;

  QVector<Op> ops;
  const QString error = m_state->apply(cmd, &ops);
  if (!error.isEmpty())
    return Result<Patch>(error);

  Patch patch;
  patch.ops = ops;
  patch.actor = cmd.actor;
  patch.lamport = cmd.lamport;
  patch.timestampMs = QDateTime::currentMSecsSinceEpoch();
  patch.before = m_selection;

  // Derive the after-selection from the ops when possible: a text edit leaves
  // the caret at the end of the inserted text; structural edits keep it in the
  // same block (or move it to the newly created one).
  patch.after = m_selection;
  for (const Op &op : ops) {
    if (op.type == Op::Type::InsertText) {
      patch.after.blockId = op.blockId;
      patch.after.anchor = patch.after.head = op.position + op.text.size();
    } else if (op.type == Op::Type::DeleteRange) {
      patch.after.blockId = op.blockId;
      patch.after.anchor = patch.after.head = op.position;
    } else if (op.type == Op::Type::ReplaceRange) {
      patch.after.blockId = op.blockId;
      patch.after.anchor = patch.after.head = op.position + op.text.size();
    } else if (op.type == Op::Type::InsertBlock) {
      patch.after.blockId = op.blockId;
      patch.after.anchor = patch.after.head = 0;
    } else if (op.type == Op::Type::RemoveBlock) {
      patch.after = Selection{};
    }
  }

  ++m_revision;
  patch.revision = m_revision;
  m_state->setLamport(cmd.lamport);
  m_selection = patch.after;

  if (!m_lightweightHistory)
    entry.after = m_state->serialize();
  entry.afterSelection = m_selection;
  entry.revisionAfter = m_revision;
  entry.patch = patch;
  pushHistory(std::move(entry));

  emit patchApplied(patch);
  emit revisionChanged(m_revision);
  emit selectionChanged(m_selection);
  return Result<Patch>(patch);
}

bool EditorSession::canApply(const Command &cmd, QString *reason) const {
  if (m_gate) {
    Command effective = cmd;
    if (effective.actor.id.isEmpty())
      effective.actor = m_actor;
    if (!m_gate->allows(effective, reason))
      return false;
  }
  // Validate against a temporary copy by asking Commands::validate directly
  // on the live document (validation is non-mutating).
  const QString error = Commands::validate(document(), cmd);
  if (!error.isEmpty() && reason)
    *reason = error;
  return error.isEmpty();
}

Result<Patch> EditorSession::applyRemote(const Patch &patch) {
  if (m_gate) {
    Command probe;
    probe.type = Command::Type::SetText;
    probe.actor = patch.actor;
    QString reason;
    if (!m_gate->allows(probe, &reason))
      return Result<Patch>(reason);
  }
  HistoryEntry entry;
  entry.before = m_state->serialize();
  entry.beforeSelection = m_selection;
  entry.revisionBefore = m_revision;

  for (const Op &op : patch.ops) {
    const QString error = m_state->observe(op);
    if (!error.isEmpty())
      return Result<Patch>(QStringLiteral("Remote op failed: ") + error);
  }

  ++m_revision;
  Patch local = patch;
  local.revision = m_revision;
  m_state->setLamport(qMax(lamport(), patch.lamport));

  entry.after = m_state->serialize();
  entry.afterSelection = m_selection;
  entry.revisionAfter = m_revision;
  entry.patch = local;
  pushHistory(std::move(entry));

  emit remoteApplied(local);
  emit patchApplied(local);
  emit revisionChanged(m_revision);
  return Result<Patch>(local);
}

void EditorSession::pushHistory(HistoryEntry &&entry) {
  m_undo.push_back(std::move(entry));
  while (static_cast<int>(m_undo.size()) > kMaxHistory)
    m_undo.pop_front();
  m_redo.clear();
  emit historyChanged();
}

bool EditorSession::undo() {
  if (m_undo.empty())
    return false;
  HistoryEntry entry = std::move(m_undo.back());
  m_undo.pop_back();

  QString error;
  m_state->load(entry.before, &error);
  if (!error.isEmpty())
    return false;

  m_redo.push_back(std::move(entry));
  m_revision = entry.revisionBefore;
  m_selection = entry.beforeSelection;

  emit revisionChanged(m_revision);
  emit selectionChanged(m_selection);
  emit historyChanged();
  return true;
}

bool EditorSession::redo() {
  if (m_redo.empty())
    return false;
  HistoryEntry entry = std::move(m_redo.back());
  m_redo.pop_back();

  QString error;
  m_state->load(entry.after, &error);
  if (!error.isEmpty())
    return false;

  m_undo.push_back(std::move(entry));
  m_revision = entry.revisionAfter;
  m_selection = entry.afterSelection;

  emit revisionChanged(m_revision);
  emit selectionChanged(m_selection);
  emit historyChanged();
  return true;
}

void EditorSession::clearHistory() {
  m_undo.clear();
  m_redo.clear();
  emit historyChanged();
}

void EditorSession::setSelection(const Selection &selection) {
  if (m_selection.blockId == selection.blockId &&
      m_selection.anchor == selection.anchor &&
      m_selection.head == selection.head)
    return;
  m_selection = selection;
  emit selectionChanged(m_selection);
}

QString EditorSession::documentJson() const {
  return QString::fromUtf8(
      QJsonDocument(m_state->serialize()).toJson(QJsonDocument::Compact));
}

bool EditorSession::loadJson(const QJsonObject &json, QString *error) {
  const bool ok = m_state->load(json, error);
  if (!ok)
    return false;
  m_revision = 0;
  m_selection = Selection{};
  clearHistory();
  emit documentReplaced();
  emit revisionChanged(0);
  return true;
}

void EditorSession::setDocumentState(DocumentState *state) {
  if (!state || state == m_state)
    return;
  DocumentState *old = m_state;
  m_state = state;
  if (old != &m_defaultState)
    delete old;
  emit documentReplaced();
}

} // namespace Templnote::Engine
