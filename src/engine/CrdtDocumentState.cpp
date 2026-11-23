#include "CrdtDocumentState.h"

#include "Commands.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <algorithm>

namespace Templnote::Engine {
namespace {

QString opKey(const Op &op) {
  QByteArray data;
  data += QByteArray::number(static_cast<int>(op.type));
  data += op.actor.id.toUtf8();
  data += op.blockId.toUtf8();
  data += QByteArray::number(static_cast<qint64>(op.lamport));
  data += QByteArray::number(op.position);
  data += op.text.toUtf8();
  data += op.fullText.toUtf8();
  data += QByteArray::number(op.blockType);
  data += op.key.toUtf8();
  data += QJsonDocument(op.payload.toObject()).toJson(QJsonDocument::Compact);
  return QString::fromLatin1(
      QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

bool opLess(const Op &a, const Op &b) {
  if (a.lamport != b.lamport)
    return a.lamport < b.lamport;
  if (a.actor.id != b.actor.id)
    return a.actor.id < b.actor.id;
  if (a.type != b.type)
    return static_cast<int>(a.type) < static_cast<int>(b.type);
  if (a.blockId != b.blockId)
    return a.blockId < b.blockId;
  if (a.position != b.position)
    return a.position < b.position;
  return opKey(a) < opKey(b);
}

} // namespace

QString CrdtDocumentState::apply(const Command &cmd, QVector<Op> *ops) {
  Document scratch;
  QString scratchError;
  if (!scratch.fromJson(m_document.toJson(), &scratchError))
    return scratchError;
  const Commands::ApplyResult r = Commands::apply(scratch, cmd);
  if (!r.ok())
    return r.error;

  QVector<Op> produced = r.ops;
  for (Op op : produced) {
    if (op.actor.id.isEmpty())
      op.actor = cmd.actor;
    if (op.lamport == 0)
      op.lamport = cmd.lamport;
    if (!m_opSet.isEmpty())
      op.lamport = qMax(op.lamport, m_opSet.last().lamport);
    m_opKeys.append(opKey(op));
    m_opSet.append(op);
    m_lamport = qMax(m_lamport, op.lamport);
    applyOpIncrementally(op);
  }
  if (ops)
    *ops = produced;
  return {};
}

QString CrdtDocumentState::observe(const Op &op) {
  const QString key = opKey(op);
  if (m_opKeys.contains(key))
    return {};
  const bool outOfOrder = op.lamport <= m_lamport;
  m_opKeys.append(key);
  m_opSet.append(op);
  m_lamport = qMax(m_lamport, op.lamport);
  // A remote op with a lamport clock at or below our current state could
  // change replay order relative to already-applied ops, so do a full rebuild
  // to stay convergent. Newer ops can be applied incrementally.
  if (outOfOrder)
    rebuild();
  else
    applyOpIncrementally(op);
  return {};
}

int CrdtDocumentState::merge(const QVector<Op> &remoteOps) {
  int added = 0;
  for (const Op &op : remoteOps) {
    const QString key = opKey(op);
    if (m_opKeys.contains(key))
      continue;
    m_opKeys.append(key);
    m_opSet.append(op);
    m_lamport = qMax(m_lamport, op.lamport);
    ++added;
  }
  if (added > 0)
    rebuild(); // merges can be out of order — one rebuild for the whole batch
  return added;
}

bool CrdtDocumentState::wins(quint64 clock, const QString &actor,
                             quint64 currentClock,
                             const QString &currentActor) {
  if (clock != currentClock)
    return clock > currentClock;
  return actor > currentActor;
}

void CrdtDocumentState::applyOpIncrementally(const Op &op) {
  // Fast path for the common case: a locally generated op (or a strictly newer
  // remote one) whose Lamport clock makes it the last in replay order. Apply it
  // directly to the live document instead of replaying the whole op set.
  if (op.type == Op::Type::InsertBlock) {
    if (m_document.findBlock(op.blockId))
      return;
    Block block;
    block.id = op.blockId;
    block.type = static_cast<BlockType>(op.blockType);
    block.headingLevel = op.headingLevel;
    if (block.type == BlockType::Heading && block.headingLevel <= 0)
      block.headingLevel = 1;
    block.text = op.fullText;
    int row = m_document.count();
    if (!op.afterId.isEmpty()) {
      const int anchor = m_document.indexOf(op.afterId);
      if (anchor >= 0)
        row = anchor + 1;
    }
    m_document.insertBlock(row, block);
    Versioned version;
    version.text.value = op.fullText;
    version.text.clock = op.lamport;
    version.text.actor = op.actor.id;
    version.type.value = op.blockType;
    version.type.clock = op.lamport;
    version.type.actor = op.actor.id;
    version.headingLevel.value = op.headingLevel;
    version.headingLevel.clock = op.lamport;
    version.headingLevel.actor = op.actor.id;
    version.parentId.value = op.afterId;
    version.parentId.clock = op.lamport;
    version.parentId.actor = op.actor.id;
    m_versions.insert(op.blockId, version);
    return;
  }

  const int row = m_document.indexOf(op.blockId);
  if (row < 0)
    return;
  Versioned version = m_versions.value(op.blockId);
  const auto setField = [&](Field &field, const QVariant &value) {
    if (wins(op.lamport, op.actor.id, field.clock, field.actor)) {
      field.value = value;
      field.clock = op.lamport;
      field.actor = op.actor.id;
    }
  };

  switch (op.type) {
  case Op::Type::InsertText:
  case Op::Type::DeleteRange:
  case Op::Type::ReplaceRange:
    if (!op.fullText.isEmpty())
      setField(version.text, op.fullText);
    break;
  case Op::Type::SetBlockType:
    setField(version.type, op.blockType);
    setField(version.headingLevel, op.headingLevel);
    break;
  case Op::Type::SetHeadingLevel:
    setField(version.headingLevel, op.headingLevel);
    break;
  case Op::Type::SetAlignment:
    setField(version.fields[QStringLiteral("align")], op.text);
    break;
  case Op::Type::SetMetadata:
    if (!op.key.isEmpty())
      setField(version.fields[op.key], op.payload.toVariant());
    else {
      const QVariantMap incoming = op.payload.toObject().toVariantMap();
      for (auto it = incoming.constBegin(); it != incoming.constEnd(); ++it)
        setField(version.fields[it.key()], it.value());
    }
    break;
  case Op::Type::SetParent:
    setField(version.parentId, op.afterId);
    break;
  case Op::Type::RemoveBlock:
    m_document.removeBlock(op.blockId);
    m_versions.remove(op.blockId);
    return;
  case Op::Type::MoveBlock:
    if (op.destination >= 0 && op.destination < m_document.count())
      m_document.moveBlock(op.blockId, op.destination);
    break;
  default:
    break;
  }
  m_versions.insert(op.blockId, version);

  // Write the resolved fields back to the document.
  Block block = *m_document.blockAt(row);
  block.text = version.text.value.toString();
  block.type = static_cast<BlockType>(version.type.value.toInt());
  block.headingLevel = version.headingLevel.value.toInt();
  if (block.type == BlockType::Heading && block.headingLevel <= 0)
    block.headingLevel = 1;
  block.parentId = version.parentId.value.toString();
  QVariantMap metadata;
  for (auto it = version.fields.constBegin(); it != version.fields.constEnd();
       ++it)
    metadata[it.key()] = it.value().value;
  block.metadata = metadata;
  m_document.setBlock(row, block);
}

void CrdtDocumentState::rebuild() {
  m_versions.clear();
  Document rebuilt;

  QVector<Op> ordered = m_opSet;
  std::stable_sort(ordered.begin(), ordered.end(), opLess);

  // Pass 1: create blocks so content ops have a target.
  for (const Op &op : ordered) {
    if (op.type != Op::Type::InsertBlock || rebuilt.findBlock(op.blockId))
      continue;
    Block block;
    block.id = op.blockId;
    block.type = static_cast<BlockType>(op.blockType);
    block.headingLevel = op.headingLevel;
    if (block.type == BlockType::Heading && block.headingLevel <= 0)
      block.headingLevel = 1;
    int row = rebuilt.count();
    if (!op.afterId.isEmpty()) {
      const int anchor = rebuilt.indexOf(op.afterId);
      if (anchor >= 0)
        row = anchor + 1;
    }
    block.text = op.fullText;
    rebuilt.insertBlock(row, block);
  }

  // Pass 2: LWW resolution for text / fields.
  for (const Op &op : ordered) {
    const int row = rebuilt.indexOf(op.blockId);
    if (row < 0)
      continue;
    Versioned version = m_versions.value(op.blockId);
    const auto setField = [&](Field &field, const QVariant &value) {
      if (wins(op.lamport, op.actor.id, field.clock, field.actor)) {
        field.value = value;
        field.clock = op.lamport;
        field.actor = op.actor.id;
      }
    };

    switch (op.type) {
    case Op::Type::InsertBlock:
      if (!version.text.value.isValid()) {
        setField(version.text, op.fullText);
        setField(version.type, op.blockType);
        setField(version.headingLevel, op.headingLevel);
        setField(version.parentId, op.afterId);
      }
      break;
    case Op::Type::InsertText:
    case Op::Type::DeleteRange:
    case Op::Type::ReplaceRange:
      if (!op.fullText.isEmpty())
        setField(version.text, op.fullText);
      break;
    case Op::Type::SetBlockType:
      setField(version.type, op.blockType);
      setField(version.headingLevel, op.headingLevel);
      break;
    case Op::Type::SetHeadingLevel:
      setField(version.headingLevel, op.headingLevel);
      break;
    case Op::Type::SetAlignment:
      setField(version.fields[QStringLiteral("align")], op.text);
      break;
    case Op::Type::SetMetadata:
      if (!op.key.isEmpty()) {
        setField(version.fields[op.key], op.payload.toVariant());
      } else {
        const QVariantMap incoming = op.payload.toObject().toVariantMap();
        for (auto it = incoming.constBegin(); it != incoming.constEnd(); ++it)
          setField(version.fields[it.key()], it.value());
      }
      break;
    case Op::Type::SetParent:
      setField(version.parentId, op.afterId);
      break;
    case Op::Type::RemoveBlock:
      rebuilt.removeBlock(op.blockId);
      m_versions.remove(op.blockId);
      continue;
    case Op::Type::MoveBlock:
      if (op.destination >= 0 && op.destination < rebuilt.count())
        rebuilt.moveBlock(op.blockId, op.destination);
      break;
    default:
      break;
    }
    m_versions.insert(op.blockId, version);
  }

  for (auto it = m_versions.constBegin(); it != m_versions.constEnd(); ++it) {
    const int row = rebuilt.indexOf(it.key());
    if (row < 0)
      continue;
    Block block = *rebuilt.blockAt(row);
    block.text = it.value().text.value.toString();
    block.type = static_cast<BlockType>(it.value().type.value.toInt());
    block.headingLevel = it.value().headingLevel.value.toInt();
    if (block.type == BlockType::Heading && block.headingLevel <= 0)
      block.headingLevel = 1;
    block.parentId = it.value().parentId.value.toString();
    QVariantMap metadata;
    for (auto m = it.value().fields.constBegin(); m != it.value().fields.constEnd();
         ++m)
      metadata[m.key()] = m.value().value;
    block.metadata = metadata;
    rebuilt.setBlock(row, block);
  }

  m_document = rebuilt;
}

QJsonObject CrdtDocumentState::serialize() const { return m_document.toJson(); }

bool CrdtDocumentState::load(const QJsonObject &json, QString *error) {
  if (!m_document.fromJson(json, error))
    return false;
  clear();
  for (int i = 0; i < m_document.count(); ++i) {
    const Block *block = m_document.blockAt(i);
    Op op;
    op.type = Op::Type::InsertBlock;
    op.blockId = block->id;
    op.afterId = i > 0 ? m_document.blockAt(i - 1)->id : QString();
    op.blockType = static_cast<int>(block->type);
    op.headingLevel = block->headingLevel;
    op.fullText = block->text;
    op.lamport = 0;
    op.actor = Actor::import();
    m_opKeys.append(opKey(op));
    m_opSet.append(op);

    Versioned version;
    version.text.value = block->text;
    version.type.value = op.blockType;
    version.headingLevel.value = block->headingLevel;
    version.parentId.value = block->parentId;
    for (auto m = block->metadata.constBegin(); m != block->metadata.constEnd();
         ++m) {
      Field f;
      f.value = m.value();
      version.fields.insert(m.key(), f);
    }
    m_versions.insert(block->id, version);
  }
  return true;
}

void CrdtDocumentState::clear() {
  m_opSet.clear();
  m_opKeys.clear();
  m_versions.clear();
  m_lamport = 0;
}

} // namespace Templnote::Engine
