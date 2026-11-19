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

  bool updateBlockAlignment(const QString &id, const QString &align);

  bool updateBlockChecked(const QString &id, bool checked);
  bool updateBlockCalloutType(const QString &id, const QString &calloutType);
  bool updateBlockCodeLanguage(const QString &id, const QString &language);
  bool updateBlockImageSource(const QString &id, const QString &source);
  bool updateBlockImageWidthRatio(const QString &id, double ratio);

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

  bool isFullWidth() const { return m_fullWidth; }
  void setFullWidth(bool fullWidth) { m_fullWidth = fullWidth; m_hasFullWidth = true; }
  bool hasFullWidthSet() const { return m_hasFullWidth; }
  void clearFullWidthSet() { m_hasFullWidth = false; m_fullWidth = false; }

private:
  // Keeps id lookups O(1). Rebuilt after structural changes, which are
  // already O(n) because they shift the block vector.
  void rebuildIdIndex();

  QVector<Block> m_blocks;
  QHash<QString, int> m_idIndex;
  bool m_fullWidth = false;
  bool m_hasFullWidth = false;
};
} // namespace Templnote
