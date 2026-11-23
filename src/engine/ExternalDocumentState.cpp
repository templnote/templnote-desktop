#include "ExternalDocumentState.h"

#include "Commands.h"

namespace Templnote::Engine {

QString ExternalDocumentState::apply(const Command &cmd, QVector<Op> *ops) {
  if (!m_document)
    return QStringLiteral("No external document.");
  Commands::ApplyResult result = Commands::apply(*m_document, cmd);
  if (!result.ok())
    return result.error;
  if (ops)
    *ops = result.ops;
  for (const Op &op : result.ops)
    m_lamport = qMax(m_lamport, op.lamport);
  return {};
}

QString ExternalDocumentState::observe(const Op &op) {
  if (!m_document)
    return QStringLiteral("No external document.");
  // Reconstruct a command from the op and apply it to the shared document.
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
    if (const Block *b = m_document->findBlock(op.blockId)) {
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
    if (const Block *b = m_document->findBlock(op.blockId)) {
      QVariantMap metadata = b->metadata;
      if (!op.key.isEmpty())
        metadata[op.key] = op.payload.toVariant();
      else
        metadata = op.payload.toObject().toVariantMap();
      cmd.metadata = metadata;
    }
    break;
  case Op::Type::ApplySnapshot:
    cmd.type = Command::Type::ApplyJson;
    cmd.text = op.fullText;
    break;
  default:
    return {};
  }
  QVector<Op> ignored;
  return apply(cmd, &ignored);
}

QJsonObject ExternalDocumentState::serialize() const {
  return m_document ? m_document->toJson() : QJsonObject{};
}

bool ExternalDocumentState::load(const QJsonObject &json, QString *error) {
  if (!m_document)
    return false;
  return m_document->fromJson(json, error);
}

} // namespace Templnote::Engine
