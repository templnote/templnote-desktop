#pragma once

// Core value types for the headless editor engine.
//
// Everything in this header is QtCore-only: it must be usable from a
// QCoreApplication daemon with no GUI, no QML and no Qt Quick types.
//
// Design contract:
//   - Command  : an intent ("bold this range", "split here"). This is also
//                the shape AI tools speak.
//   - Op       : an atomic, actor-tagged primitive change produced by applying
//                a Command. Ops are what get journaled, synced and replayed.
//   - Patch    : the transaction returned by EditorSession::apply(). It
//                carries every Op in the transaction plus causality metadata.
//   - Selection: caret/anchor as data (block id + raw offsets), never a widget.
//   - Result<T>: explicit error channel, no exceptions crossing the API.

#include "core/BlockTypes.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <cstdint>
#include <utility>

namespace Templnote::Engine {

// ---------------------------------------------------------------------------
// Result<T>
// ---------------------------------------------------------------------------
template <typename T> class Result {
public:
  Result(T value) : m_value(std::move(value)), m_ok(true) {}
  Result(const QString &error) : m_error(error), m_ok(false) {}
  Result(QString &&error) : m_error(std::move(error)), m_ok(false) {}

  bool ok() const { return m_ok; }
  explicit operator bool() const { return m_ok; }
  const T &value() const { return m_value; }
  T &value() { return m_value; }
  T valueOr(const T &fallback) const { return m_ok ? m_value : fallback; }
  const QString &error() const { return m_error; }

private:
  T m_value{};
  QString m_error;
  bool m_ok = false;
};

template <> class Result<void> {
public:
  Result() : m_ok(true) {}
  Result(const QString &error) : m_error(error), m_ok(false) {}
  Result(QString &&error) : m_error(std::move(error)), m_ok(false) {}

  bool ok() const { return m_ok; }
  explicit operator bool() const { return m_ok; }
  const QString &error() const { return m_error; }

private:
  QString m_error;
  bool m_ok = false;
};

using VoidResult = Result<void>;

// ---------------------------------------------------------------------------
// Actor — who (or what) performed an edit.
// ---------------------------------------------------------------------------
struct Actor {
  enum class Kind : uint8_t { Human, Agent, Pipeline, System, Import };

  QString id;
  Kind kind = Kind::Human;
  QString displayName;

  static Actor human(const QString &id = QStringLiteral("local-human")) {
    return Actor{id, Kind::Human, QStringLiteral("User")};
  }
  static Actor agent(const QString &id, const QString &name = {}) {
    return Actor{id, Kind::Agent, name.isEmpty() ? id : name};
  }
  static Actor pipeline(const QString &id, const QString &name = {}) {
    return Actor{id, Kind::Pipeline, name.isEmpty() ? id : name};
  }
  static Actor system() {
    return Actor{QStringLiteral("system"), Kind::System,
                 QStringLiteral("System")};
  }
  static Actor import() {
    return Actor{QStringLiteral("import"), Kind::Import,
                 QStringLiteral("Import")};
  }

  bool isAgent() const {
    return kind == Kind::Agent || kind == Kind::Pipeline;
  }
  QString kindName() const;
  QJsonObject toJson() const;
  static Actor fromJson(const QJsonObject &json);
};

// ---------------------------------------------------------------------------
// Selection — data, never a QML TextArea.
// ---------------------------------------------------------------------------
struct Selection {
  QString blockId;
  int anchor = 0;
  int head = 0;

  int start() const { return qMin(anchor, head); }
  int end() const { return qMax(anchor, head); }
  bool collapsed() const { return anchor == head; }
  bool empty() const { return blockId.isEmpty(); }

  QJsonObject toJson() const;
  static Selection fromJson(const QJsonObject &json);
};

// ---------------------------------------------------------------------------
// Op — atomic primitive change. Stable ids make every op CRDT-compatible.
// ---------------------------------------------------------------------------
struct Op {
  enum class Type : uint8_t {
    InsertText,
    DeleteRange,
    ReplaceRange,
    InsertBlock,
    RemoveBlock,
    MoveBlock,
    SetBlockType,
    SetHeadingLevel,
    SetAlignment,
    SetMetadata,
    SetParent,
    ApplySnapshot // full-document JSON, for complex structural operations
  };

  Type type = Type::ReplaceRange;
  QString blockId;      // stable anchor for the affected block
  QString afterId;      // insert/move reference (stable id)
  int position = -1;    // block-local raw offset
  int length = 0;       // range length (DeleteRange/ReplaceRange)
  int destination = -1; // MoveBlock target row
  QString text;         // inserted/replacement text
  QString fullText;     // resulting complete block text (for convergent LWW replay)
  int blockType = 0;
  int headingLevel = 0;
  QString key;          // metadata key
  QJsonValue payload;   // type-specific data
  quint64 lamport = 0;
  Actor actor;

  QString typeName() const;
  QJsonObject toJson() const;
  static Op fromJson(const QJsonObject &json);
};

// ---------------------------------------------------------------------------
// Command — a validated intent. This is the AI tool-call shape.
// ---------------------------------------------------------------------------
struct Command {
  enum class Type : uint8_t {
    InsertText,
    DeleteRange,
    ReplaceRange,
    ToggleFormat,
    SetBlockType,
    SetAlignment,
    SetMetadata,
    SplitBlock,
    MergeWithPrevious,
    InsertBlock,
    RemoveBlock,
    MoveBlock,
    SetText,
    SetChecked,
    SetCalloutType,
    SetCodeLanguage,
    SetImageSource,
    SetImageWidthRatio,
    ApplyJson // apply a full document JSON (complex structural edits)
  };

  Type type = Type::ReplaceRange;
  QString blockId;
  QString afterId;
  int position = -1;
  int start = -1;
  int end = -1;
  QString text;
  QString format;
  QString alignment;
  QString calloutType;
  QString language;
  QString imageSource;
  double imageWidthRatio = 1.0;
  int blockType = 0;
  int headingLevel = 0;
  int destination = -1;
  bool checked = false;
  QVariantMap metadata;
  Actor actor;
  quint64 lamport = 0;
  // Set by the session after apply; empty on a freshly built command.
  QString resultingBlockId;

  QString typeName() const;
  static QString typeName(Type type);
  static Type typeFromName(const QString &name, bool *ok = nullptr);

  // JSON is the tool-call wire format. Keys are stable and documented.
  QJsonObject toJson() const;
  static Command fromJson(const QJsonObject &json, QString *error = nullptr);

  // Convenience constructors for the common cases.
  static Command insertText(const QString &blockId, int position,
                            const QString &text) {
    Command c;
    c.type = Type::InsertText;
    c.blockId = blockId;
    c.position = position;
    c.text = text;
    return c;
  }
  static Command replaceRange(const QString &blockId, int start, int end,
                              const QString &text) {
    Command c;
    c.type = Type::ReplaceRange;
    c.blockId = blockId;
    c.start = start;
    c.end = end;
    c.text = text;
    return c;
  }
  static Command deleteRange(const QString &blockId, int start, int end) {
    Command c;
    c.type = Type::DeleteRange;
    c.blockId = blockId;
    c.start = start;
    c.end = end;
    return c;
  }
  static Command setBlockType(const QString &blockId, int type,
                              int headingLevel = 0) {
    Command c;
    c.type = Type::SetBlockType;
    c.blockId = blockId;
    c.blockType = type;
    c.headingLevel = headingLevel;
    return c;
  }
  static Command setText(const QString &blockId, const QString &text) {
    Command c;
    c.type = Type::SetText;
    c.blockId = blockId;
    c.text = text;
    return c;
  }
  static Command insertBlock(const QString &afterId, int type,
                             const QString &text = {}) {
    Command c;
    c.type = Type::InsertBlock;
    c.afterId = afterId;
    c.blockType = type;
    c.text = text;
    return c;
  }
  static Command toggleFormat(const QString &blockId, int start, int end,
                              const QString &format) {
    Command c;
    c.type = Type::ToggleFormat;
    c.blockId = blockId;
    c.start = start;
    c.end = end;
    c.format = format;
    return c;
  }
};

// ---------------------------------------------------------------------------
// Patch — the transaction returned by apply().
// ---------------------------------------------------------------------------
struct Patch {
  int revision = 0;
  QVector<Op> ops;
  Selection before;
  Selection after;
  Actor actor;
  quint64 lamport = 0;
  qint64 timestampMs = 0;

  bool empty() const { return ops.isEmpty(); }
  QJsonObject toJson() const;
  static Patch fromJson(const QJsonObject &json);
};

// ---------------------------------------------------------------------------
// Version / snapshot metadata.
// ---------------------------------------------------------------------------
struct SnapshotInfo {
  QString id;
  int revision = 0;
  qint64 timestampMs = 0;
  Actor actor;
  QString label;
  QJsonObject toJson() const;
  static SnapshotInfo fromJson(const QJsonObject &json);
};

} // namespace Templnote::Engine
