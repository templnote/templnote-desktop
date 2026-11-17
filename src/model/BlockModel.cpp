#include "BlockModel.h"
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

bool BlockModel::canUndo() const { return !m_undoStack.isEmpty(); }
bool BlockModel::canRedo() const { return !m_redoStack.isEmpty(); }

void BlockModel::saveHistory(const QVector<Block> &before,
                             const QString &focusBeforeId,
                             const QString &focusAfterId) {
  HistoryEntry entry;
  entry.kind = HistoryEntry::Kind::StructuralEdit;
  entry.focusBeforeId = focusBeforeId;
  entry.focusAfterId = focusAfterId;
  entry.patch = buildPatch(before, m_document.blocks());

  m_undoStack.append(entry);
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
    m_undoStack.removeFirst();
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
          {NextBlockTypeRole, "nextBlockType"},
          {NumberIndexRole, "numberIndex"}};
}

QString BlockModel::appendBlock(int type, const QString &text) {
  if (!isValidBlockType(type))
    return {};

  const int row = m_document.count();

  const QVector<Block> before = m_document.snapshot();

  invalidateNumberIndices();

  beginInsertRows({}, row, row);

  Block block = Block::create(static_cast<BlockType>(type), text);

  const QString id = block.id;

  m_document.appendBlock(block);

  endInsertRows();

  const QString focusBeforeId = before.isEmpty() ? QString() : before.last().id;
  saveHistory(before, focusBeforeId, id);

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

  const QVector<Block> before = m_document.snapshot();

  invalidateNumberIndices();

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

  markModified();
  emit countChanged();

  return true;
}

bool BlockModel::removeBlock(const QString &id) {
  const int row = m_document.indexOf(id);

  if (row < 0)
    return false;

  const QVector<Block> before = m_document.snapshot();

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

  saveHistory(before, id, focusAfterId);

  if (success && row > 0)
    emit dataChanged(index(row - 1), index(row - 1), {NextBlockTypeRole});

  if (success && row < m_document.count()) {
    emit dataChanged(index(row), index(m_document.count() - 1),
                     {NumberIndexRole});
  }

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

  const QVector<Block> before = m_document.snapshot();

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

  saveHistory(before, id, newId);

  emit dataChanged(index(row), index(row),
                   {ContentRole, Qt::DisplayRole, NextBlockTypeRole});

  if (row + 2 < m_document.count()) {
    emit dataChanged(index(row + 2), index(m_document.count() - 1),
                     {NumberIndexRole});
  }

  markModified();
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

  invalidateNumberIndices();

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

  const QVector<Block> before = m_document.snapshot();

  invalidateNumberIndices();

  if (!m_document.updateBlockType(id, static_cast<BlockType>(type),
                                  headingLevel)) {
    return false;
  }

  saveHistory(before, id, id);
  emit dataChanged(index(row), index(m_document.count() - 1),
                   {BlockTypeRole, HeadingLevelRole, NumberIndexRole});

  if (row > 0)
    emit dataChanged(index(row - 1), index(row - 1), {NextBlockTypeRole});

  markModified();
  return true;
}

bool BlockModel::moveBlock(const QString &id, int destination) {
  const int source = m_document.indexOf(id);

  if (source < 0 || destination < 0 || destination >= m_document.count()) {
    return false;
  }

  if (source == destination)
    return true;

  const QVector<Block> before = m_document.snapshot();

  invalidateNumberIndices();

  if (!beginMoveRows({}, source, source, {},
                     destination > source ? destination + 1 : destination)) {
    return false;
  }

  const bool success = m_document.moveBlock(id, destination);

  endMoveRows();

  if (success) {
    saveHistory(before, id, id);

    const int first = qMin(source, destination);
    const int last = qMin(m_document.count() - 1, qMax(source, destination));

    if (first <= last)
      emit dataChanged(index(first), index(last), {NextBlockTypeRole});

    if (first < m_document.count())
      emit dataChanged(index(first), index(m_document.count() - 1),
                       {NumberIndexRole});

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

void BlockModel::markModified() {
  setModified(true);
  scheduleAutosave();
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

  setCurrentFilePath(absolutePath);
  setLastModified(modified.toLocalTime());
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
  emit documentLoaded();

  return true;
}

bool BlockModel::createDocument(const QString &path) {
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

QVector<BlockModel::PatchOp>
BlockModel::buildPatch(const QVector<Block> &before,
                       const QVector<Block> &after) {
  QVector<PatchOp> ops;
  QVector<Block> working = before;

  // Remove blocks that no longer exist in the target document.
  QSet<QString> targetIds;
  targetIds.reserve(after.size());

  for (const Block &block : after)
    targetIds.insert(block.id);

  int row = 0;

  while (row < working.size()) {
    if (!targetIds.contains(working[row].id)) {
      PatchOp op;
      op.type = PatchOp::Type::Remove;
      op.from = row;
      op.block = working[row];
      ops.append(op);
      working.removeAt(row);
    } else {
      ++row;
    }
  }

  // Align block order: move existing blocks and insert new ones. A position
  // map avoids rescanning the vector for every row.
  QHash<QString, int> position;
  position.reserve(working.size());

  for (int i = 0; i < working.size(); ++i)
    position.insert(working[i].id, i);

  row = 0;

  while (row < working.size()) {
    if (row >= after.size()) {
      while (working.size() > row) {
        PatchOp op;
        op.type = PatchOp::Type::Remove;
        op.from = row;
        op.block = working[row];
        ops.append(op);
        working.removeAt(row);
      }
      break;
    }

    if (working[row].id == after[row].id) {
      ++row;
      continue;
    }

    const int existing = position.value(after[row].id, -1);

    if (existing >= 0) {
      PatchOp op;
      op.type = PatchOp::Type::Move;
      op.from = existing;
      op.to = row;
      op.block = working[existing];
      ops.append(op);

      const Block moved = working.takeAt(existing);
      working.insert(row, moved);

      const int first = qMin(existing, row);
      const int last = qMax(existing, row);

      for (int i = first; i <= last; ++i)
        position.insert(working[i].id, i);
    } else {
      PatchOp op;
      op.type = PatchOp::Type::Insert;
      op.to = row;
      op.block = after[row];
      ops.append(op);
      working.insert(row, after[row]);

      for (int i = row; i < working.size(); ++i)
        position.insert(working[i].id, i);
    }

    ++row;
  }

  // Append blocks that only exist in the target.
  while (working.size() < after.size()) {
    const int appendRow = working.size();

    PatchOp op;
    op.type = PatchOp::Type::Insert;
    op.to = appendRow;
    op.block = after[appendRow];
    ops.append(op);
    working.append(after[appendRow]);
    position.insert(after[appendRow].id, appendRow);
  }

  // Update text and block properties for blocks present in both documents.
  for (int r = 0; r < after.size() && r < working.size(); ++r) {
    const Block &current = working[r];
    const Block &wanted = after[r];

    if (current.text == wanted.text && current.type == wanted.type &&
        current.headingLevel == wanted.headingLevel &&
        current.parentId == wanted.parentId &&
        current.metadata == wanted.metadata) {
      continue;
    }

    PatchOp op;
    op.type = PatchOp::Type::Update;
    op.block = wanted;
    op.beforeBlock = current;
    ops.append(op);
  }

  return ops;
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
    if (m_document.count() > 0) {
      // Neighbour-dependent roles change for every affected row.
      emit dataChanged(index(0), index(m_document.count() - 1),
                       {NextBlockTypeRole, NumberIndexRole});
    }

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
        m_document.removeBlock(op.block.id);
        endRemoveRows();
        return true;
      }
    } else {
      beginInsertRows({}, op.to, op.to);
      m_document.insertBlock(op.to, op.block);
      endInsertRows();
      return true;
    }
    break;

  case PatchOp::Type::Remove:
    if (inverse) {
      beginInsertRows({}, op.from, op.from);
      m_document.insertBlock(op.from, op.block);
      endInsertRows();
      return true;
    }

    if (const int row = m_document.indexOf(op.block.id); row >= 0) {
      beginRemoveRows({}, row, row);
      m_document.removeBlock(op.block.id);
      endRemoveRows();
      return true;
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
      roles << BlockTypeRole << HeadingLevelRole;
    if (current->parentId != wanted.parentId ||
        current->metadata != wanted.metadata)
      roles << ParentIdRole << MetadataRole;

    if (roles.isEmpty())
      break;

    m_document.setBlock(row, wanted);
    emit dataChanged(index(row), index(row), roles);

    // Neighbour-dependent roles also change when a block type changes: the
    // previous block's "next type" and the numbering of following rows.
    if (typeChanged) {
      if (row + 1 < m_document.count()) {
        emit dataChanged(index(row + 1), index(m_document.count() - 1),
                         {NumberIndexRole});
      }

      if (row > 0)
        emit dataChanged(index(row - 1), index(row - 1),
                         {NextBlockTypeRole});
    }

    return false;
  }
  }

  return false;
}

bool BlockModel::undo() {
  if (m_undoStack.isEmpty())
    return false;

  closeTextGroup();

  const HistoryEntry entry = m_undoStack.takeLast();
  m_redoStack.append(entry);

  applyHistoryEntry(entry, true);

  markModified();
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

  markModified();
  emit historyChanged();
  return true;
}

} // namespace Templnote
