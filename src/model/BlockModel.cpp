#include "BlockModel.h"
#include <QDebug>
#include <qcontainerfwd.h>
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

bool BlockModel::canUndo() const { return !m_undoStack.isEmpty(); }
bool BlockModel::canRedo() const { return !m_redoStack.isEmpty(); }
void BlockModel::saveHistory(const QVector<Block> &before) {
  m_undoStack.append(before);
  m_redoStack.clear();
  emit historyChanged();
}

int BlockModel::count() const { return m_document.count(); }
// text rednering in the editor
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

  case NextBlockTypeRole: {
    if (index.row() + 1 >= m_document.count())
      return -1;

    const Block *next = m_document.blockAt(index.row() + 1);

    return next ? static_cast<int>(next->type) : -1;
  }

  case NumberIndexRole: {
    if (block->type != BlockType::NumberedList)
      return 0;

    int num = 1;
    for (int r = index.row() - 1; r >= 0; --r) {
      const Block *prev = m_document.blockAt(r);
      if (prev && prev->type == BlockType::NumberedList) {
        num++;
      } else {
        break;
      }
    }
    return num;
  }

  case Qt::DisplayRole:
    return block->text;

  default:
    return {};
  }
}

QHash<int, QByteArray> BlockModel::roleNames() const {
  return {{BlockIdRole, "blockId"},
          {ParentIdRole, "parentId"},
          {BlockTypeRole, "blockType"},
          {ContentRole, "content"},
          {HeadingLevelRole, "headingLevel"},
          {MetadataRole, "metadata"},
          {NextBlockTypeRole, "nextBlockType"},
          {NumberIndexRole, "numberIndex"}};
}

QString BlockModel::appendBlock(int type, const QString &text) {
  if (!isValidBlockType(type))
    return {};

  const int row = m_document.count();

  const QVector<Block> before = m_document.snapshot();

  beginInsertRows({}, row, row);

  Block block = Block::create(static_cast<BlockType>(type), text);

  const QString id = block.id;

  m_document.appendBlock(block);

  endInsertRows();

  saveHistory(before);

  if (row > 0)
    emit dataChanged(index(row - 1), index(row - 1), {NextBlockTypeRole});

  emit countChanged();

  return id;
}

bool BlockModel::insertBlock(int row, int type, const QString &text) {
  if (!isValidBlockType(type) || row < 0 || row > m_document.count()) {
    return false;
  }

  const QVector<Block> before = m_document.snapshot();

  beginInsertRows({}, row, row);

  const bool success = m_document.insertBlock(
      row, Block::create(static_cast<BlockType>(type), text));

  endInsertRows();

  if (!success)
    return false;

  saveHistory(before);

  if (row > 0)
    emit dataChanged(index(row - 1), index(row - 1), {NextBlockTypeRole});

  if (row + 1 < m_document.count()) {
    emit dataChanged(index(row + 1), index(m_document.count() - 1),
                     {NumberIndexRole});
  }

  emit countChanged();

  return true;
}

bool BlockModel::removeBlock(const QString &id) {
  const int row = m_document.indexOf(id);

  if (row < 0)
    return false;

  const QVector<Block> before = m_document.snapshot();

  beginRemoveRows({}, row, row);

  const bool success = m_document.removeBlock(id);

  endRemoveRows();

  if (!success)
    return false;

  saveHistory(before);

  if (success && row > 0)
    emit dataChanged(index(row - 1), index(row - 1), {NextBlockTypeRole});

  if (success && row < m_document.count()) {
    emit dataChanged(index(row), index(m_document.count() - 1),
                     {NumberIndexRole});
  }

  emit countChanged();

  return true;
}

bool BlockModel::updateBlockText(const QString &id, const QString &text) {
  const int row = m_document.indexOf(id);

  if (row < 0)
    return false;

  const Block *block = m_document.blockAt(row);

  if (!block || block->text == text)
    return true;

  const QVector<Block> before = m_document.snapshot();

  if (!m_document.updateBlockText(id, text))
    return false;

  saveHistory(before);

  emit dataChanged(index(row), index(row), {ContentRole, Qt::DisplayRole});

  return true;
}

QString BlockModel::splitBlock(const QString &id, int position,
                               const QString &editorText) {
  const int row = m_document.indexOf(id);

  if (row < 0)
    return {};

  const QVector<Block> before = m_document.snapshot();

  if (!editorText.isNull() &&
      !m_document.updateBlockText(id, editorText)) {
    return {};
  }

  const Block *block = m_document.blockAt(row);

  if (!block || position < 0 || position > block->text.size()) {
    return {};
  }

  beginInsertRows({}, row + 1, row + 1);

  const QString newId = m_document.splitBlock(id, position);

  endInsertRows();

  if (newId.isEmpty())
    return {};

  saveHistory(before);

  emit dataChanged(index(row), index(row),
                   {ContentRole, Qt::DisplayRole, NextBlockTypeRole});

  if (row + 2 < m_document.count()) {
    emit dataChanged(index(row + 2), index(m_document.count() - 1),
                     {NumberIndexRole});
  }

  return newId;
}

bool BlockModel::mergeWithPrevious(const QString &id,
                                   const QString &editorText) {
  const int row = m_document.indexOf(id);

  if (row <= 0)
    return false;

  const QVector<Block> before = m_document.snapshot();

  if (!editorText.isNull() &&
      !m_document.updateBlockText(id, editorText)) {
    return false;
  }

  beginRemoveRows({}, row, row);

  const bool success = m_document.mergeWithPrevious(id);

  endRemoveRows();

  if (!success)
    return false;

  saveHistory(before);

  emit dataChanged(index(row - 1), index(row - 1),
                   {ContentRole, Qt::DisplayRole, NextBlockTypeRole});

  if (row - 1 < m_document.count()) {
    emit dataChanged(index(row - 1), index(m_document.count() - 1),
                     {NumberIndexRole});
  }

  emit countChanged();

  return true;
}

bool BlockModel::changeBlockType(const QString &id, int type,
                                 int headingLevel) {
  if (!isValidBlockType(type))
    return false;

  const int row = m_document.indexOf(id);

  if (row < 0)
    return false;

  const QVector<Block> before = m_document.snapshot();

  if (!m_document.updateBlockType(id, static_cast<BlockType>(type),
                                  headingLevel)) {
    return false;
  }

  saveHistory(before);
  emit dataChanged(index(row), index(m_document.count() - 1),
                   {BlockTypeRole, HeadingLevelRole, NumberIndexRole});

  if (row > 0)
    emit dataChanged(index(row - 1), index(row - 1), {NextBlockTypeRole});

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

  if (success) {
    const int first = qMin(source, destination);
    const int last = qMin(m_document.count() - 1, qMax(source, destination));

    if (first <= last)
      emit dataChanged(index(first), index(last), {NextBlockTypeRole});

    if (first < m_document.count())
      emit dataChanged(index(first), index(m_document.count() - 1),
                       {NumberIndexRole});
  }

  return success;
}

const Document &BlockModel::document() const { return m_document; }

// undo redo functionality

bool BlockModel::undo() {
  if (m_undoStack.isEmpty())
    return false;

  m_redoStack.append(m_document.snapshot());

  const QVector<Block> previous = m_undoStack.takeLast();

  beginResetModel();
  m_document.restore(previous);
  endResetModel();

  emit countChanged();
  emit historyChanged();
  return true;
}

bool BlockModel::redo() {
  if (m_redoStack.isEmpty())
    return false;

  m_undoStack.append(m_document.snapshot());

  const QVector<Block> next = m_redoStack.takeLast();

  beginResetModel();
  m_document.restore(next);
  endResetModel();

  emit countChanged();
  emit historyChanged();
  return true;
}

} // namespace Templnote
