#include <qtpreprocessorsupport.h>
#include <strings.h>

#include <QJsonArray>
#include <QSet>

#include "Block.h"
#include "Document.h"
#include "core/BlockTypes.h"

namespace Templnote {
const QVector<Block> &Document::blocks() const { return m_blocks; }

// used for the undo redo
QVector<Block> Document::snapshot() const { return m_blocks; }

void Document::restore(const QVector<Block> &blocks) { m_blocks = blocks; }

int Document::count() const { return m_blocks.size(); }

const Block *Document::blockAt(int index) const {
  if (index < 0 || index >= m_blocks.size())
    return nullptr;
  return &m_blocks[index];
}

const Block *Document::findBlock(const QString &id) const {
  const int index = indexOf(id);

  return index >= 0 ? &m_blocks[index] : nullptr;
}

int Document::indexOf(const QString &id) const {
  for (int i = 0; i < m_blocks.size(); ++i) {
    if (m_blocks[i].id == id)
      return i;
  }
  return -1;
}

bool Document::insertBlock(int index, const Block &block) {
  if (index < 0 || index > m_blocks.size())
    return false;

  if (block.id.isEmpty() || indexOf(block.id) >= 0)
    return false;

  m_blocks.insert(index, block);
  return true;
}

bool Document::appendBlock(const Block &block) {
  return insertBlock(m_blocks.size(), block);
}

bool Document::removeBlock(const QString &id) {
  const int index = indexOf(id);

  if (index < 0)
    return false;

  m_blocks.removeAt(index);

  return true;
}

bool Document::updateBlockText(const QString &id, const QString &text) {
  const int index = indexOf(id);

  if (index < 0)
    return false;

  m_blocks[index].text = text;

  return true;
}

bool Document::updateBlockType(const QString &id, BlockType type,
                               int headingLevel) {
  const int index = indexOf(id);

  if (index < 0)
    return false;

  m_blocks[index].type = type;
  // keeps headingLevel consistent with the block type
  if (type == BlockType::Heading) {
    if (headingLevel == 0)
      headingLevel = 1;

    if (headingLevel < 1 || headingLevel > 6)
      return false;
    m_blocks[index].headingLevel = headingLevel;
  } else
    m_blocks[index].headingLevel = 0;

  return true;
}

bool Document::setBlock(int index, const Block &block) {
  if (index < 0 || index >= m_blocks.size())
    return false;

  m_blocks[index] = block;
  return true;
}

bool Document::moveBlock(const QString &id, int destination) {
  const int source = indexOf(id);

  if (source < 0 || destination < 0 || destination >= m_blocks.size()) {
    return false;
  }

  if (source == destination)
    return true;

  Block block = m_blocks.takeAt(source);
  m_blocks.insert(destination, block);

  return true;
}

QString Document::splitBlock(const QString &id, int position) {
  const int index = indexOf(id);

  if (index < 0)
    return {};

  Block &current = m_blocks[index];

  if (position < 0 || position > current.text.size())
    return {};

  const QString remainingText = current.text.mid(position);
  current.text.truncate(position);

  const BlockType newType = (current.type == BlockType::Bullet ||
                             current.type == BlockType::NumberedList)
                                ? current.type
                                : BlockType::Paragraph;

  Block newBlock = Block::create(newType, remainingText);

  newBlock.parentId = current.parentId;
  const QString newId = newBlock.id;

  m_blocks.insert(index + 1, newBlock);
  return newId;
}

bool Document::mergeWithPrevious(const QString &id) {
  const int index = indexOf(id);

  if (index <= 0)
    return false;

  Block &previous = m_blocks[index - 1];
  const Block current = m_blocks[index];

  previous.text += current.text;

  m_blocks.removeAt(index);

  return true;
}

QJsonObject Document::toJson() const {
  QJsonArray blocks;

  for (const Block &block : m_blocks)
    blocks.append(block.toJson());

  QJsonObject json;
  json.insert(QStringLiteral("format"), QStringLiteral("templnote"));
  json.insert(QStringLiteral("version"), 1);
  json.insert(QStringLiteral("blocks"), blocks);

  return json;
}

bool Document::fromJson(const QJsonObject &json, QString *error) {
  const auto fail = [error](const QString &message) {
    if (error)
      *error = message;
    return false;
  };

  const QString format = json.value(QStringLiteral("format")).toString();

  if (!format.isEmpty() && format != QLatin1String("templnote"))
    return fail(QStringLiteral("Not a Templnote document."));

  const int version = json.value(QStringLiteral("version")).toInt(1);

  if (version > 1)
    return fail(QStringLiteral("Document was created by a newer version of "
                               "Templnote."));

  const QJsonValue blocksValue = json.value(QStringLiteral("blocks"));

  if (!blocksValue.isArray())
    return fail(QStringLiteral("Document is missing its block list."));

  const QJsonArray blocks = blocksValue.toArray();

  QVector<Block> parsed;
  parsed.reserve(blocks.size());

  QSet<QString> ids;
  ids.reserve(blocks.size());

  for (const QJsonValue &value : blocks) {
    if (!value.isObject())
      return fail(QStringLiteral("Document contains an invalid block."));

    bool blockOk = false;
    const Block block = Block::fromJson(value.toObject(), &blockOk);

    if (!blockOk)
      return fail(QStringLiteral("Document contains a block without an id."));

    if (ids.contains(block.id))
      return fail(QStringLiteral("Document contains duplicate block ids."));

    ids.insert(block.id);
    parsed.append(block);
  }

  m_blocks = parsed;

  if (error)
    error->clear();

  return true;
}
} // namespace Templnote
