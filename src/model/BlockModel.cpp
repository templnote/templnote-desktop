#include "BlockModel.h"
#include "../tracking/ScopedTimer.h"
#include "../tracking/MemoryTracker.h"
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QTextBoundaryFinder>
#include <algorithm>
#include <functional>
#include <utility>
#include <qcontainerfwd.h>
#include <qnamespace.h>

#include "../core/BlockTypes.h"
#include "../storage/AppPaths.h"

namespace Templnote {
namespace {
struct TextDiff {
  int prefix = 0;
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

  return {prefix,
          before.mid(prefix, before.size() - prefix - suffix),
          after.mid(prefix, after.size() - prefix - suffix)};
}

} // namespace

// The editor shows only top-level blocks. Nested column children are rendered
// by ColumnsBlock itself, so the ListView works against this proxy instead of
// the raw document model.
class VisibleBlocksProxy : public QSortFilterProxyModel {
public:
  explicit VisibleBlocksProxy(QObject *parent = nullptr)
      : QSortFilterProxyModel(parent) {}

  void setParentFilter(const QString &parentId) {
    if (m_parentFilter == parentId)
      return;

    m_parentFilter = parentId;
    invalidateFilter();
  }

protected:
  bool filterAcceptsRow(int sourceRow,
                        const QModelIndex &sourceParent) const override {
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    return index.isValid() &&
           sourceModel()->data(index, Templnote::BlockModel::ParentIdRole)
                   .toString() == m_parentFilter;
  }

private:
  QString m_parentFilter;
};

namespace {

bool containsWhitespace(const QString &text) {
  for (const QChar character : text) {
    if (character.isSpace())
      return true;
  }

  return false;
}

// Returns true when the given edit ends the current word. Whitespace and
// punctuation are attached to the word before them, while ordinary letters and
// digits keep the group open. QTextBoundaryFinder handles punctuation and
// scripts where a simple space check is not enough.
bool endsWord(const QString &text, const TextDiff &diff) {
  if (containsWhitespace(diff.removed) || containsWhitespace(diff.inserted))
    return true;

  if (diff.inserted.isEmpty())
    return false;

  if (diff.inserted.back().isLetterOrNumber())
    return false;

  QTextBoundaryFinder finder(QTextBoundaryFinder::Word, text);
  finder.setPosition(qBound(0, diff.prefix + diff.inserted.size(), text.size()));
  return finder.isAtBoundary();
}

} // namespace

BlockModel::BlockModel(QObject *parent) : QAbstractListModel(parent) {
  m_visibleModel = new VisibleBlocksProxy(this);
  m_visibleModel->setParentFilter(QString());
  m_visibleModel->setSourceModel(this);

  m_autosaveTimer.setSingleShot(true);
  m_autosaveTimer.setInterval(1500);
  connect(&m_autosaveTimer, &QTimer::timeout, this, &BlockModel::autosave);

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

bool BlockModel::canUndo() const { return !m_undoStack.empty(); }
bool BlockModel::canRedo() const { return !m_redoStack.empty(); }

void BlockModel::saveHistory(const QVector<PatchOp> &patch,
                             const QString &focusBeforeId,
                             const QString &focusAfterId) {
  HistoryEntry entry;
  entry.kind = HistoryEntry::Kind::StructuralEdit;
  entry.focusBeforeId = focusBeforeId;
  entry.focusAfterId = focusAfterId;
  entry.patch = patch;
  entry.stateBeforeId = m_currentStateId;
  entry.stateAfterId = ++m_stateCounter;
  m_currentStateId = entry.stateAfterId;
  ++m_contentRevision;
  emit contentRevisionChanged();

  m_undoStack.push_back(entry);
  m_redoStack.clear();
  trimHistory();
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
  const bool reachedWordBoundary = endsWord(after, diff);

  const bool canMerge =
      m_textGroupOpen && m_textGroupBlockId == id &&
      m_textGroupTimer.isValid() && m_textGroupTimer.elapsed() < 750 &&
      isSingleCharacterEdit && !m_undoStack.empty() &&
      m_undoStack.back().kind == HistoryEntry::Kind::TextEdit &&
      m_undoStack.back().blockId == id &&
      m_undoStack.back().textAfter == before;

  if (canMerge) {
    m_undoStack.back().textAfter = after;
    m_undoStack.back().stateAfterId = ++m_stateCounter;
    m_currentStateId = m_undoStack.back().stateAfterId;
    ++m_contentRevision;
    emit contentRevisionChanged();
    m_textGroupTimer.restart();
  } else {
    HistoryEntry entry;
    entry.kind = HistoryEntry::Kind::TextEdit;
    entry.blockId = id;
    entry.textBefore = before;
    entry.textAfter = after;
    entry.focusBeforeId = id;
    entry.focusAfterId = id;
    entry.stateBeforeId = m_currentStateId;
    entry.stateAfterId = ++m_stateCounter;
    m_currentStateId = entry.stateAfterId;
    ++m_contentRevision;
    emit contentRevisionChanged();

    m_undoStack.push_back(entry);
    m_redoStack.clear();

    m_textGroupBlockId = id;
    m_textGroupTimer.start();
    m_textGroupOpen = true;
  }

  trimHistory();

  // The edit that inserts/removes whitespace is kept in the current word.
  // The next character starts a new undo step.
  if (reachedWordBoundary)
    m_textGroupOpen = false;

  // Multi-character edits (paste, selection replacement, ...) are their own
  // undo step; typing afterwards starts a fresh group.
  if (!isSingleCharacterEdit)
    m_textGroupOpen = false;

  emit historyChanged();
}

void BlockModel::closeTextGroup() {
  m_textGroupOpen = false;
  m_textGroupBlockId.clear();
  m_textGroupTimer.invalidate();
}

void BlockModel::trimHistory() {
  while (m_undoStack.size() > kMaxHistoryEntries)
    m_undoStack.pop_front();
}

void BlockModel::invalidateStructureCaches() {
  m_numberIndicesDirty = true;
  m_columnChildren.clear();
}

void BlockModel::rebuildNumberIndices() const {
  const int rows = m_document.count();

  m_numberIndices.resize(rows);

  int number = 0;

  for (int row = 0; row < rows; ++row) {
    const Block *block = m_document.blockAt(row);

    if (block && block->type == BlockType::NumberedList) {
      ++number;
      m_numberIndices[row] = number;
    } else {
      number = 0;
      m_numberIndices[row] = 0;
    }
  }

  m_numberIndicesDirty = false;
}

int BlockModel::count() const { return m_document.count(); }
// text rednering in the editor
QVariant BlockModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid())
    return {};

  const Block *block = m_document.blockAt(index.row());

  if (!block)
    return {};

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

  case AlignmentRole: {
    const QString align = block->metadata.value(QStringLiteral("align")).toString();
    return align.isEmpty() ? QStringLiteral("left") : align;
  }

  case CheckedRole: {
    return block->metadata.value(QStringLiteral("checked"), false).toBool();
  }

  case CodeLanguageRole: {
    return block->metadata.value(QStringLiteral("codeLanguage"), QStringLiteral("")).toString();
  }

  case ImageSourceRole: {
    return block->metadata.value(QStringLiteral("imageSource"), QStringLiteral("")).toString();
  }

  case ImageWidthRatioRole: {
    return block->metadata.value(QStringLiteral("imageWidthRatio"), 1.0).toDouble();
  }

  case CalloutTypeRole: {
    const QString type = block->metadata.value(QStringLiteral("calloutType")).toString();
    return type.isEmpty() ? QStringLiteral("note") : type;
  }

  case NextBlockTypeRole: {
    if (index.row() + 1 >= m_document.count())
      return -1;

    const Block *next = m_document.blockAt(index.row() + 1);

    return next ? static_cast<int>(next->type) : -1;
  }

  case NumberIndexRole: {
    if (block->type != BlockType::NumberedList)
      return 0;

    if (m_numberIndicesDirty)
      rebuildNumberIndices();

    if (index.row() < m_numberIndices.size())
      return m_numberIndices[index.row()];

    return 0;
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
          {AlignmentRole, "alignment"},
          {CheckedRole, "checked"},
          {CalloutTypeRole, "calloutType"},
          {CodeLanguageRole, "codeLanguage"},
          {ImageSourceRole, "imageSource"},
          {ImageWidthRatioRole, "imageWidthRatio"},
          {NextBlockTypeRole, "nextBlockType"},
          {NumberIndexRole, "numberIndex"}};
}

QString BlockModel::appendBlock(int type, const QString &text) {
  if (!isValidBlockType(type))
    return {};

  const int row = m_document.count();
  const QString focusBeforeId =
      m_document.count() > 0 ? m_document.blockAt(m_document.count() - 1)->id
                             : QString();

  invalidateStructureCaches();

  beginInsertRows({}, row, row);

  Block block = Block::create(static_cast<BlockType>(type), text);

  const QString id = block.id;

  m_document.appendBlock(block);

  endInsertRows();

  PatchOp op;
  op.type = PatchOp::Type::Insert;
  op.to = row;
  op.block = block;
  saveHistory({op}, focusBeforeId, id);

  if (row > 0)
    emit dataChanged(index(row - 1), index(row - 1), {NextBlockTypeRole});

  markModified();
  emit countChanged();

  return id;
}

QString BlockModel::insertBlockInternal(int row, int type, const QString &text,
                                        const QString &parentId) {
  if (!isValidBlockType(type) || row < 0 || row > m_document.count())
    return {};

  QString focusBeforeId;

  if (m_document.count() > 0)
    focusBeforeId = m_document.blockAt(qBound(0, row - 1,
                                              m_document.count() - 1))->id;

  invalidateStructureCaches();

  beginInsertRows({}, row, row);

  Block block = Block::create(static_cast<BlockType>(type), text);
  block.parentId = parentId;
  const QString id = block.id;

  const bool success = m_document.insertBlock(row, block);

  endInsertRows();

  if (!success)
    return {};

  PatchOp op;
  op.type = PatchOp::Type::Insert;
  op.to = row;
  op.block = block;
  saveHistory({op}, focusBeforeId, id);

  if (row > 0)
    emit dataChanged(index(row - 1), index(row - 1), {NextBlockTypeRole});

  if (row + 1 < m_document.count())
    emitNumberIndexChanged(row + 1);

  markModified();
  emit countChanged();

  return id;
}

bool BlockModel::insertBlock(int row, int type, const QString &text) {
  return !insertBlockInternal(row, type, text, QString()).isEmpty();
}

QString BlockModel::insertBlockAfter(const QString &afterId, int type,
                                     const QString &text) {
  const int row = m_document.indexOf(afterId);

  if (row < 0 || row + 1 > m_document.count())
    return {};

  const Block *after = m_document.blockAt(row);

  if (!after)
    return {};

  // A block inserted after a column child must stay in that column, otherwise
  // it becomes a top-level block rendered inside the column's row range.
  return insertBlockInternal(row + 1, type, text, after->parentId);
}

bool BlockModel::removeBlock(const QString &id) {
  const int row = m_document.indexOf(id);

  if (row < 0)
    return false;

  const Block *target = m_document.blockAt(row);

  if (!target)
    return false;

  // Removing a layout or a column must remove its whole subtree. Otherwise the
  // descendants survive with a dangling parentId and are never rendered again.
  QStringList doomed{id};

  if (target->type == BlockType::Columns) {
    const QStringList columns = columnIdsOf(id);

    for (const QString &columnId : columns) {
      doomed.append(columnId);
      doomed.append(blocksOfColumn(columnId));
    }
  } else if (target->type == BlockType::Column) {
    doomed.append(blocksOfColumn(id));
  }

  QVector<int> rows;
  rows.reserve(doomed.size());

  for (const QString &blockId : std::as_const(doomed)) {
    const int blockRow = m_document.indexOf(blockId);

    if (blockRow >= 0)
      rows.append(blockRow);
  }

  // Remove from the bottom up so every recorded row stays valid for both the
  // view signals and the undo patch.
  std::sort(rows.begin(), rows.end(), std::greater<int>());

  invalidateStructureCaches();

  QVector<PatchOp> patch;
  patch.reserve(rows.size());

  for (const int blockRow : rows) {
    const Block *block = m_document.blockAt(blockRow);

    if (!block)
      continue;

    const QString blockId = block->id;
    const Block removed = *block;

    beginRemoveRows({}, blockRow, blockRow);

    const bool success = m_document.removeBlock(blockId);

    endRemoveRows();

    if (!success)
      continue;

    PatchOp op;
    op.type = PatchOp::Type::Remove;
    op.from = blockRow;
    op.block = removed;
    patch.append(op);
  }

  if (patch.isEmpty())
    return false;

  QString focusAfterId;

  if (m_document.count() > 0) {
    const int focusRow = qBound(0, row, m_document.count() - 1);
    focusAfterId = m_document.blockAt(focusRow)->id;
  }

  saveHistory(patch, id, focusAfterId);

  if (row > 0 && row - 1 < m_document.count())
    emit dataChanged(index(row - 1), index(row - 1), {NextBlockTypeRole});

  emitNumberIndexChanged(row);

  markModified();
  emit countChanged();

  return true;
}

QString BlockModel::duplicateBlock(const QString &id) {
  const int row = m_document.indexOf(id);

  if (row < 0)
    return {};

  const Block *source = m_document.blockAt(row);

  if (!source)
    return {};

  const auto copyOf = [](const Block &original,
                         const QString &parentId) -> Block {
    Block copy = original;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.parentId = parentId;
    return copy;
  };

  // A container has to be copied with its whole subtree; copying only the
  // layout row used to produce an empty layout that could not even be repaired
  // because insertColumn() requires an existing column to anchor to.
  QVector<Block> copies;

  if (source->type == BlockType::Columns) {
    const Block layoutCopy = copyOf(*source, source->parentId);
    copies.append(layoutCopy);

    for (const QString &columnId : columnIdsOf(id)) {
      const Block *column = m_document.findBlock(columnId);

      if (!column)
        continue;

      const Block columnCopy = copyOf(*column, layoutCopy.id);
      copies.append(columnCopy);

      for (const QString &childId : blocksOfColumn(columnId)) {
        const Block *child = m_document.findBlock(childId);

        if (child)
          copies.append(copyOf(*child, columnCopy.id));
      }
    }
  } else if (source->type == BlockType::Column) {
    const Block columnCopy = copyOf(*source, source->parentId);
    copies.append(columnCopy);

    for (const QString &childId : blocksOfColumn(id)) {
      const Block *child = m_document.findBlock(childId);

      if (child)
        copies.append(copyOf(*child, columnCopy.id));
    }
  } else {
    copies.append(copyOf(*source, source->parentId));
  }

  if (copies.isEmpty())
    return {};

  QVector<PatchOp> patch;
  patch.reserve(copies.size());

  int insertRow = row + 1;

  for (const Block &copy : std::as_const(copies)) {
    PatchOp op;
    op.type = PatchOp::Type::Insert;
    op.to = insertRow++;
    op.block = copy;
    patch.append(op);
  }

  invalidateStructureCaches();
  applyPatch(patch, false);
  saveHistory(patch, id, copies.first().id);
  markModified();

  return copies.first().id;
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

  markModified();
  return true;
}

QString BlockModel::splitBlock(const QString &id, int position,
                               const QString &editorText) {
  const int row = m_document.indexOf(id);

  if (row < 0)
    return {};

  const Block beforeBlock = *m_document.blockAt(row);

  if (!editorText.isNull() &&
      !m_document.updateBlockText(id, editorText)) {
    return {};
  }

  const Block *block = m_document.blockAt(row);

  if (!block || position < 0 || position > block->text.size()) {
    return {};
  }

  invalidateStructureCaches();

  beginInsertRows({}, row + 1, row + 1);

  const QString newId = m_document.splitBlock(id, position);

  endInsertRows();

  if (newId.isEmpty())
    return {};

  QVector<PatchOp> patch;

  PatchOp update;
  update.type = PatchOp::Type::Update;
  update.beforeBlock = beforeBlock;
  update.block = *m_document.blockAt(row);
  patch.append(update);

  PatchOp insert;
  insert.type = PatchOp::Type::Insert;
  insert.to = row + 1;
  insert.block = *m_document.blockAt(row + 1);
  patch.append(insert);

  saveHistory(patch, id, newId);

  emit dataChanged(index(row), index(row),
                   {ContentRole, Qt::DisplayRole, NextBlockTypeRole});

  emitNumberIndexChanged(row + 2);

  markModified();
  return newId;
}

bool BlockModel::mergeWithPrevious(const QString &id,
                                   const QString &editorText) {
  const int row = m_document.indexOf(id);

  if (row <= 0)
    return false;

  const Block *previous = m_document.blockAt(row - 1);
  const Block *current = m_document.blockAt(row);

  if (!previous || !current)
    return false;

  // Merging across a column boundary used to write the child's text onto the
  // Column wrapper and delete the child. Keep the parent boundary a hard
  // barrier; the same applies to a layout row.
  if (previous->type == BlockType::Column ||
      previous->type == BlockType::Columns ||
      previous->parentId != current->parentId) {
    return false;
  }

  const Block beforePrevious = *previous;

  if (!editorText.isNull() &&
      !m_document.updateBlockText(id, editorText)) {
    return false;
  }

  const Block removed = *m_document.blockAt(row);

  invalidateStructureCaches();

  beginRemoveRows({}, row, row);

  const bool success = m_document.mergeWithPrevious(id);

  endRemoveRows();

  if (!success)
    return false;

  const QString focusAfterId = m_document.blockAt(row - 1)->id;

  QVector<PatchOp> patch;

  PatchOp update;
  update.type = PatchOp::Type::Update;
  update.beforeBlock = beforePrevious;
  update.block = *m_document.blockAt(row - 1);
  patch.append(update);

  PatchOp remove;
  remove.type = PatchOp::Type::Remove;
  remove.from = row;
  remove.block = removed;
  patch.append(remove);

  saveHistory(patch, id, focusAfterId);

  emit dataChanged(index(row - 1), index(row - 1),
                   {ContentRole, Qt::DisplayRole, NextBlockTypeRole});

  emitNumberIndexChanged(row - 1);

  markModified();
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

  const Block beforeBlock = *m_document.blockAt(row);

  invalidateStructureCaches();

  if (!m_document.updateBlockType(id, static_cast<BlockType>(type),
                                  headingLevel)) {
    return false;
  }

  PatchOp op;
  op.type = PatchOp::Type::Update;
  op.beforeBlock = beforeBlock;
  op.block = *m_document.blockAt(row);
  saveHistory({op}, id, id);
  emit dataChanged(index(row), index(row), {BlockTypeRole, HeadingLevelRole});
  emitNumberIndexChanged(row);

  if (row > 0)
    emit dataChanged(index(row - 1), index(row - 1), {NextBlockTypeRole});

  markModified();
  return true;
}

bool BlockModel::setBlockAlignment(const QString &id, const QString &align) {
  const int row = m_document.indexOf(id);

  if (row < 0)
    return false;

  const Block *block = m_document.blockAt(row);
  if (!block)
    return false;

  // Don't align list blocks per specification
  if (block->type == BlockType::Bullet || block->type == BlockType::NumberedList || block->type == BlockType::Todo)
    return false;

  QString currentAlign = block->metadata.value(QStringLiteral("align")).toString();
  if (currentAlign.isEmpty())
    currentAlign = QStringLiteral("left");
  const QString targetAlign = (align == QLatin1String("center") || align == QLatin1String("right"))
                                  ? align
                                  : QStringLiteral("left");

  if (currentAlign == targetAlign)
    return true;

  const Block beforeBlock = *block;

  if (!m_document.updateBlockAlignment(id, targetAlign))
    return false;

  PatchOp op;
  op.type = PatchOp::Type::Update;
  op.beforeBlock = beforeBlock;
  op.block = *m_document.blockAt(row);
  saveHistory({op}, id, id);
  emit dataChanged(index(row), index(row), {MetadataRole, AlignmentRole});

  markModified();
  return true;
}

bool BlockModel::setBlockChecked(const QString &id, bool checked) {
  const int row = m_document.indexOf(id);

  if (row < 0)
    return false;

  const Block *block = m_document.blockAt(row);
  if (!block || block->type != BlockType::Todo)
    return false;

  const bool currentChecked = block->metadata.value(QStringLiteral("checked"), false).toBool();
  if (currentChecked == checked)
    return true;

  const Block beforeBlock = *block;

  if (!m_document.updateBlockChecked(id, checked))
    return false;

  PatchOp op;
  op.type = PatchOp::Type::Update;
  op.beforeBlock = beforeBlock;
  op.block = *m_document.blockAt(row);
  saveHistory({op}, id, id);
  emit dataChanged(index(row), index(row), {MetadataRole, CheckedRole});

  markModified();
  return true;
}

bool BlockModel::setBlockCalloutType(const QString &id, const QString &calloutType) {
  const int row = m_document.indexOf(id);

  if (row < 0)
    return false;

  const Block *block = m_document.blockAt(row);
  if (!block || block->type != BlockType::Callout)
    return false;

  const QString currentType = block->metadata.value(QStringLiteral("calloutType"), QStringLiteral("note")).toString();
  if (currentType == calloutType)
    return true;

  const Block beforeBlock = *block;

  if (!m_document.updateBlockCalloutType(id, calloutType))
    return false;

  PatchOp op;
  op.type = PatchOp::Type::Update;
  op.beforeBlock = beforeBlock;
  op.block = *m_document.blockAt(row);
  saveHistory({op}, id, id);
  emit dataChanged(index(row), index(row), {MetadataRole, CalloutTypeRole});

  markModified();
  return true;
}

bool BlockModel::setBlockCodeLanguage(const QString &id, const QString &language) {
  const int row = m_document.indexOf(id);

  if (row < 0)
    return false;

  const Block *block = m_document.blockAt(row);
  if (!block || block->type != BlockType::Code)
    return false;

  const QString currentLang = block->metadata.value(QStringLiteral("codeLanguage"), QStringLiteral("")).toString();
  if (currentLang == language.trimmed())
    return true;

  const Block beforeBlock = *block;

  if (!m_document.updateBlockCodeLanguage(id, language))
    return false;

  PatchOp op;
  op.type = PatchOp::Type::Update;
  op.beforeBlock = beforeBlock;
  op.block = *m_document.blockAt(row);
  saveHistory({op}, id, id);
  emit dataChanged(index(row), index(row), {MetadataRole, CodeLanguageRole});

  markModified();
  return true;
}

bool BlockModel::setBlockImageSource(const QString &id, const QString &source) {
  const int row = m_document.indexOf(id);
  if (row < 0)
    return false;

  const Block *block = m_document.blockAt(row);
  if (!block || block->type != BlockType::Image)
    return false;

  const QString currentSource = block->metadata.value(QStringLiteral("imageSource"), QStringLiteral("")).toString();
  if (currentSource == source.trimmed())
    return true;

  const Block beforeBlock = *block;

  if (!m_document.updateBlockImageSource(id, source))
    return false;

  PatchOp op;
  op.type = PatchOp::Type::Update;
  op.beforeBlock = beforeBlock;
  op.block = *m_document.blockAt(row);
  saveHistory({op}, id, id);
  emit dataChanged(index(row), index(row), {MetadataRole, ImageSourceRole});

  markModified();
  return true;
}

bool BlockModel::setBlockImageWidthRatio(const QString &id, double ratio) {
  const int row = m_document.indexOf(id);
  if (row < 0)
    return false;

  const Block *block = m_document.blockAt(row);
  if (!block || block->type != BlockType::Image)
    return false;

  const double currentRatio = block->metadata.value(QStringLiteral("imageWidthRatio"), 1.0).toDouble();
  if (qFuzzyCompare(currentRatio, ratio))
    return true;

  const Block beforeBlock = *block;

  if (!m_document.updateBlockImageWidthRatio(id, ratio))
    return false;

  PatchOp op;
  op.type = PatchOp::Type::Update;
  op.beforeBlock = beforeBlock;
  op.block = *m_document.blockAt(row);
  saveHistory({op}, id, id);
  emit dataChanged(index(row), index(row), {MetadataRole, ImageWidthRatioRole});

  markModified();
  return true;
}

bool BlockModel::setBlockMetadata(const QString &id, const QVariantMap &metadata) {
  const int row = m_document.indexOf(id);
  if (row < 0)
    return false;

  const Block *block = m_document.blockAt(row);
  if (!block)
    return false;

  if (block->metadata == metadata)
    return true;

  const Block beforeBlock = *block;

  if (!m_document.updateBlockMetadata(id, metadata))
    return false;

  if (metadata.contains(QStringLiteral("plainText"))) {
    m_document.updateBlockText(id, metadata.value(QStringLiteral("plainText")).toString());
  }

  PatchOp op;
  op.type = PatchOp::Type::Update;
  op.beforeBlock = beforeBlock;
  op.block = *m_document.blockAt(row);
  saveHistory({op}, id, id);
  emit dataChanged(index(row), index(row), {MetadataRole, ContentRole, Qt::DisplayRole});

  markModified();
  return true;
}

QString BlockModel::importAttachment(const QUrl &fileUrl) {
  const QString localPath = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
  if (!QFileInfo::exists(localPath))
    return localPath;

  const QString attachDir = AppPaths::attachmentsDirectory();
  if (attachDir.isEmpty())
    return QUrl::fromLocalFile(localPath).toString();

  const QFileInfo info(localPath);
  const QString ext = info.suffix().isEmpty() ? QStringLiteral("png") : info.suffix().toLower();
  const QString targetFileName = QUuid::createUuid().toString(QUuid::WithoutBraces) + QLatin1Char('.') + ext;
  const QString targetFilePath = QDir(attachDir).filePath(targetFileName);

  if (QFile::copy(localPath, targetFilePath)) {
    return QUrl::fromLocalFile(targetFilePath).toString();
  }

  return QUrl::fromLocalFile(localPath).toString();
}

void BlockModel::cleanupOrphanedAttachments() {
  const QString directoryPath = AppPaths::attachmentsDirectory();

  if (directoryPath.isEmpty())
    return;

  const QDir directory(directoryPath);
  const QStringList files =
      directory.entryList(QDir::Files | QDir::NoSymLinks, QDir::Name);

  if (files.isEmpty())
    return;

  // Attachment names are UUIDs, so a substring search over the saved document
  // files and the open document is enough to tell whether one is still used.
  QByteArray corpus;

  auto collectFromDirectory = [&corpus](const QString &root) {
    if (root.isEmpty())
      return;

    QDirIterator it(root, QStringList{QStringLiteral("*.templnote")},
                    QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext()) {
      QFile file(it.next());

      if (file.open(QIODevice::ReadOnly))
        corpus.append(file.readAll());
    }
  };

  collectFromDirectory(AppPaths::documentsDirectory());

  // Documents may live outside the documents folder as well.
  if (!m_currentFilePath.isEmpty())
    collectFromDirectory(QFileInfo(m_currentFilePath).absolutePath());

  // Serialize the open document as well: attachment URLs live in block
  // metadata (imageSource), not only in the block text.
  corpus.append(
      QJsonDocument(m_document.toJson()).toJson(QJsonDocument::Compact));

  for (const QString &name : files) {
    const QString path = directory.filePath(name);

    if (!corpus.contains(name.toUtf8()))
      QFile::remove(path);
  }
}

bool BlockModel::moveBlock(const QString &id, int destination) {
  const int source = m_document.indexOf(id);

  if (source < 0 || destination < 0 || destination >= m_document.count()) {
    return false;
  }

  if (source == destination)
    return true;

  const Block moved = *m_document.blockAt(source);

  invalidateStructureCaches();

  if (!beginMoveRows({}, source, source, {},
                     destination > source ? destination + 1 : destination)) {
    return false;
  }

  const bool success = m_document.moveBlock(id, destination);

  endMoveRows();

  if (success) {
    PatchOp op;
    op.type = PatchOp::Type::Move;
    op.from = source;
    op.to = destination;
    op.block = moved;
    saveHistory({op}, id, id);

    const int first = qMin(source, destination);
    const int last = qMin(m_document.count() - 1, qMax(source, destination));

    if (first <= last)
      emit dataChanged(index(first), index(last), {NextBlockTypeRole});

    if (first < m_document.count()) {
      emitNumberIndexChanged(first);

      if (destination != first)
        emitNumberIndexChanged(destination);
    }

    markModified();
  }

  return success;
}

const Document &BlockModel::document() const { return m_document; }

int BlockModel::indexOfBlock(const QString &id) const {
  return m_document.indexOf(id);
}

QString BlockModel::blockIdAt(int row) const {
  const Block *block = m_document.blockAt(row);
  return block ? block->id : QString();
}

int BlockModel::blockTypeAtRow(int row) const {
  const Block *block = m_document.blockAt(row);
  return block ? static_cast<int>(block->type) : -1;
}

bool BlockModel::autosaveEnabled() const { return m_autosaveEnabled; }

void BlockModel::setAutosaveEnabled(bool enabled) {
  if (m_autosaveEnabled == enabled)
    return;

  m_autosaveEnabled = enabled;

  if (enabled)
    scheduleAutosave();
  else
    m_autosaveTimer.stop();

  emit autosaveEnabledChanged();
}

QString BlockModel::lastHistoryBlockId() const { return m_lastHistoryBlockId; }

int BlockModel::lastHistoryCursorPosition() const {
  return m_lastHistoryCursorPosition;
}

QString BlockModel::currentFilePath() const { return m_currentFilePath; }

QString BlockModel::documentTitle() const {
  if (m_currentFilePath.isEmpty())
    return QStringLiteral("Untitled");

  return QFileInfo(m_currentFilePath).completeBaseName();
}

QString BlockModel::documentFolder() const { return m_documentFolder; }

QDateTime BlockModel::lastModified() const { return m_lastModified; }

bool BlockModel::isModified() const { return m_modified; }

QString BlockModel::lastError() const { return m_lastError; }

bool BlockModel::hasUnsavedChanges() const {
  return m_currentStateId != m_savedStateId ||
         m_document.isFullWidth() != m_savedFullWidth ||
         m_document.hasFullWidthSet() != m_savedHasFullWidth;
}

void BlockModel::syncModifiedState() {
  const bool modified = hasUnsavedChanges();
  setModified(modified);

  if (modified)
    scheduleAutosave();
}

void BlockModel::markModified() { syncModifiedState(); }

void BlockModel::emitNumberIndexChanged(int firstRow) {
  // Numbering resets at the first non-numbered block, so a change can only
  // affect the run that starts at (or contains) firstRow. Stop at that
  // boundary instead of rescanning the rest of the document and emitting one
  // signal per numbered block in the whole file.
  for (int row = qMax(0, firstRow); row < m_document.count(); ++row) {
    const Block *block = m_document.blockAt(row);

    if (!block || block->type != BlockType::NumberedList)
      break;

    emit dataChanged(index(row), index(row), {NumberIndexRole});
  }
}

void BlockModel::setModified(bool modified) {
  if (m_modified == modified)
    return;

  m_modified = modified;

  if (!modified)
    m_autosaveTimer.stop();

  emit modifiedChanged();
}

void BlockModel::scheduleAutosave() {
  if (!m_autosaveEnabled || m_currentFilePath.isEmpty())
    return;

  m_autosaveTimer.start();
}

void BlockModel::autosave() {
  // The user may have saved or switched documents while the timer was
  // pending. Only write when there is still something to persist.
  if (!m_modified || m_currentFilePath.isEmpty())
    return;

  saveCurrentDocument();
}

void BlockModel::setLastModified(const QDateTime &modified) {
  if (m_lastModified == modified)
    return;

  m_lastModified = modified;
  emit lastModifiedChanged();
}

QString BlockModel::folderForPath(const QString &path) {
  if (path.isEmpty())
    return {};

  const QString root = QDir::cleanPath(AppPaths::documentsDirectory());

  if (root.isEmpty())
    return {};

  const QString folder = QFileInfo(path).absolutePath();
  const QString relative = QDir(root).relativeFilePath(folder);

  // Files outside the Templnote folder, or directly inside it, have no
  // folder label.
  if (relative.isEmpty() || relative == QLatin1String(".") ||
      relative.startsWith(QLatin1String("..")) ||
      QDir::isAbsolutePath(relative)) {
    return {};
  }

  return QDir::cleanPath(relative);
}

void BlockModel::setCurrentFilePath(const QString &path) {
  const QString normalized =
      path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();

  if (m_currentFilePath == normalized)
    return;

  m_currentFilePath = normalized;
  m_documentFolder = folderForPath(normalized);

  emit currentFilePathChanged();
  emit documentTitleChanged();
}

bool BlockModel::setError(const QString &message) {
  m_lastError = message;
  emit lastErrorChanged();
  return false;
}

bool BlockModel::resetDocument(Document document) {
  beginResetModel();

  m_document = std::move(document);

  m_autosaveTimer.stop();
  m_undoStack.clear();
  m_redoStack.clear();
  closeTextGroup();
  invalidateStructureCaches();
  m_lastHistoryBlockId.clear();
  m_lastHistoryCursorPosition = -1;

  // A freshly loaded/replaced document starts at a clean, saved state.
  m_currentStateId = ++m_stateCounter;
  m_savedStateId = m_currentStateId;
  m_savedFullWidth = m_document.isFullWidth();
  m_savedHasFullWidth = m_document.hasFullWidthSet();
  ++m_contentRevision;
  emit contentRevisionChanged();

  // The cache belongs to the previous document; force the next save to write.
  m_lastSavedData.clear();
  m_lastSavedPath.clear();

  endResetModel();

  emit countChanged();
  emit historyChanged();

  return true;
}

bool BlockModel::saveDocument(const QString &path) {
  QString target = path.trimmed();

  if (target.isEmpty())
    return setError(QStringLiteral("Cannot save a document without a path."));

  if (!target.endsWith(QStringLiteral(".templnote"), Qt::CaseInsensitive))
    target += QStringLiteral(".templnote");

  QFileInfo info(target);

  if (info.exists() && info.isDir())
    return setError(QStringLiteral("The save path is a directory."));

  if (!QDir().mkpath(info.absolutePath()))
    return setError(QStringLiteral("Could not create the document folder."));

  const QString absolutePath = info.absoluteFilePath();

  QJsonObject json = m_document.toJson();
  const QDateTime modified = QDateTime::currentDateTimeUtc();
  const QString now = modified.toString(Qt::ISODate);

  if (m_createdAt.isEmpty())
    m_createdAt = now;

  json.insert(QStringLiteral("title"),
              QFileInfo(absolutePath).completeBaseName());
  json.insert(QStringLiteral("createdAt"), m_createdAt);

  // Compare the document content without the timestamp so an autosave that
  // did not change anything does not rewrite the file.
  const QByteArray contentKey =
      QJsonDocument(json).toJson(QJsonDocument::Compact);
  const bool unchanged = absolutePath == m_lastSavedPath &&
                         contentKey == m_lastSavedData &&
                         QFileInfo::exists(absolutePath);

  if (!unchanged) {
    json.insert(QStringLiteral("modifiedAt"), now);

    const QByteArray data =
        QJsonDocument(json).toJson(QJsonDocument::Indented);

    // QSaveFile writes to a temporary file and renames it into place, so an
    // interrupted save can never truncate an existing document.
    QSaveFile file(absolutePath);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
      return setError(file.errorString());

    if (file.write(data) != data.size()) {
      file.cancelWriting();
      return setError(file.errorString());
    }

    if (!file.commit())
      return setError(file.errorString());

    m_lastSavedData = contentKey;
    m_lastSavedPath = absolutePath;
    setLastModified(modified.toLocalTime());
  }

  // Closing the group guarantees that the next edit gets its own history
  // entry, so undoing it lands exactly on the state written here.
  closeTextGroup();
  m_savedStateId = m_currentStateId;
  m_savedFullWidth = m_document.isFullWidth();
  m_savedHasFullWidth = m_document.hasFullWidthSet();

  setCurrentFilePath(absolutePath);
  setModified(false);

  m_lastError.clear();
  emit lastErrorChanged();

  // The file now contains the current references, so imported files that no
  // document points at can be reclaimed from the cache.
  cleanupOrphanedAttachments();

  return true;
}

bool BlockModel::saveCurrentDocument() {
  if (m_currentFilePath.isEmpty())
    return setError(QStringLiteral("This document has not been saved yet."));

  return saveDocument(m_currentFilePath);
}

bool BlockModel::loadDocument(const QString &path) {
  ScopedTimer loadTimer(QStringLiteral("BlockModel::loadDocument: ") + path);
  QString target = path.trimmed();

  if (target.isEmpty())
    return setError(QStringLiteral("Cannot open a document without a path."));

  if (!QFileInfo::exists(target) &&
      !target.endsWith(QStringLiteral(".templnote"), Qt::CaseInsensitive)) {
    target += QStringLiteral(".templnote");
  }

  const QFileInfo info(target);

  if (!info.exists() || !info.isFile())
    return setError(QStringLiteral("The document does not exist."));

  QFile file(info.absoluteFilePath());

  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return setError(file.errorString());

  QJsonParseError parseError{};
  const QJsonDocument parsed =
      QJsonDocument::fromJson(file.readAll(), &parseError);
  file.close();

  if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
    return setError(QStringLiteral("Could not read document: %1")
                        .arg(parseError.errorString()));
  }

  Document document;
  QString documentError;

  if (!document.fromJson(parsed.object(), &documentError))
    return setError(documentError);

  resetDocument(std::move(document));

  m_createdAt =
      parsed.object().value(QStringLiteral("createdAt")).toString();

  QDateTime modified = QDateTime::fromString(
      parsed.object().value(QStringLiteral("modifiedAt")).toString(),
      Qt::ISODate);

  // Older or hand-written files may not carry modifiedAt.
  if (!modified.isValid())
    modified = info.lastModified();

  setCurrentFilePath(info.absoluteFilePath());
  setLastModified(modified.toLocalTime());
  setModified(false);

  m_lastError.clear();
  emit lastErrorChanged();
  emit fullWidthChanged();
  emit documentLoaded();

  return true;
}

bool BlockModel::createDocument(const QString &path) {
  // Creating a document replaces the open one, so never discard unsaved edits
  // if the caller has not persisted them yet.
  if (m_modified) {
    if (!m_currentFilePath.isEmpty()) {
      if (!saveCurrentDocument())
        return false;
    } else {
      // A modified document that has never been saved has no path of its own.
      // Keep its edits in a fresh Untitled file instead of refusing to create
      // the new document or silently throwing the content away.
      const QString untitledPath = AppPaths::uniqueDocumentPath();

      if (untitledPath.isEmpty() || !saveDocument(untitledPath))
        return false;
    }
  }

  Document document;
  document.appendBlock(Block::create(BlockType::Paragraph));

  resetDocument(std::move(document));

  m_createdAt.clear();
  setModified(false);
  setCurrentFilePath({});

  if (!saveDocument(path))
    return false;

  emit documentCreated();
  return true;
}

bool BlockModel::closeDocument() {
  Document empty;
  resetDocument(std::move(empty));

  m_createdAt.clear();
  m_lastError.clear();
  emit lastErrorChanged();

  setModified(false);
  setLastModified({});
  setCurrentFilePath({});
  emit fullWidthChanged();

  cleanupOrphanedAttachments();

  return true;
}

bool BlockModel::isFullWidth() const {
  return m_document.isFullWidth();
}

void BlockModel::setFullWidth(bool fullWidth) {
  if (m_document.isFullWidth() == fullWidth && m_document.hasFullWidthSet())
    return;

  m_document.setFullWidth(fullWidth);
  // Full width is part of the saved JSON but is not undoable, so it needs its
  // own state id to keep the dirty flag correct.
  m_currentStateId = ++m_stateCounter;
  ++m_contentRevision;
  emit contentRevisionChanged();
  markModified();
  emit fullWidthChanged();
}

bool BlockModel::hasFullWidthSet() const {
  return m_document.hasFullWidthSet();
}

int BlockModel::contentRevision() const { return m_contentRevision; }

// ---------------------------------------------------------------------------
// Column layouts
// ---------------------------------------------------------------------------

namespace {
// Columns use pixel widths like table columns. When their combined width is
// larger than the editor, ColumnsBlock scrolls horizontally instead of
// squeezing them.
constexpr double kMinColumnWidth = 80.0;
// Used for new layouts and to migrate older documents that stored a ratio.
constexpr double kReferenceSheetWidth = 800.0;
constexpr int kMaxColumns = 5;
} // namespace

QAbstractItemModel *BlockModel::visibleModel() const { return m_visibleModel; }

int BlockModel::visibleCount() const { return m_visibleModel->rowCount(); }

int BlockModel::sourceRowOfBlock(const QString &blockId) const {
  return m_document.indexOf(blockId);
}

int BlockModel::sourceRowOfVisibleRow(int row) const {
  const QModelIndex proxyIndex = m_visibleModel->index(row, 0);

  if (!proxyIndex.isValid())
    return -1;

  return m_visibleModel->mapToSource(proxyIndex).row();
}

QAbstractItemModel *BlockModel::blocksModelForColumn(const QString &columnId) {
  if (const auto existing = m_columnModels.constFind(columnId);
      existing != m_columnModels.constEnd())
    return existing.value();

  auto *proxy = new VisibleBlocksProxy(this);
  proxy->setParentFilter(columnId);
  proxy->setSourceModel(this);
  m_columnModels.insert(columnId, proxy);
  return proxy;
}

QString BlockModel::topLevelAncestorId(const QString &blockId) const {
  QString current = blockId;
  QSet<QString> seen;

  while (!current.isEmpty() && !seen.contains(current)) {
    seen.insert(current);
    const Block *block = m_document.findBlock(current);

    if (!block)
      return {};

    if (block->parentId.isEmpty())
      return current;

    current = block->parentId;
  }

  return {};
}

int BlockModel::visibleRowOf(const QString &blockId) const {
  const QString topId = topLevelAncestorId(blockId);

  if (topId.isEmpty())
    return -1;

  const int sourceRow = m_document.indexOf(topId);

  if (sourceRow < 0)
    return -1;

  return m_visibleModel->mapFromSource(index(sourceRow, 0)).row();
}

QString BlockModel::blockIdAtVisibleRow(int row) const {
  const QModelIndex proxyIndex = m_visibleModel->index(row, 0);

  if (!proxyIndex.isValid())
    return {};

  const QModelIndex source = m_visibleModel->mapToSource(proxyIndex);
  const Block *block = m_document.blockAt(source.row());

  return block ? block->id : QString();
}

int BlockModel::layoutRowOf(const QString &blockId) const {
  QString current = blockId;
  QSet<QString> seen;

  while (!current.isEmpty() && !seen.contains(current)) {
    seen.insert(current);
    const int row = m_document.indexOf(current);

    if (row < 0)
      return -1;

    const Block *block = m_document.blockAt(row);

    if (!block)
      return -1;

    if (block->type == BlockType::Columns)
      return row;

    current = block->parentId;
  }

  return -1;
}

QStringList BlockModel::columnIdsOf(const QString &containerId) const {
  QStringList ids;
  const Block *layout = m_document.findBlock(containerId);

  if (!layout || layout->type != BlockType::Columns)
    return ids;

  // Find columns by parentId rather than by row adjacency. Moving a layout or
  // any of its descendants used to break the contiguity assumption and made
  // the whole layout appear empty.
  for (int row = 0; row < m_document.count(); ++row) {
    const Block *block = m_document.blockAt(row);

    if (block && block->parentId == containerId &&
        block->type == BlockType::Column) {
      ids.append(block->id);
    }
  }

  return ids;
}

QStringList BlockModel::blocksOfColumn(const QString &columnId) const {
  if (const auto cached = m_columnChildren.constFind(columnId);
      cached != m_columnChildren.constEnd())
    return cached.value();

  QStringList ids;

  for (int row = 0; row < m_document.count(); ++row) {
    const Block *block = m_document.blockAt(row);

    if (block && block->parentId == columnId &&
        block->type != BlockType::Column)
      ids.append(block->id);
  }

  m_columnChildren.insert(columnId, ids);
  return ids;
}

QString BlockModel::parentBlockId(const QString &blockId) const {
  const Block *block = m_document.findBlock(blockId);
  return block ? block->parentId : QString();
}

QString BlockModel::columnIdOfBlock(const QString &blockId) const {
  const Block *block = m_document.findBlock(blockId);

  if (!block)
    return {};

  if (block->type == BlockType::Column)
    return block->id;

  const Block *parent = m_document.findBlock(block->parentId);

  return (parent && parent->type == BlockType::Column) ? parent->id : QString();
}

QVector<BlockModel::PatchOp>
BlockModel::widthPatch(const QVector<QString> &columnIds,
                       const QVector<double> &widths) const {
  QVector<PatchOp> patch;

  for (int i = 0; i < columnIds.size(); ++i) {
    const Block *current = m_document.findBlock(columnIds[i]);

    if (!current)
      continue;

    Block wanted = *current;
    QVariantMap metadata = current->metadata;
    metadata.insert(QStringLiteral("width"), widths.value(i));
    wanted.metadata = metadata;

    PatchOp op;
    op.type = PatchOp::Type::Update;
    op.beforeBlock = *current;
    op.block = wanted;
    patch.append(op);
  }

  return patch;
}

bool BlockModel::reparentChildren(const QString &fromColumnId,
                                  const QString &toColumnId,
                                  QVector<PatchOp> &patch) const {
  for (const QString &id : blocksOfColumn(fromColumnId)) {
    const Block *current = m_document.findBlock(id);

    if (!current)
      continue;

    Block wanted = *current;
    wanted.parentId = toColumnId;

    PatchOp op;
    op.type = PatchOp::Type::Update;
    op.beforeBlock = *current;
    op.block = wanted;
    patch.append(op);
  }

  return true;
}

QString BlockModel::createColumnsLayout(int row, int count,
                                        double availableWidth) {
  if (count < 2 || count > kMaxColumns || row < 0 || row >= m_document.count())
    return {};

  const Block *source = m_document.blockAt(row);

  if (!source || !source->parentId.isEmpty() ||
      source->type == BlockType::Columns || source->type == BlockType::Column)
    return {};

  const Block original = *source;
  const QString layoutId = original.id;

  Block layout = original;
  layout.type = BlockType::Columns;
  layout.text.clear();
  layout.headingLevel = 0;
  // Columns do not support alignment or the inline formatting toolbar.
  layout.metadata = QVariantMap();

  QVector<Block> columns;
  QVector<Block> children;
  // Split the editor's available width evenly so a fresh layout fills it,
  // falling back to the fixed sheet width for callers without that info.
  const double width = availableWidth > 0.0
                       ? qMax(kMinColumnWidth, availableWidth / count)
                       : kReferenceSheetWidth / count;

  for (int i = 0; i < count; ++i) {
    Block column = Block::create(BlockType::Column);
    column.parentId = layoutId;
    column.metadata.insert(QStringLiteral("width"), width);
    columns.append(column);

    Block child = Block::create(BlockType::Paragraph,
                                i == 0 ? original.text : QString());
    child.parentId = column.id;
    children.append(child);
  }

  QVector<PatchOp> patch;

  PatchOp convert;
  convert.type = PatchOp::Type::Update;
  convert.beforeBlock = original;
  convert.block = layout;
  patch.append(convert);

  for (int i = 0; i < count; ++i) {
    PatchOp columnOp;
    columnOp.type = PatchOp::Type::Insert;
    columnOp.to = row + 1 + i * 2;
    columnOp.block = columns[i];
    patch.append(columnOp);

    PatchOp childOp;
    childOp.type = PatchOp::Type::Insert;
    childOp.to = row + 2 + i * 2;
    childOp.block = children[i];
    patch.append(childOp);
  }

  invalidateStructureCaches();
  applyPatch(patch, false);

  emit dataChanged(index(row), index(row),
                   {BlockTypeRole, HeadingLevelRole, ContentRole, MetadataRole,
                    Qt::DisplayRole});
  saveHistory(patch, original.id, children.first().id);
  markModified();

  return children.first().id;
}

bool BlockModel::insertColumn(const QString &containerId,
                              const QString &afterColumnId) {
  const int layoutRow = m_document.indexOf(containerId);

  if (layoutRow < 0)
    return false;

  const Block *layout = m_document.blockAt(layoutRow);

  if (!layout || layout->type != BlockType::Columns)
    return false;

  const QStringList ids = columnIdsOf(containerId);
  const int afterIndex = ids.indexOf(afterColumnId);

  if (ids.size() >= kMaxColumns)
    return false;

  // An empty afterColumnId means "prepend before the first column".
  if (!afterColumnId.isEmpty() && afterIndex < 0)
    return false;

  // A layout with no columns can exist in documents written by older
  // versions; allow adding the first column so it can be repaired.
  if (ids.isEmpty() && !afterColumnId.isEmpty())
    return false;

  const int oldCount = ids.size();

  // A new column starts at the average width of its neighbours; existing
  // columns keep their widths, so the layout can grow past the editor width.
  double totalWidth = 0.0;

  for (const QString &id : ids)
    totalWidth += columnWidth(id);

  const double newWidth =
      oldCount > 0 ? totalWidth / oldCount : kReferenceSheetWidth;

  Block column = Block::create(BlockType::Column);
  column.parentId = containerId;
  column.metadata.insert(QStringLiteral("width"), newWidth);

  Block child = Block::create(BlockType::Paragraph);
  child.parentId = column.id;

  // Place the new column after the whole subtree of afterColumnId. Column
  // subtrees are contiguous because nested layouts are not allowed.
  int insertAt;

  if (afterColumnId.isEmpty()) {
    insertAt = ids.isEmpty() ? layoutRow + 1
                             : m_document.indexOf(ids.first());
  } else {
    const int anchorRow = m_document.indexOf(afterColumnId);
    insertAt = anchorRow + 1;

    for (int row = anchorRow + 1; row < m_document.count(); ++row) {
      const Block *block = m_document.blockAt(row);

      if (!block || block->parentId.isEmpty())
        break;

      if (block->parentId == afterColumnId)
        insertAt = row + 1;
      else if (block->type == BlockType::Column)
        break;
    }
  }

  QVector<PatchOp> patch;

  PatchOp columnOp;
  columnOp.type = PatchOp::Type::Insert;
  columnOp.to = insertAt;
  columnOp.block = column;
  patch.append(columnOp);

  PatchOp childOp;
  childOp.type = PatchOp::Type::Insert;
  childOp.to = insertAt + 1;
  childOp.block = child;
  patch.append(childOp);

  invalidateStructureCaches();
  applyPatch(patch, false);
  saveHistory(patch, afterColumnId, child.id);
  markModified();

  return true;
}

bool BlockModel::moveColumn(const QString &columnId, int direction) {
  const Block *column = m_document.findBlock(columnId);

  if (!column || column->type != BlockType::Column)
    return false;

  const QStringList ids = columnIdsOf(column->parentId);
  const int index = ids.indexOf(columnId);

  if (index < 0)
    return false;

  const int target = index + (direction < 0 ? -1 : 1);

  if (target < 0 || target >= ids.size())
    return false;

  const int from = m_document.indexOf(columnId);
  const int destination = m_document.indexOf(ids[target]);

  PatchOp op;
  op.type = PatchOp::Type::Move;
  op.block = *column;
  op.from = from;
  op.to = destination;

  applyPatch({op}, false);
  saveHistory({op}, columnId, columnId);
  markModified();

  return true;
}

bool BlockModel::moveColumnToIndex(const QString &columnId, int targetIndex) {
  const Block *column = m_document.findBlock(columnId);

  if (!column || column->type != BlockType::Column)
    return false;

  const QStringList ids = columnIdsOf(column->parentId);
  const int index = ids.indexOf(columnId);

  if (index < 0 || ids.size() < 2)
    return false;

  const int target = qBound(0, targetIndex, ids.size() - 1);

  if (target == index)
    return true;

  const int from = m_document.indexOf(columnId);
  const int destination = m_document.indexOf(ids[target]);

  PatchOp op;
  op.type = PatchOp::Type::Move;
  op.block = *column;
  op.from = from;
  op.to = destination;

  applyPatch({op}, false);
  saveHistory({op}, columnId, columnId);
  markModified();

  return true;
}

bool BlockModel::equalizeColumns(const QString &containerId) {
  const QStringList ids = columnIdsOf(containerId);

  if (ids.size() < 2)
    return false;

  double totalWidth = 0.0;

  for (const QString &id : ids)
    totalWidth += columnWidth(id);

  QVector<QString> columnIds;
  QVector<double> widths;
  const double evenWidth = totalWidth / ids.size();

  for (const QString &id : ids) {
    columnIds.append(id);
    widths.append(evenWidth);
  }

  const QVector<PatchOp> patch = widthPatch(columnIds, widths);
  applyPatch(patch, false);
  saveHistory(patch, containerId, containerId);
  markModified();

  return true;
}

bool BlockModel::removeColumn(const QString &columnId) {
  const int layoutRow = layoutRowOf(columnId);

  if (layoutRow < 0)
    return false;

  const Block *layoutBlock = m_document.blockAt(layoutRow);
  const QString layoutId = layoutBlock->id;
  const QStringList ids = columnIdsOf(layoutId);
  const int index = ids.indexOf(columnId);

  if (index < 0 || ids.size() < 2)
    return false;

  const QString neighborId = ids[index == 0 ? 1 : index - 1];
  const bool collapse = ids.size() == 2;

  QVector<PatchOp> patch;
  const QStringList mergedChildren = blocksOfColumn(columnId);
  reparentChildren(columnId, neighborId, patch);

  if (collapse) {
    // Promote the merged children to top level, turn the layout into the
    // paragraph it replaced, and drop both column wrappers.
    QStringList allChildren = blocksOfColumn(neighborId);
    allChildren.append(mergedChildren);

    for (const QString &id : allChildren) {
      const Block *current = m_document.findBlock(id);

      if (!current)
        continue;

      Block wanted = *current;
      wanted.parentId.clear();

      PatchOp op;
      op.type = PatchOp::Type::Update;
      op.beforeBlock = *current;
      op.block = wanted;
      patch.append(op);
    }

    Block wantedLayout = *layoutBlock;
    wantedLayout.type = BlockType::Paragraph;
    wantedLayout.text.clear();
    wantedLayout.headingLevel = 0;
    wantedLayout.metadata.clear();

    PatchOp convert;
    convert.type = PatchOp::Type::Update;
    convert.beforeBlock = *layoutBlock;
    convert.block = wantedLayout;
    patch.append(convert);

    for (const QString &id : ids) {
      const Block *column = m_document.findBlock(id);

      if (!column)
        continue;

      PatchOp remove;
      remove.type = PatchOp::Type::Remove;
      remove.from = m_document.indexOf(id);
      remove.block = *column;
      patch.append(remove);
    }
  } else {
    PatchOp remove;
    remove.type = PatchOp::Type::Remove;
    remove.from = m_document.indexOf(columnId);
    remove.block = *m_document.findBlock(columnId);
    patch.append(remove);
  }

  const QString focusAfter =
      collapse ? layoutId : blocksOfColumn(neighborId).value(0, neighborId);

  invalidateStructureCaches();
  applyPatch(patch, false);
  saveHistory(patch, columnId, focusAfter);
  markModified();

  return true;
}

bool BlockModel::moveBlockToColumn(const QString &blockId,
                                   const QString &targetColumnId, int row) {
  const Block *block = m_document.findBlock(blockId);
  const Block *target = m_document.findBlock(targetColumnId);

  if (!block || !target || target->type != BlockType::Column)
    return false;

  if (block->type == BlockType::Columns || block->type == BlockType::Column)
    return false;

  const QString sourceColumnId = columnIdOfBlock(blockId);

  if (sourceColumnId.isEmpty())
    return false;

  QStringList targetChildren = blocksOfColumn(targetColumnId);
  targetChildren.removeAll(blockId);

  const int slot = qBound(0, row, targetChildren.size());
  const int from = m_document.indexOf(blockId);
  int destination;

  if (targetChildren.isEmpty()) {
    destination = m_document.indexOf(targetColumnId) + 1;
  } else if (slot == targetChildren.size()) {
    destination = m_document.indexOf(targetChildren.last()) + 1;
  } else {
    destination = m_document.indexOf(targetChildren[slot]);
  }

  if (from < destination)
    --destination;

  if (destination < 0 || destination >= m_document.count())
    return false;

  QVector<PatchOp> patch;

  PatchOp parentOp;
  parentOp.type = PatchOp::Type::Update;
  parentOp.beforeBlock = *block;
  Block wanted = *block;
  wanted.parentId = targetColumnId;
  parentOp.block = wanted;
  patch.append(parentOp);

  PatchOp moveOp;
  moveOp.type = PatchOp::Type::Move;
  moveOp.block = wanted;
  moveOp.from = from;
  moveOp.to = destination;
  patch.append(moveOp);

  invalidateStructureCaches();
  applyPatch(patch, false);
  saveHistory(patch, blockId, blockId);
  markModified();

  return true;
}

double BlockModel::columnWidth(const QString &columnId) const {
  const Block *column = m_document.findBlock(columnId);

  if (!column)
    return 0.0;

  // Older layouts stored a ratio; migrate them to the standard sheet width.
  if (column->metadata.contains(QStringLiteral("width")))
    return column->metadata.value(QStringLiteral("width")).toDouble();

  return column->metadata.value(QStringLiteral("ratio"), 0.0).toDouble()
         * kReferenceSheetWidth;
}

QString BlockModel::appendBlockToColumn(const QString &columnId, int type,
                                        const QString &text) {
  const Block *column = m_document.findBlock(columnId);

  if (!column || column->type != BlockType::Column || !isValidBlockType(type))
    return {};

  const QStringList children = blocksOfColumn(columnId);
  int row = m_document.indexOf(columnId) + 1;

  if (!children.isEmpty())
    row = m_document.indexOf(children.last()) + 1;

  Block block = Block::create(static_cast<BlockType>(type), text);
  block.parentId = columnId;

  PatchOp op;
  op.type = PatchOp::Type::Insert;
  op.to = row;
  op.block = block;

  invalidateStructureCaches();
  applyPatch({op}, false);
  saveHistory({op}, columnId, block.id);
  markModified();

  return block.id;
}

bool BlockModel::setColumnWidth(const QString &columnId, double width) {
  const Block *column = m_document.findBlock(columnId);

  if (!column || column->type != BlockType::Column)
    return false;

  Block wanted = *column;
  QVariantMap metadata = column->metadata;
  metadata.insert(QStringLiteral("width"), qMax(kMinColumnWidth, width));
  metadata.remove(QStringLiteral("ratio"));
  wanted.metadata = metadata;

  const QVector<PatchOp> patch = widthPatch({columnId},
                                            {metadata.value(QStringLiteral("width")).toDouble()});
  applyPatch(patch, false);

  if (!m_columnResizeActive)
    saveHistory(patch, columnId, columnId);

  markModified();
  return true;
}

bool BlockModel::beginColumnResize(const QString &containerId) {
  const QStringList ids = columnIdsOf(containerId);

  if (ids.size() < 2)
    return false;

  m_columnResizeBefore.clear();

  for (const QString &id : ids) {
    const Block *column = m_document.findBlock(id);

    if (column)
      m_columnResizeBefore.insert(id, *column);
  }

  m_columnResizeActive = true;
  return true;
}

bool BlockModel::endColumnResize() {
  if (!m_columnResizeActive)
    return false;

  m_columnResizeActive = false;

  QVector<PatchOp> patch;

  for (auto it = m_columnResizeBefore.constBegin();
       it != m_columnResizeBefore.constEnd(); ++it) {
    const Block *current = m_document.findBlock(it.key());

    if (!current || current->metadata == it.value().metadata)
      continue;

    PatchOp op;
    op.type = PatchOp::Type::Update;
    op.beforeBlock = it.value();
    op.block = *current;
    patch.append(op);
  }

  m_columnResizeBefore.clear();

  if (patch.isEmpty())
    return true;

  saveHistory(patch, patch.first().block.id, patch.first().block.id);
  return true;
}

bool BlockModel::renameDocument(const QString &newBaseName) {
  if (m_currentFilePath.isEmpty())
    return setError(QStringLiteral("This document has not been saved yet."));

  QString base = newBaseName.trimmed();

  if (base.endsWith(QStringLiteral(".templnote"), Qt::CaseInsensitive))
    base.chop(10);

  base = AppPaths::sanitizeFileName(base);

  if (base.isEmpty())
    base = QStringLiteral("Untitled");

  const QFileInfo currentInfo(m_currentFilePath);

  if (base == currentInfo.completeBaseName()) {
    m_lastError.clear();
    emit lastErrorChanged();
    return true;
  }

  const QString target =
      AppPaths::uniqueDocumentPath(currentInfo.absolutePath(), base);

  if (target.isEmpty())
    return setError(QStringLiteral("Could not build a rename target."));

  if (!QFile::rename(currentInfo.absoluteFilePath(), target))
    return setError(QStringLiteral("Could not rename the document."));

  setCurrentFilePath(target);

  // Rename is already complete; updating the JSON title is best effort so a
  // read-only or externally modified file never blocks the rename itself.
  QFile file(target);

  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QJsonParseError parseError{};
    const QJsonDocument parsed =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error == QJsonParseError::NoError && parsed.isObject()) {
      QJsonObject json = parsed.object();
      json.insert(QStringLiteral("title"),
                  QFileInfo(target).completeBaseName());

      QSaveFile output(target);

      if (output.open(QIODevice::WriteOnly | QIODevice::Text)) {
        const QByteArray data =
            QJsonDocument(json).toJson(QJsonDocument::Indented);

        if (output.write(data) == data.size() && output.commit()) {
          m_lastError.clear();
          emit lastErrorChanged();
          return true;
        }
      }
    }
  }

  setError(QStringLiteral(
      "The document was renamed, but its title could not be updated."));
  return true;
}

void BlockModel::adoptRenamedPath(const QString &newPath) {
  const QString trimmed = newPath.trimmed();

  if (m_currentFilePath.isEmpty() || trimmed.isEmpty())
    return;

  const QString normalized = QFileInfo(trimmed).absoluteFilePath();

  if (normalized == m_currentFilePath)
    return;

  // The file was moved on disk while it stayed open. Keep the document and
  // its history intact and only retarget the path so autosave writes to the
  // new location. A changed title is expected after a rename.
  if (m_lastSavedPath == m_currentFilePath)
    m_lastSavedPath = normalized;

  setCurrentFilePath(normalized);
}

// undo redo functionality

void BlockModel::applyHistoryEntry(const HistoryEntry &entry, bool undo) {
  if (entry.kind == HistoryEntry::Kind::TextEdit) {
    const int row = m_document.indexOf(entry.blockId);

    if (row < 0)
      return;

    m_lastHistoryBlockId = entry.blockId;

    const TextDiff diff = diffText(entry.textBefore, entry.textAfter);
    m_lastHistoryCursorPosition =
        undo ? diff.prefix + diff.removed.size()
             : diff.prefix + diff.inserted.size();

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

  // Structural steps keep the caret at the end of the focused block.
  m_lastHistoryCursorPosition = -1;

  applyPatch(entry.patch, undo);
}

void BlockModel::applyPatch(const QVector<PatchOp> &patch, bool undo) {
  bool structureChanged = false;

  invalidateStructureCaches();

  if (undo) {
    for (int i = patch.size() - 1; i >= 0; --i)
      structureChanged |= applyPatchOp(patch[i], true);
  } else {
    for (const PatchOp &op : patch)
      structureChanged |= applyPatchOp(op, false);
  }

  if (structureChanged) {
    // Only rows next to a structural operation can have a different
    // nextBlockType; notifying every row made all delegates re-evaluate.
    QSet<int> affectedRows;
    QSet<int> numberStarts;

    for (const PatchOp &op : patch) {
      for (int offset = -2; offset <= 2; ++offset) {
        const int fromRow = op.from + offset;
        const int toRow = op.to + offset;

        if (fromRow >= 0 && fromRow < m_document.count())
          affectedRows.insert(fromRow);
        if (toRow >= 0 && toRow < m_document.count())
          affectedRows.insert(toRow);
      }

      // Numbering only changes from the touched row to the end of that
      // numbered run. Emitting from row 0 used to rescan the whole document
      // after every insert/remove/move.
      if (op.from >= 0)
        numberStarts.insert(op.from);
      if (op.to >= 0)
        numberStarts.insert(op.to);
    }

    for (int row : std::as_const(affectedRows))
      emit dataChanged(index(row), index(row), {NextBlockTypeRole});

    for (int row : std::as_const(numberStarts))
      emitNumberIndexChanged(row);

    emit countChanged();
  }
}

bool BlockModel::applyPatchOp(const PatchOp &op, bool inverse) {
  switch (op.type) {
  case PatchOp::Type::Insert:
    if (inverse) {
      const int row = m_document.indexOf(op.block.id);

      if (row >= 0) {
        beginRemoveRows({}, row, row);
        const bool removed = m_document.removeBlock(op.block.id);
        endRemoveRows();
        return removed;
      }
    } else {
      // Validate before announcing the row: a rejected insert used to still
      // emit begin/endInsertRows and desync the view from the document.
      if (op.to < 0 || op.to > m_document.count() ||
          op.block.id.isEmpty() || m_document.indexOf(op.block.id) >= 0)
        break;

      beginInsertRows({}, op.to, op.to);
      const bool inserted = m_document.insertBlock(op.to, op.block);
      endInsertRows();
      return inserted;
    }
    break;

  case PatchOp::Type::Remove:
    if (inverse) {
      if (op.from < 0 || op.from > m_document.count() ||
          op.block.id.isEmpty() || m_document.indexOf(op.block.id) >= 0)
        break;

      beginInsertRows({}, op.from, op.from);
      const bool inserted = m_document.insertBlock(op.from, op.block);
      endInsertRows();
      return inserted;
    }

    if (const int row = m_document.indexOf(op.block.id); row >= 0) {
      beginRemoveRows({}, row, row);
      const bool removed = m_document.removeBlock(op.block.id);
      endRemoveRows();
      return removed;
    }
    break;

  case PatchOp::Type::Move: {
    const int from = m_document.indexOf(op.block.id);
    const int to = inverse ? op.from : op.to;

    if (from < 0 || from == to || to < 0 || to >= m_document.count())
      break;

    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    m_document.moveBlock(op.block.id, to);
    endMoveRows();
    return true;
  }

  case PatchOp::Type::Update: {
    const Block &wanted = inverse ? op.beforeBlock : op.block;
    const int row = m_document.indexOf(wanted.id);

    if (row < 0)
      break;

    const Block *current = m_document.blockAt(row);

    if (!current)
      break;

    const bool textChanged = current->text != wanted.text;
    const bool typeChanged = current->type != wanted.type ||
                             current->headingLevel != wanted.headingLevel;

    QVector<int> roles;

    if (textChanged)
      roles << ContentRole << Qt::DisplayRole;
    if (typeChanged)
      roles << BlockTypeRole << HeadingLevelRole << NumberIndexRole;
    if (current->parentId != wanted.parentId ||
        current->metadata != wanted.metadata)
      roles << ParentIdRole << MetadataRole << AlignmentRole << CheckedRole;

    if (roles.isEmpty())
      break;

    m_document.setBlock(row, wanted);
    emit dataChanged(index(row), index(row), roles);

    // Neighbour-dependent roles also change when a block type changes: the
    // previous block's "next type" and the numbering of following rows.
    if (typeChanged) {
      emitNumberIndexChanged(row);

      if (row > 0)
        emit dataChanged(index(row - 1), index(row - 1),
                         {NextBlockTypeRole});

      return true;
    }

    return false;
  }
  }

  return false;
}

bool BlockModel::undo() {
  if (m_undoStack.empty())
    return false;

  closeTextGroup();

  const HistoryEntry entry = m_undoStack.back();
  m_undoStack.pop_back();
  m_redoStack.push_back(entry);

  applyHistoryEntry(entry, true);
  ++m_contentRevision;
  emit contentRevisionChanged();

  m_currentStateId = entry.stateBeforeId;
  syncModifiedState();
  emit historyChanged();
  return true;
}

bool BlockModel::redo() {
  if (m_redoStack.empty())
    return false;

  closeTextGroup();

  const HistoryEntry entry = m_redoStack.back();
  m_redoStack.pop_back();
  m_undoStack.push_back(entry);

  applyHistoryEntry(entry, false);
  ++m_contentRevision;
  emit contentRevisionChanged();

  m_currentStateId = entry.stateAfterId;
  syncModifiedState();
  emit historyChanged();
  return true;
}

} // namespace Templnote
