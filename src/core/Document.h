#pragma once

#include <QString>
#include <QVector>

#include "Block.h"

namespace Templnote {
class Document {
public:
  Document() = default;

  const QVector<Block> &blocks() const;
  // this helps in undo redo
  QVector<Block> snapshot() const;
  void restore(const QVector<Block> &blocks);

  int count() const;

  const Block *blockAt(int index) const;

  const Block *findBlock(const QString &id) const;

  int indexOf(const QString &id) const;

  bool insertBlock(int index, const Block &block);

  bool appendBlock(const Block &block);

  bool removeBlock(const QString &id);

  bool updateBlockText(const QString &id, const QString &text);

  bool updateBlockType(const QString &id, BlockType type, int headingLevel = 0);

  bool moveBlock(const QString &id, int destination);

  QString splitBlock(const QString &id, int position);

  bool mergeWithPrevious(const QString &id);

private:
  QVector<Block> m_blocks;
};
} // namespace Templnote
