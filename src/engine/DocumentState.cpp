#include "DocumentState.h"

#include "Commands.h"

#include <QJsonDocument>

namespace Templnote::Engine {

QString JsonDocumentState::apply(const Command &cmd, QVector<Op> *ops) {
  Commands::ApplyResult result = Commands::apply(m_document, cmd);
  if (!result.ok())
    return result.error;
  if (ops)
    *ops = result.ops;
  for (const Op &op : result.ops)
    m_lamport = qMax(m_lamport, op.lamport);
  return {};
}

QString JsonDocumentState::observe(const Op &op) {
  // Replaying an op on the JSON backend means interpreting it as a command.
  // This is used for local patch replay and tests; true concurrent merging is
  // provided by the CRDT backend in P4.
  Command cmd;
  cmd.actor = op.actor;
  cmd.lamport = op.lamport;
  cmd.blockId = op.blockId;
  cmd.afterId = op.afterId;
  switch (op.type) {
  case Op::Type::InsertText:
    cmd.type = Command::Type::InsertText;
    cmd.position = op.position;
    cmd.text = op.text;
    break;
  case Op::Type::DeleteRange:
    cmd.type = Command::Type::DeleteRange;
    cmd.start = op.position;
    cmd.end = op.position + op.length;
    break;
  case Op::Type::ReplaceRange:
    cmd.type = Command::Type::ReplaceRange;
    cmd.start = op.position;
    cmd.end = op.position + op.length;
    cmd.text = op.text;
    break;
  case Op::Type::InsertBlock:
    cmd.type = Command::Type::InsertBlock;
    cmd.blockType = op.blockType;
    cmd.headingLevel = op.headingLevel;
    cmd.text = op.text;
    break;
  case Op::Type::RemoveBlock:
    cmd.type = Command::Type::RemoveBlock;
    break;
  case Op::Type::MoveBlock:
    cmd.type = Command::Type::MoveBlock;
    cmd.destination = op.destination;
    break;
  case Op::Type::SetBlockType:
    cmd.type = Command::Type::SetBlockType;
    cmd.blockType = op.blockType;
    cmd.headingLevel = op.headingLevel;
    break;
  case Op::Type::SetHeadingLevel:
    cmd.type = Command::Type::SetBlockType;
    if (const Block *b = m_document.findBlock(op.blockId)) {
      cmd.blockType = static_cast<int>(b->type);
      cmd.headingLevel = op.headingLevel;
    }
    break;
  case Op::Type::SetAlignment:
    cmd.type = Command::Type::SetAlignment;
    cmd.alignment = op.text;
    break;
  case Op::Type::SetMetadata:
    cmd.type = Command::Type::SetMetadata;
    if (const Block *b = m_document.findBlock(op.blockId)) {
      QVariantMap metadata = b->metadata;
      if (!op.key.isEmpty()) {
        metadata[op.key] = op.payload.toVariant();
        cmd.metadata = metadata;
      } else {
        cmd.metadata = op.payload.toObject().toVariantMap();
      }
    }
    break;
  case Op::Type::SetParent:
    cmd.type = Command::Type::SetMetadata;
    if (const Block *b = m_document.findBlock(op.blockId)) {
      QVariantMap metadata = b->metadata;
      metadata[QStringLiteral("parentId")] = op.afterId;
      cmd.metadata = metadata;
    }
    break;
  }
  QVector<Op> ignored;
  const QString error = apply(cmd, &ignored);
  if (error.isEmpty())
    m_lamport = qMax(m_lamport, op.lamport);
  return error;
}

QJsonObject JsonDocumentState::serialize() const { return m_document.toJson(); }

bool JsonDocumentState::load(const QJsonObject &json, QString *error) {
  return m_document.fromJson(json, error);
}

} // namespace Templnote::Engine
