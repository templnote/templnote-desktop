#pragma once

#include <QHash>
#include <QJsonObject>
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

  bool setBlock(int index, const Block &block);

  bool moveBlock(const QString &id, int destination);

  QString splitBlock(const QString &id, int position);

  bool mergeWithPrevious(const QString &id);

  // Serializes only the blocks (plus format marker). File-level metadata such
  // as the title and timestamps is owned by BlockModel.
  QJsonObject toJson() const;

  // Parses into a temporary and only replaces the current blocks on success.
  // Unknown block types fall back to paragraphs, but missing ids, duplicate
  // ids and unsupported format versions are rejected.
  bool fromJson(const QJsonObject &json, QString *error = nullptr);

private:
  // Keeps id lookups O(1). Rebuilt after structural changes, which are
  // already O(n) because they shift the block vector.
  void rebuildIdIndex();

  QVector<Block> m_blocks;
  QHash<QString, int> m_idIndex;
};
} // namespace Templnote
