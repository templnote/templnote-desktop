#pragma once

// Pure command application. Every function takes a Document by value (or a
// pointer) and returns the atomic ops it performed. No QObject, no GUI, no
// signals — this is the layer both the desktop editor and AI agents call.

#include "Types.h"
#include "core/Document.h"

#include <QVector>

namespace Templnote::Engine::Commands {

struct ApplyResult {
  QVector<Op> ops;
  QString error;
  bool ok() const { return error.isEmpty(); }
};

// Validate a command against the current document without changing anything.
// Returns an empty string when the command is applicable.
QString validate(const Document &doc, const Command &cmd);

// Apply one command. Returns the ops that were performed (empty on failure).
// `actor` and `lamport` from cmd are stamped onto every emitted op.
ApplyResult apply(Document &doc, const Command &cmd);

// Ported from TextBlock.qml: toggle inline markdown formatting over a range.
// Returns the new text; `newStart`/`newEnd` receive the adjusted selection.
struct FormatResult {
  QString text;
  int start = 0;
  int end = 0;
  bool ok() const { return !text.isNull(); }
};
FormatResult toggleFormat(const QString &raw, int start, int end,
                          const QString &format);

} // namespace Templnote::Engine::Commands
