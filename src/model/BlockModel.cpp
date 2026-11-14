#include "BlockModel.h"
#include <QDebug>
#include <qcontainerfwd.h>
#include <qnamespace.h>

namespace Templnote {
namespace {
struct TextDiff {
  QString removed;
  QString inserted;
};

TextDiff diffText(const QString &before, const QString &after) {
  int prefix = 0;
  const int maxPrefix = qMin(before.size(), after.size());

  while (prefix < maxPrefix && before[prefix] == after[prefix])
    ++prefix;

  int suffix = 0;

  while (suffix < before.size() - prefix &&
         suffix < after.size() - prefix &&
         before[before.size() - 1 - suffix] ==
             after[after.size() - 1 - suffix]) {
    ++suffix;
  }

  return {before.mid(prefix, before.size() - prefix - suffix),
          after.mid(prefix, after.size() - prefix - suffix)};
}

bool containsWhitespace(const QString &text) {
  for (const QChar character : text) {
    if (character.isSpace())
      return true;
  }

  return false;
}
} // namespace

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

void BlockModel::saveHistory(const QVector<Block> &before,
                             const QString &focusBeforeId,
                             const QString &focusAfterId) {
  HistoryEntry entry;
  entry.kind = HistoryEntry::Kind::DocumentEdit;
  entry.focusBeforeId = focusBeforeId;
  entry.focusAfterId = focusAfterId;
  entry.documentBefore = before;
  entry.documentAfter = m_document.snapshot();

  m_undoStack.append(entry);
  m_redoStack.clear();
  closeTextGroup();

  emit historyChanged();
}

void BlockModel::saveTextHistory(const QString &id, const QString &before,
                                 const QString &after) {
  const TextDiff diff = diffText(before, after);
  const int changedCharacters =
      diff.removed.size() + diff.inserted.size();

  // A normal key press inserts or removes exactly one character. Anything
  // bigger (paste, replacing a selection, ...) becomes its own history step.
  const bool isSingleCharacterEdit = changedCharacters == 1;
  const bool reachedWordBoundary =
      containsWhitespace(diff.removed) || containsWhitespace(diff.inserted);

  const bool canMerge =
      m_textGroupOpen && m_textGroupBlockId == id &&
      m_textGroupTimer.isValid() && m_textGroupTimer.elapsed() < 750 &&
      isSingleCharacterEdit && !m_undoStack.isEmpty() &&
      m_undoStack.last().kind == HistoryEntry::Kind::TextEdit &&
      m_undoStack.last().blockId == id &&
      m_undoStack.last().textAfter == before;

  if (canMerge) {
    m_undoStack.last().textAfter = after;
    m_textGroupTimer.restart();
  } else {
    HistoryEntry entry;
    entry.kind = HistoryEntry::Kind::TextEdit;
    entry.blockId = id;
    entry.textBefore = before;
    entry.textAfter = after;
    entry.focusBeforeId = id;
    entry.focusAfterId = id;

    m_undoStack.append(entry);
    m_redoStack.clear();

    m_textGroupBlockId = id;
    m_textGroupTimer.start();
    m_textGroupOpen = true;
  }

  // The edit that inserts/removes whitespace is kept in the current word.
  // The next character starts a new undo step.
  if (reachedWordBoundary)
    m_textGroupOpen = false;

  emit historyChanged();
}

void BlockModel::closeTextGroup() {
  m_textGroupOpen = false;
  m_textGroupBlockId.clear();
  m_textGroupTimer.invalidate();
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

  const QString focusBeforeId = before.isEmpty() ? QString() : before.last().id;
  saveHistory(before, focusBeforeId, id);

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

  const Block block = Block::create(static_cast<BlockType>(type), text);
  const QString id = block.id;

  const bool success = m_document.insertBlock(row, block);

  endInsertRows();

  if (!success)
    return false;

  QString focusBeforeId;

  if (!before.isEmpty())
    focusBeforeId = before[qBound(0, row - 1, before.size() - 1)].id;

  saveHistory(before, focusBeforeId, id);

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

  QString focusAfterId;

  if (m_document.count() > 0) {
    const int focusRow = qBound(0, row, m_document.count() - 1);
    focusAfterId = m_document.blockAt(focusRow)->id;
  }

  saveHistory(before, id, focusAfterId);

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

  const QString before = block->text;

  if (!m_document.updateBlockText(id, text))
    return false;

  saveTextHistory(id, before, text);

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

  saveHistory(before, id, newId);

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

  const QString focusAfterId = m_document.blockAt(row - 1)->id;
  saveHistory(before, id, focusAfterId);

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

  saveHistory(before, id, id);
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

int BlockModel::indexOfBlock(const QString &id) const {
  return m_document.indexOf(id);
}

QString BlockModel::lastHistoryBlockId() const { return m_lastHistoryBlockId; }

// undo redo functionality

void BlockModel::applyHistoryEntry(const HistoryEntry &entry, bool undo) {
  if (entry.kind == HistoryEntry::Kind::TextEdit) {
    const int row = m_document.indexOf(entry.blockId);

    if (row < 0)
      return;

    m_lastHistoryBlockId = entry.blockId;

    const QString &text = undo ? entry.textBefore : entry.textAfter;

    if (m_document.updateBlockText(entry.blockId, text)) {
      // A text edit only affects one row, so there is no need to reset the
      // whole model (which would also destroy the editor's focus/cursor).
      emit dataChanged(index(row), index(row), {ContentRole, Qt::DisplayRole});
    }
    return;
  }

  m_lastHistoryBlockId =
      undo ? entry.focusBeforeId : entry.focusAfterId;

  beginResetModel();
  m_document.restore(undo ? entry.documentBefore : entry.documentAfter);
  endResetModel();

  emit countChanged();
}

bool BlockModel::undo() {
  if (m_undoStack.isEmpty())
    return false;

  closeTextGroup();

  const HistoryEntry entry = m_undoStack.takeLast();
  m_redoStack.append(entry);

  applyHistoryEntry(entry, true);

  emit historyChanged();
  return true;
}

bool BlockModel::redo() {
  if (m_redoStack.isEmpty())
    return false;

  closeTextGroup();

  const HistoryEntry entry = m_redoStack.takeLast();
  m_undoStack.append(entry);

  applyHistoryEntry(entry, false);

  emit historyChanged();
  return true;
}

} // namespace Templnote
