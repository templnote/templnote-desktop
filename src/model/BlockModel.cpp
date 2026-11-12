#include "BlockModel.h"
#include <QDebug>
#include <qnamespace.h>

namespace Templnote {
BlockModel::BlockModel(QObject *parent) : QAbstractListModel(parent) {
  m_document.appendBlock(
      Block::create(BlockType::Heading, "Welcome to Templnote"));

  m_document.appendBlock(Block::create(BlockType::Paragraph,
                                       "Your local-first writing environment"));
}

int BlockModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  return m_document.count();
}

int BlockModel::count() const { return m_document.count(); }

QVariant BlockModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid())
    return {};

  const Block *block = m_document.blockAt(index.row());

  if (!block)
    return {};

  qDebug() << "Requested row:" << index.row() << "Role:" << role
           << "Text:" << block->text << "ID:" << block->id;

  switch (role) {
  case BlockIdRole:
    return block->id;
  case ParentIdRole:
    return block->parentId;
  case BlockTypeRole:
    return static_cast<int>(block->type);

  case ContentRole:
    return block->text;

  case HeadingLevelRole:
    return block->headingLevel;

  case MetadataRole:
    return block->metadata;

  case Qt::DisplayRole:
    return block->text;

  default:
    return {};
  }
}

QHash<int, QByteArray> BlockModel::roleNames() const {
  return {{BlockIdRole, "blockId"},           {ParentIdRole, "parentId"},
          {BlockTypeRole, "blockType"},       {ContentRole, "content"},
          {HeadingLevelRole, "headingLevel"}, {MetadataRole, "metadata"}};
}

QString BlockModel::appendBlock(int type, const QString &text) {
  if (!isValidBlockType(type))
    return {};

  const int row = m_document.count();

  beginInsertRows({}, row, row);

  Block block = Block::create(static_cast<BlockType>(type), text);

  const QString id = block.id;

  m_document.appendBlock(block);

  endInsertRows();

  emit countChanged();

  return id;
}

bool BlockModel::insertBlock(int row, int type, const QString &text) {
  if (!isValidBlockType(type) || row < 0 || row > m_document.count()) {
    return false;
  }

  beginInsertRows({}, row, row);

  const bool success = m_document.insertBlock(
      row, Block::create(static_cast<BlockType>(type), text));

  endInsertRows();

  if (success)
    emit countChanged();

  return success;
}

bool BlockModel::removeBlock(const QString &id) {
  const int row = m_document.indexOf(id);

  if (row < 0)
    return false;

  beginRemoveRows({}, row, row);

  const bool success = m_document.removeBlock(id);

  endRemoveRows();

  if (success)
    emit countChanged();

  return success;
}

bool BlockModel::updateBlockText(const QString &id, const QString &text) {
  const int row = m_document.indexOf(id);

  if (row < 0)
    return false;

  if (!m_document.updateBlockText(id, text))
    return false;

  emit dataChanged(index(row), index(row), {ContentRole, Qt::DisplayRole});

  return true;
}

QString BlockModel::splitBlock(const QString &id, int position) {
  const int row = m_document.indexOf(id);

  if (row < 0)
    return {};

  beginInsertRows({}, row + 1, row + 1);

  const QString newId = m_document.splitBlock(id, position);

  endInsertRows();

  if (newId.isEmpty())
    return {};

  emit dataChanged(index(row), index(row), {ContentRole, Qt::DisplayRole});

  return newId;
}

bool BlockModel::mergeWithPrevious(const QString &id) {
  return m_document.mergeWithPrevious(id);
}

bool BlockModel::changeBlockType(const QString &id, int type) {
  if (!isValidBlockType(type))
    return false;

  const int row = m_document.indexOf(id);

  if (row < 0)
    return false;

  if (!m_document.updateBlockType(id, static_cast<BlockType>(type))) {
    return false;
  }
  emit dataChanged(index(row), index(row), {BlockTypeRole});

  return true;
}

bool BlockModel::moveBlock(const QString &id, int destination) {
  const int source = m_document.indexOf(id);

  if (source < 0 || destination < 0 || destination >= m_document.count()) {
    return false;
  }

  if (source == destination)
    return true;

  if (!beginMoveRows({}, source, source, {},
                     destination > source ? destination + 1 : destination)) {
    return false;
  }

  const bool success = m_document.moveBlock(id, destination);

  endMoveRows();

  return success;
}

const Document &BlockModel::document() const { return m_document; }

} // namespace Templnote
