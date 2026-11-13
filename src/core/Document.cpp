#include <qtpreprocessorsupport.h>
#include <strings.h>

#include "Block.h"
#include "Document.h"
#include "core/BlockTypes.h"

namespace Templnote {
const QVector<Block> &Document::blocks() const { return m_blocks; }

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
    if (headingLevel < 1 || headingLevel > 6)
      return false;
    m_blocks[index].headingLevel = headingLevel;
  } else
    m_blocks[index].headingLevel = 0;

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

  Block newBlock = Block::create(BlockType::Paragraph, remainingText);

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
} // namespace Templnote
