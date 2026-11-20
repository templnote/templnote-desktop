#include "BlockModel.h"
#include "../tracking/ScopedTimer.h"
#include "../tracking/MemoryTracker.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QTextBoundaryFinder>
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

void BlockModel::invalidateNumberIndices() { m_numberIndicesDirty = true; }

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

  invalidateNumberIndices();

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

bool BlockModel::insertBlock(int row, int type, const QString &text) {
  if (!isValidBlockType(type) || row < 0 || row > m_document.count()) {
    return false;
  }

  QString focusBeforeId;

  if (m_document.count() > 0)
    focusBeforeId = m_document.blockAt(qBound(0, row - 1,
                                              m_document.count() - 1))->id;

  invalidateNumberIndices();

  beginInsertRows({}, row, row);

  const Block block = Block::create(static_cast<BlockType>(type), text);
  const QString id = block.id;

  const bool success = m_document.insertBlock(row, block);

  endInsertRows();

  if (!success)
    return false;

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

  return true;
}

bool BlockModel::removeBlock(const QString &id) {
  const int row = m_document.indexOf(id);

  if (row < 0)
    return false;

  const Block removed = *m_document.blockAt(row);

  invalidateNumberIndices();

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

  PatchOp op;
  op.type = PatchOp::Type::Remove;
  op.from = row;
  op.block = removed;
  saveHistory({op}, id, focusAfterId);

  if (success && row > 0)
    emit dataChanged(index(row - 1), index(row - 1), {NextBlockTypeRole});

  if (success)
    emitNumberIndexChanged(row);

  markModified();
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

  invalidateNumberIndices();

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

  const Block beforePrevious = *m_document.blockAt(row - 1);

  if (!editorText.isNull() &&
      !m_document.updateBlockText(id, editorText)) {
    return false;
  }

  const Block removed = *m_document.blockAt(row);

  invalidateNumberIndices();

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

  invalidateNumberIndices();

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

bool BlockModel::moveBlock(const QString &id, int destination) {
  const int source = m_document.indexOf(id);

  if (source < 0 || destination < 0 || destination >= m_document.count()) {
    return false;
  }

  if (source == destination)
    return true;

  const Block moved = *m_document.blockAt(source);

  invalidateNumberIndices();

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

    if (first < m_document.count())
      emitNumberIndexChanged(first);

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
  for (int row = qMax(0, firstRow); row < m_document.count(); ++row) {
    const Block *block = m_document.blockAt(row);

    if (block && block->type == BlockType::NumberedList)
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
  invalidateNumberIndices();
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
  if (m_modified && !saveCurrentDocument())
    return false;

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

  invalidateNumberIndices();

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

    for (const PatchOp &op : patch) {
      for (int offset = -2; offset <= 2; ++offset) {
        const int fromRow = op.from + offset;
        const int toRow = op.to + offset;

        if (fromRow >= 0 && fromRow < m_document.count())
          affectedRows.insert(fromRow);
        if (toRow >= 0 && toRow < m_document.count())
          affectedRows.insert(toRow);
      }
    }

    for (int row : std::as_const(affectedRows))
      emit dataChanged(index(row), index(row), {NextBlockTypeRole});

    emitNumberIndexChanged(0);

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

  m_currentStateId = entry.stateAfterId;
  syncModifiedState();
  emit historyChanged();
  return true;
}

} // namespace Templnote
