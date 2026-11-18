#include "SearchIndex.h"

#include "../core/Document.h"
#include "../model/BlockModel.h"
#include "../storage/AppPaths.h"
#include "../storage/DocumentListModel.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPair>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariantMap>
#include <QtConcurrent>
#include <algorithm>
#include <atomic>
#include <utility>

namespace Templnote {
namespace {
constexpr int kFlushDelayMs = 350;
constexpr int kMaxResults = 200;

QString escapeLike(QString value) {
  value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
  value.replace(QLatin1Char('%'), QStringLiteral("\\%"));
  value.replace(QLatin1Char('_'), QStringLiteral("\\_"));
  return value;
}

// Self-contained description of one write, so a batch can be executed on a
// worker thread without touching the model or the GUI connection.
struct WriteBlock {
  QString blockId;
  QString text;
};

struct WriteOp {
  enum class Type {
    Upsert,
    Remove,
    Replace,
    FileReplace,
    RemoveDocument,
    RemovePrefix,
    Rename
  };

  Type type = Type::Upsert;
  QString path;
  QString newPath;
  QString title;
  QString blockId;
  QString text;
  QVector<WriteBlock> blocks;
  bool updateMeta = false;
};

QString normalizePath(const QString &path) {
  if (path.isEmpty())
    return {};

  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

// Mirrors the token characters used by the FTS tokenizer closely enough for
// highlight matching: word characters start a token, punctuation splits it.
bool isTokenCharacter(QChar character) {
  return character.isLetterOrNumber();
}

// Deletes the FTS rows belonging to `path` through the rowid map. Deleting
// from an FTS5 table by a non-rowid column scans the whole table, so every
// document/block removal goes through search_rows instead.
bool row_deleteDocumentRows(QSqlDatabase &db, const QString &path) {
  QSqlQuery removeIndex(db);
  removeIndex.prepare(QStringLiteral(
      "DELETE FROM search_index WHERE rowid IN "
      "(SELECT rowid FROM search_rows WHERE document_path = ?)"));
  removeIndex.addBindValue(path);

  QSqlQuery removeRows(db);
  removeRows.prepare(
      QStringLiteral("DELETE FROM search_rows WHERE document_path = ?"));
  removeRows.addBindValue(path);

  return removeIndex.exec() && removeRows.exec();
}

bool row_deleteBlockRow(QSqlDatabase &db, const QString &path,
                        const QString &blockId) {
  QSqlQuery removeIndex(db);
  removeIndex.prepare(QStringLiteral(
      "DELETE FROM search_index WHERE rowid IN "
      "(SELECT rowid FROM search_rows WHERE document_path = ? AND "
      "block_id = ?)"));
  removeIndex.addBindValue(path);
  removeIndex.addBindValue(blockId);

  QSqlQuery removeRows(db);
  removeRows.prepare(QStringLiteral(
      "DELETE FROM search_rows WHERE document_path = ? AND block_id = ?"));
  removeRows.addBindValue(path);
  removeRows.addBindValue(blockId);

  return removeIndex.exec() && removeRows.exec();
}

bool row_removePathPrefix(QSqlDatabase &db, const QString &path) {
  const QString prefix = escapeLike(path) + QStringLiteral("/%");

  QSqlQuery removeIndex(db);
  removeIndex.prepare(QStringLiteral(
      "DELETE FROM search_index WHERE rowid IN "
      "(SELECT rowid FROM search_rows WHERE document_path = ? OR "
      "document_path LIKE ? ESCAPE '\\')"));
  removeIndex.addBindValue(path);
  removeIndex.addBindValue(prefix);

  QSqlQuery removeRows(db);
  removeRows.prepare(QStringLiteral(
      "DELETE FROM search_rows WHERE document_path = ? OR "
      "document_path LIKE ? ESCAPE '\\'"));
  removeRows.addBindValue(path);
  removeRows.addBindValue(prefix);

  QSqlQuery removeMeta(db);
  removeMeta.prepare(QStringLiteral(
      "DELETE FROM search_meta WHERE document_path = ? OR document_path "
      "LIKE ? ESCAPE '\\'"));
  removeMeta.addBindValue(path);
  removeMeta.addBindValue(prefix);

  return removeIndex.exec() && removeRows.exec() && removeMeta.exec();
}

bool row_renamePath(QSqlDatabase &db, const QString &oldPath,
                    const QString &newPath) {
  if (oldPath.isEmpty() || newPath.isEmpty() || oldPath == newPath)
    return false;

  const QString prefix = escapeLike(oldPath) + QStringLiteral("/%");
  const QString newTitle = QFileInfo(newPath).completeBaseName();
  const int suffixStart = oldPath.length() + 1;

  QSqlQuery updateIndex(db);
  updateIndex.prepare(QStringLiteral(
      "UPDATE search_index SET "
      "document_path = ? || substr(document_path, ?), "
      "document_title = CASE WHEN document_path = ? THEN ? "
      "ELSE document_title END "
      "WHERE rowid IN (SELECT rowid FROM search_rows "
      "WHERE document_path = ? OR document_path LIKE ? ESCAPE '\\')"));
  updateIndex.addBindValue(newPath);
  updateIndex.addBindValue(suffixStart);
  updateIndex.addBindValue(oldPath);
  updateIndex.addBindValue(newTitle);
  updateIndex.addBindValue(oldPath);
  updateIndex.addBindValue(prefix);

  QSqlQuery updateRows(db);
  updateRows.prepare(QStringLiteral(
      "UPDATE search_rows SET document_path = ? || substr(document_path, ?) "
      "WHERE document_path = ? OR document_path LIKE ? ESCAPE '\\'"));
  updateRows.addBindValue(newPath);
  updateRows.addBindValue(suffixStart);
  updateRows.addBindValue(oldPath);
  updateRows.addBindValue(prefix);

  QSqlQuery updateMeta(db);
  updateMeta.prepare(QStringLiteral(
      "UPDATE search_meta SET "
      "document_path = ? || substr(document_path, ?), "
      "document_title = CASE WHEN document_path = ? THEN ? "
      "ELSE document_title END "
      "WHERE document_path = ? OR document_path LIKE ? ESCAPE '\\'"));
  updateMeta.addBindValue(newPath);
  updateMeta.addBindValue(suffixStart);
  updateMeta.addBindValue(oldPath);
  updateMeta.addBindValue(newTitle);
  updateMeta.addBindValue(oldPath);
  updateMeta.addBindValue(prefix);

  return updateIndex.exec() && updateRows.exec() && updateMeta.exec();
}

bool row_removeDocument(QSqlDatabase &db, const QString &path) {
  if (!row_deleteDocumentRows(db, path))
    return false;

  QSqlQuery removeMeta(db);
  removeMeta.prepare(
      QStringLiteral("DELETE FROM search_meta WHERE document_path = ?"));
  removeMeta.addBindValue(path);

  return removeMeta.exec();
}

bool row_replaceDocument(QSqlDatabase &db, const QString &path,
                         const QString &title,
                         const QVector<WriteBlock> &blocks, bool updateMeta) {
  if (!row_deleteDocumentRows(db, path))
    return false;

  QSqlQuery insert(db);
  insert.prepare(QStringLiteral(
      "INSERT INTO search_index(document_path, document_title, block_id, "
      "block_text) VALUES(?, ?, ?, ?)"));

  QSqlQuery insertRow(db);
  insertRow.prepare(QStringLiteral(
      "INSERT INTO search_rows(document_path, block_id, rowid) "
      "VALUES(?, ?, ?)"));

  for (const WriteBlock &block : blocks) {
    if (block.text.isEmpty())
      continue;

    insert.addBindValue(path);
    insert.addBindValue(title);
    insert.addBindValue(block.blockId);
    insert.addBindValue(block.text);

    if (!insert.exec())
      return false;

    insertRow.addBindValue(path);
    insertRow.addBindValue(block.blockId);
    insertRow.addBindValue(insert.lastInsertId().toLongLong());

    if (!insertRow.exec())
      return false;
  }

  if (updateMeta) {
    const QFileInfo info(path);
    const qint64 modified =
        info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0;
    const qint64 size = info.exists() ? info.size() : 0;

    QSqlQuery removeMeta(db);
    removeMeta.prepare(
        QStringLiteral("DELETE FROM search_meta WHERE document_path = ?"));
    removeMeta.addBindValue(path);

    if (!removeMeta.exec())
      return false;

    QSqlQuery insertMeta(db);
    insertMeta.prepare(QStringLiteral(
        "INSERT INTO search_meta(document_path, document_title, modified_ms, "
        "size) VALUES(?, ?, ?, ?)"));
    insertMeta.addBindValue(path);
    insertMeta.addBindValue(title);
    insertMeta.addBindValue(modified);
    insertMeta.addBindValue(size);

    if (!insertMeta.exec())
      return false;
  } else {
    // The index contents did not come from the file (for example the open
    // document with unsaved edits). Drop the metadata so the next startup
    // reindexes it from disk instead of trusting the in-memory snapshot.
    QSqlQuery removeMeta(db);
    removeMeta.prepare(
        QStringLiteral("DELETE FROM search_meta WHERE document_path = ?"));
    removeMeta.addBindValue(path);

    if (!removeMeta.exec())
      return false;
  }

  return true;
}

bool row_upsertBlock(QSqlDatabase &db, const QString &path,
                     const QString &title, const QString &blockId,
                     const QString &text) {
  QSqlQuery remove(db);
  remove.prepare(QStringLiteral(
      "DELETE FROM search_index WHERE document_path = ? AND block_id = ?"));
  remove.addBindValue(path);
  remove.addBindValue(blockId);

  if (!remove.exec())
    return false;

  if (!text.isEmpty()) {
    QSqlQuery insert(db);
    insert.prepare(QStringLiteral(
        "INSERT INTO search_index(document_path, document_title, block_id, "
        "block_text) VALUES(?, ?, ?, ?)"));
    insert.addBindValue(path);
    insert.addBindValue(title);
    insert.addBindValue(blockId);
    insert.addBindValue(text);

    if (!insert.exec())
      return false;
  }

  QSqlQuery invalidate(db);
  invalidate.prepare(
      QStringLiteral("DELETE FROM search_meta WHERE document_path = ?"));
  invalidate.addBindValue(path);

  return invalidate.exec();
}

bool row_removeBlock(QSqlDatabase &db, const QString &path,
                     const QString &blockId) {
  QSqlQuery remove(db);
  remove.prepare(QStringLiteral(
      "DELETE FROM search_index WHERE document_path = ? AND block_id = ?"));
  remove.addBindValue(path);
  remove.addBindValue(blockId);

  if (!remove.exec())
    return false;

  QSqlQuery invalidate(db);
  invalidate.prepare(
      QStringLiteral("DELETE FROM search_meta WHERE document_path = ?"));
  invalidate.addBindValue(path);

  return invalidate.exec();
}

bool replaceFromFile(QSqlDatabase &db, const QString &path) {
  const QString normalized = normalizePath(path);
  QFile file(normalized);

  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    row_removeDocument(db, normalized);
    return false;
  }

  QJsonParseError parseError{};
  const QJsonDocument parsed =
      QJsonDocument::fromJson(file.readAll(), &parseError);
  file.close();

  if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
    row_removeDocument(db, normalized);
    return false;
  }

  Document document;
  QString documentError;

  if (!document.fromJson(parsed.object(), &documentError)) {
    row_removeDocument(db, normalized);
    return false;
  }

  QString title =
      parsed.object().value(QStringLiteral("title")).toString().trimmed();

  if (title.isEmpty())
    title = QFileInfo(normalized).completeBaseName();

  QVector<WriteBlock> blocks;
  blocks.reserve(document.blocks().size());

  for (const Block &block : document.blocks())
    blocks.append({block.id, block.text});

  return row_replaceDocument(db, normalized, title, blocks, true);
}

void performIncrementalSync(QSqlDatabase &db, const QString &root,
                            const QString &currentDocumentPath,
                            const QString &currentDocumentTitle,
                            const QVector<WriteBlock> &currentBlocks) {
  if (!db.isOpen() || root.isEmpty())
    return;

  if (!db.transaction())
    return;

  bool ok = true;
  QSet<QString> found;

  // Read all metadata in one query instead of issuing one SELECT per file.
  // The scan below only needs the previously recorded timestamp/size.
  QHash<QString, QPair<qint64, qint64>> metadata;
  {
    QSqlQuery query(db);

    if (query.exec(QStringLiteral(
            "SELECT document_path, modified_ms, size FROM search_meta"))) {
      metadata.reserve(256);

      while (query.next()) {
        metadata.insert(query.value(0).toString(),
                        {query.value(1).toLongLong(),
                         query.value(2).toLongLong()});
      }
    }
  }

  QDirIterator it(root, QDir::Files | QDir::Readable,
                  QDirIterator::Subdirectories);

  while (ok && it.hasNext()) {
    const QFileInfo info(it.next());

    if (info.suffix().compare(QStringLiteral("templnote"),
                              Qt::CaseInsensitive) != 0) {
      continue;
    }

    const QString path = normalizePath(info.absoluteFilePath());
    found.insert(path);

    if (!currentDocumentPath.isEmpty() && path == currentDocumentPath) {
      ok = row_replaceDocument(db, path, currentDocumentTitle, currentBlocks,
                               false);
      continue;
    }

    const auto meta = metadata.constFind(path);
    const qint64 currentModified = info.lastModified().toMSecsSinceEpoch();
    const qint64 currentSize = info.size();

    if (meta == metadata.constEnd() || meta->first != currentModified ||
        meta->second != currentSize) {
      ok = replaceFromFile(db, path);
    }
  }

  // Remove index entries and metadata for files that disappeared.
  QSet<QString> knownPaths;

  if (ok) {
    QSqlQuery query(db);

    if (query.exec(QStringLiteral("SELECT document_path FROM search_meta"))) {
      while (query.next())
        knownPaths.insert(query.value(0).toString());
    }

    if (query.exec(
            QStringLiteral("SELECT DISTINCT document_path FROM search_index"))) {
      while (query.next())
        knownPaths.insert(query.value(0).toString());
    }
  }

  for (const QString &path : std::as_const(knownPaths)) {
    if (!found.contains(path))
      ok = row_removeDocument(db, path) && ok;
  }

  if (!ok)
    db.rollback();
  else if (!db.commit())
    db.rollback();
}

void executeIncrementalSync(const QString &databasePath, const QString &root,
                            const QString &currentDocumentPath,
                            const QString &currentDocumentTitle,
                            const QVector<WriteBlock> &currentBlocks) {
  static std::atomic<quint64> syncCounter{0};

  const QString connectionName =
      QStringLiteral("templnote_search_sync_%1")
          .arg(syncCounter.fetch_add(1));

  {
    QSqlDatabase db =
        QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(databasePath);

    if (db.open()) {
      QSqlQuery pragma(db);
      pragma.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
      performIncrementalSync(db, root, currentDocumentPath,
                             currentDocumentTitle, currentBlocks);
    }

    db.close();
  }

  QSqlDatabase::removeDatabase(connectionName);
}

void executeWriteBatch(const QString &databasePath, const QVector<WriteOp> &ops) {
  static std::atomic<quint64> connectionCounter{0};

  const QString connectionName =
      QStringLiteral("templnote_search_write_%1")
          .arg(connectionCounter.fetch_add(1));

  bool ok = false;

  {
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                               connectionName);
    db.setDatabaseName(databasePath);

    if (db.open()) {
      QSqlQuery pragma(db);
      pragma.exec(QStringLiteral("PRAGMA busy_timeout=5000"));

      if (db.transaction()) {
        bool batchOk = true;

        for (const WriteOp &op : ops) {
          switch (op.type) {
          case WriteOp::Type::Upsert:
            batchOk = row_upsertBlock(db, op.path, op.title, op.blockId,
                                      op.text);
            break;
          case WriteOp::Type::Remove:
            batchOk = row_removeBlock(db, op.path, op.blockId);
            break;
          case WriteOp::Type::Replace:
            batchOk = row_replaceDocument(db, op.path, op.title, op.blocks,
                                          op.updateMeta);
            break;
          case WriteOp::Type::FileReplace:
            batchOk = replaceFromFile(db, op.path);
            break;
          case WriteOp::Type::RemoveDocument:
            batchOk = row_removeDocument(db, op.path);
            break;
          case WriteOp::Type::RemovePrefix:
            batchOk = row_removePathPrefix(db, op.path);
            break;
          case WriteOp::Type::Rename:
            batchOk = row_renamePath(db, op.path, op.newPath);
            break;
          }

          if (!batchOk)
            break;
        }

        ok = batchOk && db.commit();

        if (!ok)
          db.rollback();
      }
    }

    db.close();
  }

  QSqlDatabase::removeDatabase(connectionName);

  if (!ok)
    qWarning() << "SearchIndex: background write batch failed";
}
} // namespace

SearchIndex::SearchIndex(QObject *parent)
    : SearchIndex(
          AppPaths::documentsDirectory(),
          AppPaths::cacheDirectory().isEmpty()
              ? QString()
              : QDir(AppPaths::cacheDirectory())
                    .filePath(QStringLiteral("search-index.sqlite")),
          parent) {}

SearchIndex::SearchIndex(const QString &documentsRoot,
                         const QString &databasePath, QObject *parent)
    : QObject(parent), m_connectionName(connectionNameFor(this)),
      m_documentsRoot(QDir::cleanPath(documentsRoot)),
      m_databasePath(databasePath) {
  if (!openDatabase()) {
    qWarning() << "SearchIndex: could not open the search database";
    return;
  }

  if (!createSchema()) {
    qWarning() << "SearchIndex: could not create the search schema; "
                  "recreating the database";
    if (!resetDatabaseFile() || !createSchema()) {
      qWarning() << "SearchIndex: search is unavailable";
      return;
    }
  }

  m_flushTimer.setSingleShot(true);
  m_flushTimer.setInterval(kFlushDelayMs);
  connect(&m_flushTimer, &QTimer::timeout, this, &SearchIndex::flushPending);

  // One writer keeps SQLite writes ordered; other writes (startup sync,
  // rename, delete) wait for the worker before touching the database.
  m_writePool.setMaxThreadCount(1);

  syncIncremental();
}

SearchIndex::~SearchIndex() {
  flushPending();
  m_writePool.waitForDone();

  if (m_db.isOpen())
    m_db.close();

  m_db = QSqlDatabase();
  QSqlDatabase::removeDatabase(m_connectionName);
}

BlockModel *SearchIndex::blockModel() const { return m_blockModel; }

void SearchIndex::setBlockModel(BlockModel *model) {
  if (m_blockModel == model)
    return;

  disconnectModelSignals();
  m_blockModel = model;
  connectModelSignals();

  m_currentDocumentPath =
      model ? normalizedPath(model->currentFilePath()) : QString();

  if (!m_currentDocumentPath.isEmpty())
    queueFullDocument(m_currentDocumentPath);

  emit blockModelChanged();
}

DocumentListModel *SearchIndex::documentList() const { return m_documentList; }

void SearchIndex::setDocumentList(DocumentListModel *model) {
  if (m_documentList == model)
    return;

  if (m_documentList) {
    disconnect(m_documentList, &DocumentListModel::pathRenamed, this,
               &SearchIndex::onPathRenamed);
    disconnect(m_documentList, &DocumentListModel::pathRemoved, this,
               &SearchIndex::onPathRemoved);
  }

  m_documentList = model;

  if (m_documentList) {
    connect(m_documentList, &DocumentListModel::pathRenamed, this,
            &SearchIndex::onPathRenamed);
    connect(m_documentList, &DocumentListModel::pathRemoved, this,
            &SearchIndex::onPathRemoved);
  }

  emit documentListChanged();
}

bool SearchIndex::openDatabase() {
  if (m_databasePath.isEmpty())
    return false;

  const QString directory = QFileInfo(m_databasePath).absolutePath();

  if (directory.isEmpty() || !QDir().mkpath(directory))
    return false;

  m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  m_db.setDatabaseName(m_databasePath);

  if (!m_db.open())
    return false;

  QSqlQuery pragma(m_db);
  pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
  pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));

  return true;
}

bool SearchIndex::createSchema() {
  QSqlQuery query(m_db);

  if (!query.exec(QStringLiteral(
          "CREATE VIRTUAL TABLE IF NOT EXISTS search_index USING fts5("
          "document_path UNINDEXED,"
          "document_title,"
          "block_id UNINDEXED,"
          "block_text)"))) {
    qWarning() << "SearchIndex: create search_index failed:"
               << query.lastError().text();
    return false;
  }

  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS search_meta("
          "document_path TEXT PRIMARY KEY,"
          "document_title TEXT,"
          "modified_ms INTEGER,"
          "size INTEGER)"))) {
    qWarning() << "SearchIndex: create search_meta failed:"
               << query.lastError().text();
    return false;
  }

  return true;
}

bool SearchIndex::resetDatabaseFile() {
  const QString path = m_db.databaseName();

  if (m_db.isOpen())
    m_db.close();

  m_db = QSqlDatabase();
  QSqlDatabase::removeDatabase(m_connectionName);

  if (!path.isEmpty()) {
    QFile::remove(path);
    QFile::remove(path + QStringLiteral("-wal"));
    QFile::remove(path + QStringLiteral("-shm"));
  }

  return openDatabase();
}

void SearchIndex::connectModelSignals() {
  if (!m_blockModel)
    return;

  connect(m_blockModel, &QAbstractItemModel::dataChanged, this,
          &SearchIndex::onDataChanged);
  connect(m_blockModel, &QAbstractItemModel::rowsInserted, this,
          &SearchIndex::onRowsInserted);
  connect(m_blockModel, &QAbstractItemModel::rowsAboutToBeRemoved, this,
          &SearchIndex::onRowsAboutToBeRemoved);
  connect(m_blockModel, &BlockModel::currentFilePathChanged, this,
          &SearchIndex::onCurrentFilePathChanged);
  connect(m_blockModel, &BlockModel::documentLoaded, this,
          &SearchIndex::onDocumentLoaded);
  connect(m_blockModel, &BlockModel::documentCreated, this,
          &SearchIndex::onDocumentCreated);
}

void SearchIndex::disconnectModelSignals() {
  if (!m_blockModel)
    return;

  disconnect(m_blockModel, &QAbstractItemModel::dataChanged, this,
             &SearchIndex::onDataChanged);
  disconnect(m_blockModel, &QAbstractItemModel::rowsInserted, this,
             &SearchIndex::onRowsInserted);
  disconnect(m_blockModel, &QAbstractItemModel::rowsAboutToBeRemoved, this,
             &SearchIndex::onRowsAboutToBeRemoved);
  disconnect(m_blockModel, &BlockModel::currentFilePathChanged, this,
             &SearchIndex::onCurrentFilePathChanged);
  disconnect(m_blockModel, &BlockModel::documentLoaded, this,
             &SearchIndex::onDocumentLoaded);
  disconnect(m_blockModel, &BlockModel::documentCreated, this,
             &SearchIndex::onDocumentCreated);
}

void SearchIndex::onDataChanged(const QModelIndex &topLeft,
                                const QModelIndex &bottomRight,
                                const QList<int> &roles) {
  if (!m_blockModel || topLeft.parent().isValid() || !topLeft.isValid() ||
      !bottomRight.isValid()) {
    return;
  }

  if (!roles.isEmpty() && !roles.contains(BlockModel::ContentRole) &&
      !roles.contains(Qt::DisplayRole)) {
    return;
  }

  const QString path = normalizedPath(m_blockModel->currentFilePath());

  if (path.isEmpty())
    return;

  const QString title = m_blockModel->documentTitle();

  for (int row = topLeft.row(); row <= bottomRight.row(); ++row) {
    const QModelIndex index = m_blockModel->index(row);

    const QString blockId =
        m_blockModel->data(index, BlockModel::BlockIdRole).toString();
    const QString text =
        m_blockModel->data(index, BlockModel::ContentRole).toString();

    if (!blockId.isEmpty())
      queueBlockUpsert(path, title, blockId, text);
  }

  scheduleFlush();
}

void SearchIndex::onRowsInserted(const QModelIndex &parent, int first,
                                 int last) {
  if (!m_blockModel || parent.isValid())
    return;

  const QString path = normalizedPath(m_blockModel->currentFilePath());

  if (path.isEmpty())
    return;

  const QString title = m_blockModel->documentTitle();

  for (int row = first; row <= last; ++row) {
    const QModelIndex index = m_blockModel->index(row);

    const QString blockId =
        m_blockModel->data(index, BlockModel::BlockIdRole).toString();
    const QString text =
        m_blockModel->data(index, BlockModel::ContentRole).toString();

    if (!blockId.isEmpty())
      queueBlockUpsert(path, title, blockId, text);
  }

  scheduleFlush();
}

void SearchIndex::onRowsAboutToBeRemoved(const QModelIndex &parent, int first,
                                         int last) {
  if (!m_blockModel || parent.isValid())
    return;

  const QString path = normalizedPath(m_blockModel->currentFilePath());

  if (path.isEmpty())
    return;

  // The rows still exist when this signal is emitted, so capture their ids
  // before the model removes them.
  for (int row = first; row <= last; ++row) {
    const QString blockId =
        m_blockModel->data(m_blockModel->index(row), BlockModel::BlockIdRole)
            .toString();

    if (!blockId.isEmpty())
      queueBlockRemove(path, blockId);
  }

  scheduleFlush();
}

void SearchIndex::onCurrentFilePathChanged() {
  if (!m_blockModel)
    return;

  const QString newPath = normalizedPath(m_blockModel->currentFilePath());

  if (newPath == m_currentDocumentPath)
    return;

  const QString oldPath = m_currentDocumentPath;

  if (!oldPath.isEmpty()) {
    discardPending(oldPath);

    // Switching away from a document should leave its index entry in sync
    // with disk. If the file no longer exists it was renamed or deleted.
    WriteOp write;
    write.path = oldPath;

    if (QFileInfo::exists(oldPath))
      write.type = WriteOp::Type::FileReplace;
    else
      write.type = WriteOp::Type::RemoveDocument;

    const QString databasePath = m_databasePath;

    (void)QtConcurrent::run(
        &m_writePool, [databasePath, write = std::move(write)]() {
          executeWriteBatch(databasePath, {write});
        });
  }

  m_currentDocumentPath = newPath;

  if (!newPath.isEmpty())
    queueFullDocument(newPath);

  scheduleFlush();
}

void SearchIndex::onDocumentLoaded() {
  if (!m_blockModel)
    return;

  const QString path = normalizedPath(m_blockModel->currentFilePath());

  if (!path.isEmpty()) {
    queueFullDocument(path);
    scheduleFlush();
  }
}

void SearchIndex::onDocumentCreated() {
  if (!m_blockModel)
    return;

  const QString path = normalizedPath(m_blockModel->currentFilePath());

  if (!path.isEmpty()) {
    queueFullDocument(path);
    scheduleFlush();
  }
}

void SearchIndex::onPathRenamed(const QString &oldPath,
                                const QString &newPath) {
  const QString oldNormalized = normalizedPath(oldPath);
  const QString newNormalized = normalizedPath(newPath);

  if (oldNormalized.isEmpty() || newNormalized.isEmpty() ||
      oldNormalized == newNormalized) {
    return;
  }

  discardPending(oldNormalized);

  WriteOp op;
  op.type = WriteOp::Type::Rename;
  op.path = oldNormalized;
  op.newPath = newNormalized;

  const QString databasePath = m_databasePath;

  (void)QtConcurrent::run(&m_writePool, [databasePath, op]() {
    executeWriteBatch(databasePath, {op});
  });
}

void SearchIndex::onPathRemoved(const QString &path) {
  const QString normalized = normalizedPath(path);

  if (normalized.isEmpty())
    return;

  discardPending(normalized);

  WriteOp op;
  op.type = WriteOp::Type::RemovePrefix;
  op.path = normalized;

  const QString databasePath = m_databasePath;

  (void)QtConcurrent::run(&m_writePool, [databasePath, op]() {
    executeWriteBatch(databasePath, {op});
  });

  // Deleting a folder moves its children to the root, so the filesystem may
  // contain documents at paths that were not individually reported. Do this
  // in the background; a briefly stale result is acceptable.
  syncIncrementalAsync();
}

void SearchIndex::queueBlockUpsert(const QString &path, const QString &title,
                                   const QString &blockId,
                                   const QString &text) {
  // Collapse repeated edits of the same block before the flush fires. This is
  // what keeps typing from enqueueing one SQL operation per keystroke.
  for (PendingOp &op : m_pending) {
    if (op.path != path)
      continue;

    if (op.type == PendingOp::Type::FullDocument)
      return;

    if (op.blockId == blockId) {
      op.type = PendingOp::Type::Upsert;
      op.title = title;
      op.text = text;
      return;
    }
  }

  PendingOp op;
  op.type = PendingOp::Type::Upsert;
  op.path = path;
  op.title = title;
  op.blockId = blockId;
  op.text = text;
  m_pending.append(op);
}

void SearchIndex::queueBlockRemove(const QString &path,
                                   const QString &blockId) {
  for (PendingOp &op : m_pending) {
    if (op.path != path)
      continue;

    if (op.type == PendingOp::Type::FullDocument)
      return;

    if (op.blockId == blockId) {
      op.type = PendingOp::Type::Remove;
      op.text.clear();
      return;
    }
  }

  PendingOp op;
  op.type = PendingOp::Type::Remove;
  op.path = path;
  op.blockId = blockId;
  m_pending.append(op);
}

void SearchIndex::queueFullDocument(const QString &path) {
  for (const PendingOp &op : std::as_const(m_pending)) {
    if (op.type == PendingOp::Type::FullDocument && op.path == path)
      return;
  }

  // A full reindex supersedes any queued block operations for this document.
  m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(),
                                 [&path](const PendingOp &op) {
                                   return op.path == path &&
                                          op.type !=
                                              PendingOp::Type::FullDocument;
                                 }),
                  m_pending.end());

  PendingOp op;
  op.type = PendingOp::Type::FullDocument;
  op.path = path;
  op.title = QFileInfo(path).completeBaseName();
  m_pending.append(op);
}

void SearchIndex::scheduleFlush() {
  if (m_db.isOpen())
    m_flushTimer.start();
}

void SearchIndex::flushPending() {
  if (m_pending.isEmpty() || !m_db.isOpen())
    return;

  const QVector<PendingOp> pending = m_pending;
  m_pending.clear();

  // Snapshot everything the worker needs. A full reindex of the open document
  // must read the model on the GUI thread; file-backed reindexes can read the
  // file on the worker.
  QVector<WriteOp> ops;
  ops.reserve(pending.size());

  for (const PendingOp &op : pending) {
    WriteOp write;
    write.path = op.path;
    write.title = op.title;
    write.blockId = op.blockId;
    write.text = op.text;

    switch (op.type) {
    case PendingOp::Type::Upsert:
      write.type = WriteOp::Type::Upsert;
      break;
    case PendingOp::Type::Remove:
      write.type = WriteOp::Type::Remove;
      break;
    case PendingOp::Type::FullDocument:
      if (op.path == m_currentDocumentPath && m_blockModel) {
        write.type = WriteOp::Type::Replace;
        write.title = m_blockModel->documentTitle();
        // The blocks come from the in-memory model, which can be newer than
        // the file on disk, so the metadata must stay invalidated.
        write.updateMeta = false;
        write.blocks.reserve(m_blockModel->rowCount());

        for (int row = 0; row < m_blockModel->rowCount(); ++row) {
          const QModelIndex index = m_blockModel->index(row);
          const QString blockId =
              m_blockModel->data(index, BlockModel::BlockIdRole).toString();
          const QString text =
              m_blockModel->data(index, BlockModel::ContentRole).toString();

          if (!blockId.isEmpty())
            write.blocks.append({blockId, text});
        }
      } else {
        write.type = WriteOp::Type::FileReplace;
      }
      break;
    }

    ops.append(std::move(write));
  }

  if (ops.isEmpty())
    return;

  const QString databasePath = m_databasePath;

  (void)QtConcurrent::run(&m_writePool, [databasePath, ops = std::move(ops)]() {
    executeWriteBatch(databasePath, ops);
  });
}

void SearchIndex::discardPending(const QString &pathPrefix) {
  m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(),
                                 [&pathPrefix](const PendingOp &op) {
                                   return op.path == pathPrefix ||
                                          op.path.startsWith(pathPrefix +
                                                             QLatin1Char('/'));
                                 }),
                  m_pending.end());
}

bool SearchIndex::replaceDocument(const QString &path, const QString &title,
                                  const QVector<BlockRecord> &blocks,
                                  bool updateMetaRow) {
  if (path.isEmpty() || !m_db.isOpen())
    return false;

  if (!m_db.transaction())
    return false;

  QSqlQuery remove(m_db);
  remove.prepare(QStringLiteral(
      "DELETE FROM search_index WHERE document_path = ?"));
  remove.addBindValue(path);

  if (!remove.exec()) {
    m_db.rollback();
    return false;
  }

  QSqlQuery insert(m_db);
  insert.prepare(QStringLiteral(
      "INSERT INTO search_index(document_path, document_title, block_id, "
      "block_text) VALUES(?, ?, ?, ?)"));

  for (const BlockRecord &block : blocks) {
    if (block.text.isEmpty())
      continue;

    insert.addBindValue(path);
    insert.addBindValue(title);
    insert.addBindValue(block.blockId);
    insert.addBindValue(block.text);

    if (!insert.exec()) {
      qWarning() << "SearchIndex: insert block failed:"
                 << insert.lastError().text();
      m_db.rollback();
      return false;
    }
  }

  if (updateMetaRow) {
    updateMeta(path, title);
  } else {
    // The blocks did not necessarily come from the file on disk, so make the
    // next startup reindex this document.
    if (!invalidateMeta(path)) {
      m_db.rollback();
      return false;
    }
  }

  if (!m_db.commit()) {
    m_db.rollback();
    return false;
  }

  return true;
}

bool SearchIndex::reindexDocumentFromFile(const QString &path) {
  m_writePool.waitForDone();

  const QString normalized = normalizedPath(path);

  if (normalized.isEmpty())
    return false;

  QFile file(normalized);

  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    removeDocument(normalized);
    return false;
  }

  QJsonParseError parseError{};
  const QJsonDocument parsed =
      QJsonDocument::fromJson(file.readAll(), &parseError);
  file.close();

  if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
    removeDocument(normalized);
    return false;
  }

  Document document;
  QString documentError;

  if (!document.fromJson(parsed.object(), &documentError)) {
    removeDocument(normalized);
    return false;
  }

  QString title =
      parsed.object().value(QStringLiteral("title")).toString().trimmed();

  if (title.isEmpty())
    title = QFileInfo(normalized).completeBaseName();

  QVector<BlockRecord> blocks;
  blocks.reserve(document.blocks().size());

  for (const Block &block : document.blocks())
    blocks.append({block.id, block.text});

  return replaceDocument(normalized, title, blocks, true);
}

bool SearchIndex::reindexCurrentDocument() {
  if (!m_blockModel)
    return false;

  const QString path = normalizedPath(m_blockModel->currentFilePath());

  if (path.isEmpty())
    return false;

  QVector<BlockRecord> blocks;
  blocks.reserve(m_blockModel->rowCount());

  for (int row = 0; row < m_blockModel->rowCount(); ++row) {
    const QModelIndex index = m_blockModel->index(row);

    const QString blockId =
        m_blockModel->data(index, BlockModel::BlockIdRole).toString();
    const QString text =
        m_blockModel->data(index, BlockModel::ContentRole).toString();

    if (!blockId.isEmpty())
      blocks.append({blockId, text});
  }

  return replaceDocument(path, m_blockModel->documentTitle(), blocks, false);
}

bool SearchIndex::upsertBlock(const QString &path, const QString &title,
                              const QString &blockId, const QString &text) {
  if (path.isEmpty() || blockId.isEmpty() || !m_db.isOpen())
    return false;

  if (!m_db.transaction())
    return false;

  QSqlQuery remove(m_db);
  remove.prepare(QStringLiteral(
      "DELETE FROM search_index WHERE document_path = ? AND block_id = ?"));
  remove.addBindValue(path);
  remove.addBindValue(blockId);

  if (!remove.exec()) {
    m_db.rollback();
    return false;
  }

  if (!text.isEmpty()) {
    QSqlQuery insert(m_db);
    insert.prepare(QStringLiteral(
        "INSERT INTO search_index(document_path, document_title, block_id, "
        "block_text) VALUES(?, ?, ?, ?)"));
    insert.addBindValue(path);
    insert.addBindValue(title);
    insert.addBindValue(blockId);
    insert.addBindValue(text);

    if (!insert.exec()) {
      m_db.rollback();
      return false;
    }
  }

  // Live edits are not necessarily on disk. Invalidate the file metadata so
  // the next startup reindexes this document from the saved file.
  invalidateMeta(path);

  if (!m_db.commit()) {
    m_db.rollback();
    return false;
  }

  return true;
}

bool SearchIndex::removeBlock(const QString &path, const QString &blockId) {
  if (path.isEmpty() || blockId.isEmpty() || !m_db.isOpen())
    return false;

  if (!m_db.transaction())
    return false;

  QSqlQuery remove(m_db);
  remove.prepare(QStringLiteral(
      "DELETE FROM search_index WHERE document_path = ? AND block_id = ?"));
  remove.addBindValue(path);
  remove.addBindValue(blockId);

  const bool ok = remove.exec();
  invalidateMeta(path);

  if (!ok || !m_db.commit()) {
    m_db.rollback();
    return false;
  }

  return true;
}

bool SearchIndex::removeDocument(const QString &path) {
  if (path.isEmpty() || !m_db.isOpen())
    return false;

  m_writePool.waitForDone();

  if (!m_db.transaction())
    return false;

  QSqlQuery removeIndex(m_db);
  removeIndex.prepare(QStringLiteral(
      "DELETE FROM search_index WHERE document_path = ?"));
  removeIndex.addBindValue(path);

  QSqlQuery removeMeta(m_db);
  removeMeta.prepare(
      QStringLiteral("DELETE FROM search_meta WHERE document_path = ?"));
  removeMeta.addBindValue(path);

  const bool ok = removeIndex.exec() && removeMeta.exec();

  if (!ok || !m_db.commit()) {
    m_db.rollback();
    return false;
  }

  return true;
}

bool SearchIndex::removePathPrefix(const QString &path) {
  if (path.isEmpty() || !m_db.isOpen())
    return false;

  m_writePool.waitForDone();

  const QString prefix = escapeLike(path) + QStringLiteral("/%");

  if (!m_db.transaction())
    return false;

  QSqlQuery removeIndex(m_db);
  removeIndex.prepare(QStringLiteral(
      "DELETE FROM search_index WHERE document_path = ? OR document_path "
      "LIKE ? ESCAPE '\\'"));
  removeIndex.addBindValue(path);
  removeIndex.addBindValue(prefix);

  QSqlQuery removeMeta(m_db);
  removeMeta.prepare(QStringLiteral(
      "DELETE FROM search_meta WHERE document_path = ? OR document_path "
      "LIKE ? ESCAPE '\\'"));
  removeMeta.addBindValue(path);
  removeMeta.addBindValue(prefix);

  const bool ok = removeIndex.exec() && removeMeta.exec();

  if (!ok || !m_db.commit()) {
    m_db.rollback();
    return false;
  }

  return true;
}

bool SearchIndex::renamePath(const QString &oldPath, const QString &newPath) {
  if (oldPath.isEmpty() || newPath.isEmpty() || oldPath == newPath ||
      !m_db.isOpen()) {
    return false;
  }

  m_writePool.waitForDone();

  const QString prefix = escapeLike(oldPath) + QStringLiteral("/%");
  const QString newTitle = QFileInfo(newPath).completeBaseName();
  const int suffixStart = oldPath.length() + 1;

  if (!m_db.transaction())
    return false;

  QSqlQuery updateIndex(m_db);
  updateIndex.prepare(QStringLiteral(
      "UPDATE search_index SET "
      "document_path = ? || substr(document_path, ?), "
      "document_title = CASE WHEN document_path = ? THEN ? "
      "ELSE document_title END "
      "WHERE document_path = ? OR document_path LIKE ? ESCAPE '\\'"));
  updateIndex.addBindValue(newPath);
  updateIndex.addBindValue(suffixStart);
  updateIndex.addBindValue(oldPath);
  updateIndex.addBindValue(newTitle);
  updateIndex.addBindValue(oldPath);
  updateIndex.addBindValue(prefix);

  QSqlQuery updateMeta(m_db);
  updateMeta.prepare(QStringLiteral(
      "UPDATE search_meta SET "
      "document_path = ? || substr(document_path, ?), "
      "document_title = CASE WHEN document_path = ? THEN ? "
      "ELSE document_title END "
      "WHERE document_path = ? OR document_path LIKE ? ESCAPE '\\'"));
  updateMeta.addBindValue(newPath);
  updateMeta.addBindValue(suffixStart);
  updateMeta.addBindValue(oldPath);
  updateMeta.addBindValue(newTitle);
  updateMeta.addBindValue(oldPath);
  updateMeta.addBindValue(prefix);

  const bool ok = updateIndex.exec() && updateMeta.exec();

  if (!ok || !m_db.commit()) {
    m_db.rollback();
    return false;
  }

  return true;
}

bool SearchIndex::invalidateMeta(const QString &path) {
  QSqlQuery query(m_db);
  query.prepare(
      QStringLiteral("DELETE FROM search_meta WHERE document_path = ?"));
  query.addBindValue(path);
  return query.exec();
}

bool SearchIndex::updateMeta(const QString &path, const QString &title) {
  const QFileInfo info(path);
  const qint64 modified =
      info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0;
  const qint64 size = info.exists() ? info.size() : 0;

  QSqlQuery remove(m_db);
  remove.prepare(
      QStringLiteral("DELETE FROM search_meta WHERE document_path = ?"));
  remove.addBindValue(path);

  if (!remove.exec())
    return false;

  QSqlQuery insert(m_db);
  insert.prepare(QStringLiteral(
      "INSERT INTO search_meta(document_path, document_title, modified_ms, "
      "size) VALUES(?, ?, ?, ?)"));
  insert.addBindValue(path);
  insert.addBindValue(title);
  insert.addBindValue(modified);
  insert.addBindValue(size);

  return insert.exec();
}

bool SearchIndex::readMeta(const QString &path, qint64 *modifiedMs,
                           qint64 *size) const {
  QSqlQuery query(m_db);
  query.prepare(QStringLiteral(
      "SELECT modified_ms, size FROM search_meta WHERE document_path = ?"));
  query.addBindValue(path);

  if (!query.exec() || !query.next())
    return false;

  if (modifiedMs)
    *modifiedMs = query.value(0).toLongLong();
  if (size)
    *size = query.value(1).toLongLong();

  return true;
}

void SearchIndex::syncIncremental() {
  if (!m_db.isOpen())
    return;

  m_writePool.waitForDone();

  const QString root = documentsRoot();

  if (root.isEmpty())
    return;

  const QString currentPath =
      (!m_currentDocumentPath.isEmpty() && m_blockModel)
          ? m_currentDocumentPath
          : QString();

  QVector<WriteBlock> currentBlocks;
  QString currentTitle;

  if (!currentPath.isEmpty()) {
    currentTitle = m_blockModel->documentTitle();
    currentBlocks.reserve(m_blockModel->rowCount());

    for (int row = 0; row < m_blockModel->rowCount(); ++row) {
      const QModelIndex index = m_blockModel->index(row);

      const QString blockId =
          m_blockModel->data(index, BlockModel::BlockIdRole).toString();
      const QString text =
          m_blockModel->data(index, BlockModel::ContentRole).toString();

      if (!blockId.isEmpty())
        currentBlocks.append({blockId, text});
    }
  }

  performIncrementalSync(m_db, root, currentPath, currentTitle, currentBlocks);
}

void SearchIndex::syncIncrementalAsync() {
  if (!m_db.isOpen() || m_databasePath.isEmpty())
    return;

  // Coalesce bursts of requests into a single queued scan.
  if (m_syncRunning) {
    m_syncQueued = true;
    return;
  }

  const QString root = documentsRoot();

  if (root.isEmpty())
    return;

  const QString currentPath =
      (!m_currentDocumentPath.isEmpty() && m_blockModel)
          ? m_currentDocumentPath
          : QString();

  QVector<WriteBlock> currentBlocks;
  QString currentTitle;

  if (!currentPath.isEmpty()) {
    currentTitle = m_blockModel->documentTitle();
    currentBlocks.reserve(m_blockModel->rowCount());

    for (int row = 0; row < m_blockModel->rowCount(); ++row) {
      const QModelIndex index = m_blockModel->index(row);

      const QString blockId =
          m_blockModel->data(index, BlockModel::BlockIdRole).toString();
      const QString text =
          m_blockModel->data(index, BlockModel::ContentRole).toString();

      if (!blockId.isEmpty())
        currentBlocks.append({blockId, text});
    }
  }

  m_syncRunning = true;

  auto *watcher = new QFutureWatcher<void>(this);

  connect(watcher, &QFutureWatcherBase::finished, this,
          [this, watcher]() {
            watcher->deleteLater();
            m_syncRunning = false;

            if (m_syncQueued) {
              m_syncQueued = false;
              syncIncrementalAsync();
            }

            emit indexUpdated();
          });

  const QString databasePath = m_databasePath;

  watcher->setFuture(QtConcurrent::run(
      &m_writePool, [databasePath, root, currentPath, currentTitle,
                     currentBlocks]() {
        executeIncrementalSync(databasePath, root, currentPath, currentTitle,
                               currentBlocks);
      }));
}

void SearchIndex::rebuild() {
  if (!m_db.isOpen())
    return;

  m_writePool.waitForDone();

  QSqlQuery query(m_db);
  query.exec(QStringLiteral("DELETE FROM search_index"));
  query.exec(QStringLiteral("DELETE FROM search_meta"));

  syncIncremental();
}

QVector<SearchIndex::Result> SearchIndex::search(const QString &query,
                                                bool *truncated) const {
  QVector<Result> results;

  if (truncated)
    *truncated = false;

  const QStringList terms = queryTerms(query);

  if (terms.isEmpty() || !m_db.isOpen())
    return results;

  const QString ftsQuery = buildFtsQuery(terms);

  QSqlQuery sql(m_db);
  sql.prepare(QStringLiteral(
      "SELECT document_path, document_title, block_id, block_text, "
      "bm25(search_index) "
      "FROM search_index WHERE search_index MATCH ? "
      "ORDER BY bm25(search_index) LIMIT ?"));
  sql.addBindValue(ftsQuery);
  // Fetch one extra row to detect whether the result set was capped.
  sql.addBindValue(kMaxResults + 1);

  if (!sql.exec()) {
    qWarning() << "SearchIndex: query failed:" << sql.lastError().text();
    return results;
  }

  while (sql.next()) {
    Result result;
    result.documentPath = sql.value(0).toString();
    result.documentTitle = sql.value(1).toString();
    result.blockId = sql.value(2).toString();
    result.blockText = sql.value(3).toString();
    result.rank = sql.value(4).toDouble();

    result.ranges = matchRanges(result.blockText, terms);

    if (result.ranges.isEmpty())
      continue;

    result.matchCount = result.ranges.size();
    result.snippet =
        makeSnippet(result.blockText, result.ranges, &result.snippetRanges);

    results.append(result);
  }

  if (results.size() > kMaxResults) {
    results.resize(kMaxResults);

    if (truncated)
      *truncated = true;
  }

  QHash<QString, double> bestRank;
  QHash<QString, int> documentCounts;

  for (const Result &result : std::as_const(results)) {
    const double rank = bestRank.value(result.documentPath, result.rank);

    bestRank.insert(result.documentPath, qMin(rank, result.rank));
    documentCounts[result.documentPath] += result.matchCount;
  }

  for (Result &result : results)
    result.documentMatchCount = documentCounts.value(result.documentPath);

  // Group results by document while keeping the best-ranked document first
  // and the best-ranked blocks first inside each group.
  std::stable_sort(results.begin(), results.end(),
                   [&bestRank](const Result &left, const Result &right) {
                     if (left.documentPath == right.documentPath)
                       return left.rank < right.rank;

                     const double leftRank =
                         bestRank.value(left.documentPath, left.rank);
                     const double rightRank =
                         bestRank.value(right.documentPath, right.rank);

                     if (!qFuzzyCompare(leftRank + 1.0, rightRank + 1.0))
                       return leftRank < rightRank;

                     return left.documentPath.localeAwareCompare(
                                right.documentPath) < 0;
                   });

  return results;
}

QString SearchIndex::documentsRoot() const { return m_documentsRoot; }

QString SearchIndex::connectionNameFor(const SearchIndex *index) {
  return QStringLiteral("templnote_search_%1")
      .arg(reinterpret_cast<quintptr>(index), 0, 16);
}

QString SearchIndex::normalizedPath(const QString &path) {
  if (path.isEmpty())
    return {};

  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QStringList SearchIndex::queryTerms(const QString &query) {
  const QString simplified = query.simplified();

  if (simplified.isEmpty())
    return {};

  return simplified.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

QString SearchIndex::buildFtsQuery(const QStringList &terms) {
  QStringList quoted;
  quoted.reserve(terms.size());

  for (QString term : terms) {
    term.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    // Search-as-you-type: each term matches tokens in the block text that
    // start with it, so a partially typed word ("edit") already finds
    // "editing". Limiting the match to block_text keeps every returned row
    // highlightable; document titles are metadata for grouping, not a second
    // match source that could mix terms across fields.
    quoted.append(QStringLiteral("block_text:\"") + term +
                  QStringLiteral("\"*"));
  }

  return quoted.join(QStringLiteral(" AND "));
}

QVector<SearchIndex::MatchRange>
SearchIndex::matchRanges(const QString &text, const QStringList &terms) {
  QVector<MatchRange> ranges;

  if (text.isEmpty())
    return ranges;

  const QString haystack = text.toLower();

  for (const QString &term : terms) {
    // The FTS query tokenizes terms and matches token prefixes. Split the
    // term the same way so punctuation-containing queries still highlight the
    // words that actually caused the match.
    QStringList needles;
    QString token;

    for (const QChar character : term) {
      if (isTokenCharacter(character)) {
        token.append(character.toLower());
      } else if (!token.isEmpty()) {
        needles.append(token);
        token.clear();
      }
    }

    if (!token.isEmpty())
      needles.append(token);

    for (const QString &needle : needles) {
      int from = 0;

      while (from <= haystack.length() - needle.length()) {
        const int start = haystack.indexOf(needle, from);

        if (start < 0)
          break;

        // Only a match at the beginning of a token is a real FTS prefix
        // match; "edit" must not highlight the middle of "credit".
        if (start == 0 || !isTokenCharacter(haystack.at(start - 1))) {
          ranges.append({start, static_cast<int>(needle.length())});
          from = start + needle.length();
        } else {
          from = start + 1;
        }
      }
    }
  }

  std::sort(ranges.begin(), ranges.end(),
            [](const MatchRange &left, const MatchRange &right) {
              if (left.start != right.start)
                return left.start < right.start;
              return left.length > right.length;
            });

  QVector<MatchRange> merged;
  merged.reserve(ranges.size());

  for (const MatchRange &range : std::as_const(ranges)) {
    if (!merged.isEmpty() &&
        range.start <= merged.last().start + merged.last().length) {
      const int end = qMax(merged.last().start + merged.last().length,
                           range.start + range.length);
      merged.last().length = end - merged.last().start;
      continue;
    }

    merged.append(range);
  }

  return merged;
}

QString SearchIndex::makeSnippet(const QString &text,
                                 const QVector<MatchRange> &ranges,
                                 QVector<MatchRange> *snippetRanges) {
  if (snippetRanges)
    snippetRanges->clear();

  if (ranges.isEmpty())
    return text;

  constexpr int kContextBefore = 48;
  constexpr int kSnippetLength = 160;

  const MatchRange &first = ranges.first();
  const int contextStart = qMax(0, first.start - kContextBefore);
  const int contextEnd =
      qMin(text.length(), qMax(first.start + first.length,
                               contextStart + kSnippetLength));

  const QString prefix = contextStart > 0 ? QStringLiteral("…") : QString();
  const QString suffix =
      contextEnd < text.length() ? QStringLiteral("…") : QString();

  const QString snippet =
      prefix + text.mid(contextStart, contextEnd - contextStart) + suffix;

  if (snippetRanges) {
    const int offset = prefix.length() - contextStart;

    for (const MatchRange &range : ranges) {
      const int start = qMax(range.start, contextStart);
      const int end = qMin(range.start + range.length, contextEnd);

      if (start >= end)
        continue;

      snippetRanges->append({start + offset, end - start});
    }
  }

  return snippet;
}
} // namespace Templnote
