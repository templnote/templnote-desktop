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

void Document::restore(const QVector<Block> &blocks) {
  m_blocks = blocks;
  rebuildIdIndex();
}

void Document::rebuildIdIndex() {
  m_idIndex.clear();
  m_idIndex.reserve(m_blocks.size());

  for (int i = 0; i < m_blocks.size(); ++i)
    m_idIndex.insert(m_blocks[i].id, i);
}

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
  return m_idIndex.value(id, -1);
}

bool Document::insertBlock(int index, const Block &block) {
  if (index < 0 || index > m_blocks.size())
    return false;

  if (block.id.isEmpty() || indexOf(block.id) >= 0)
    return false;

  m_blocks.insert(index, block);

  // Only rows after the insertion point shift; update them instead of
  // rebuilding the whole id map.
  for (int i = m_blocks.size() - 1; i > index; --i)
    m_idIndex[m_blocks[i].id] = i;

  m_idIndex.insert(block.id, index);
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
  m_idIndex.remove(id);

  for (int i = index; i < m_blocks.size(); ++i)
    m_idIndex[m_blocks[i].id] = i;

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

  // Validate before mutating so a rejected heading level leaves the block
  // untouched.
  if (type == BlockType::Heading) {
    if (headingLevel == 0)
      headingLevel = 1;

    if (headingLevel < 1 || headingLevel > 6)
      return false;
  }

  m_blocks[index].type = type;
  // keeps headingLevel consistent with the block type
  if (type == BlockType::Heading)
    m_blocks[index].headingLevel = headingLevel;
  else
    m_blocks[index].headingLevel = 0;

  return true;
}

bool Document::updateBlockAlignment(const QString &id, const QString &align) {
  const int index = indexOf(id);

  if (index < 0)
    return false;

  if (align == QLatin1String("center") || align == QLatin1String("right") || align == QLatin1String("left")) {
    m_blocks[index].metadata.insert(QStringLiteral("align"), align);
  } else {
    // remove align
    m_blocks[index].metadata.remove(QStringLiteral("align"));
  }

  return true;
}

bool Document::updateBlockChecked(const QString &id, bool checked) {
  const int index = indexOf(id);

  if (index < 0)
    return false;

  if (checked) {
    m_blocks[index].metadata.insert(QStringLiteral("checked"), true);
  } else {
    m_blocks[index].metadata.remove(QStringLiteral("checked"));
  }

  return true;
}

bool Document::updateBlockCalloutType(const QString &id, const QString &calloutType) {
  const int index = indexOf(id);

  if (index < 0)
    return false;

  if (!calloutType.isEmpty() && calloutType != QLatin1String("note")) {
    m_blocks[index].metadata.insert(QStringLiteral("calloutType"), calloutType);
  } else {
    m_blocks[index].metadata.remove(QStringLiteral("calloutType"));
  }

  return true;
}

bool Document::updateBlockCodeLanguage(const QString &id, const QString &language) {
  const int index = indexOf(id);

  if (index < 0)
    return false;

  const QString trimmed = language.trimmed();
  if (!trimmed.isEmpty()) {
    m_blocks[index].metadata.insert(QStringLiteral("codeLanguage"), trimmed);
  } else {
    m_blocks[index].metadata.remove(QStringLiteral("codeLanguage"));
  }

  return true;
}

bool Document::updateBlockImageSource(const QString &id, const QString &source) {
  const int index = indexOf(id);
  if (index < 0)
    return false;

  const QString trimmed = source.trimmed();
  if (!trimmed.isEmpty()) {
    m_blocks[index].metadata.insert(QStringLiteral("imageSource"), trimmed);
  } else {
    m_blocks[index].metadata.remove(QStringLiteral("imageSource"));
  }
  return true;
}

bool Document::updateBlockImageWidthRatio(const QString &id, double ratio) {
  const int index = indexOf(id);
  if (index < 0)
    return false;

  double clamped = qBound(0.2, ratio, 1.0);
  m_blocks[index].metadata.insert(QStringLiteral("imageWidthRatio"), clamped);
  return true;
}

bool Document::updateBlockMetadata(const QString &id, const QVariantMap &metadata) {
  const int index = indexOf(id);
  if (index < 0)
    return false;

  m_blocks[index].metadata = metadata;
  return true;
}

bool Document::setBlock(int index, const Block &block) {
  if (index < 0 || index >= m_blocks.size())
    return false;

  const bool idChanged = m_blocks[index].id != block.id;
  const QString previousId = m_blocks[index].id;
  m_blocks[index] = block;

  if (idChanged) {
    m_idIndex.remove(previousId);
    m_idIndex.insert(block.id, index);
  }

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

  const int first = qMin(source, destination);
  const int last = qMax(source, destination);

  for (int i = first; i <= last; ++i)
    m_idIndex[m_blocks[i].id] = i;

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
                             current.type == BlockType::NumberedList ||
                             current.type == BlockType::Todo ||
                             current.type == BlockType::Quote)
                                ? current.type
                                : BlockType::Paragraph;

  Block newBlock = Block::create(newType, remainingText);

  newBlock.parentId = current.parentId;
  const QString newId = newBlock.id;

  m_blocks.insert(index + 1, newBlock);

  for (int i = index + 1; i < m_blocks.size(); ++i)
    m_idIndex[m_blocks[i].id] = i;

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
  m_idIndex.remove(current.id);

  for (int i = index - 1; i < m_blocks.size(); ++i)
    m_idIndex[m_blocks[i].id] = i;

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
  if (m_hasFullWidth) {
    json.insert(QStringLiteral("fullWidth"), m_fullWidth);
  }

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
  rebuildIdIndex();

  if (json.contains(QStringLiteral("fullWidth"))) {
    m_fullWidth = json.value(QStringLiteral("fullWidth")).toBool(false);
    m_hasFullWidth = true;
  } else {
    m_fullWidth = false;
    m_hasFullWidth = false;
  }

  if (error)
    error->clear();

  return true;
}
} // namespace Templnote
