#include "SearchIndex.h"

#include "core/Document.h"
#include "../model/BlockModel.h"
#include "storage/AppPaths.h"
#include "../storage/DocumentListModel.h"

#include <QCoreApplication>
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
#include <QStringList>
#include <QThreadStorage>
#include <QVariantMap>
#include <QtConcurrent>
#include <algorithm>
#include <atomic>
#include <iterator>
#include <memory>
#include <utility>

namespace Templnote {
namespace {
constexpr int kFlushDelayMs = 350;
constexpr int kMaxResults = 200;
// An async cache hit may lag the index by at most this long after a write.
// Synchronous search() is never served from a stale generation.
constexpr qint64 kCacheGraceMs = 2000;
constexpr int kMaxHighlightRanges = 64;

// Bumped when the database file itself is deleted and recreated (corrupt-file
// recovery). Pooled connections opened against the old file are then dropped
// on their next use; a normal rebuild only deletes rows and needs no bump.
std::atomic<quint64> &databaseEpoch() {
  static std::atomic<quint64> epoch{0};
  return epoch;
}

// SQLite connections must be created and used on the same thread. Both the
// single-threaded writer pool and the (multi-threaded) concurrent reader pool
// therefore keep their connections in thread-local storage: the first call on
// a thread creates one, every subsequent batch/search on that thread reuses it.
// Connection names are globally unique so they never collide with the GUI
// thread's own SearchIndex connection or with another index's pools.
class ThreadSqlConnections {
public:
  ~ThreadSqlConnections() {
    for (Connection &connection : m_connections) {
      if (connection.database.isOpen())
        connection.database.close();
    }
    m_connections.clear();

    // At process teardown thread-local destructors can run after Qt's SQL
    // drivers are gone. Leaving a uniquely named registration behind then is
    // harmless; removing it at that point is not.
    if (QCoreApplication::instance()) {
      for (const QString &name : std::as_const(m_names))
        QSqlDatabase::removeDatabase(name);
    }
  }

  QSqlDatabase connection(const QString &databasePath, bool readOnly,
                          quint64 epoch) {
    const QString key = QStringLiteral("%1|%2|%3")
                            .arg(readOnly ? 1 : 0)
                            .arg(databasePath)
                            .arg(QString::number(epoch));

    const auto found = m_connections.constFind(key);
    if (found != m_connections.constEnd() && found->epoch == epoch &&
        found->database.isOpen()) {
      return found->database;
    }

    // A recovered database has a new inode. Dispose of connections that were
    // opened for an older file before creating one for the current epoch.
    for (auto it = m_connections.begin(); it != m_connections.end();) {
      if (it->epoch == epoch) {
        ++it;
        continue;
      }

      if (it->database.isOpen())
        it->database.close();

      const QString staleName = it->name;
      it = m_connections.erase(it);
      m_names.removeAll(staleName);
      if (QCoreApplication::instance())
        QSqlDatabase::removeDatabase(staleName);
    }

    static std::atomic<quint64> counter{0};
    const QString name =
        QStringLiteral("templnote_search_%1_%2")
            .arg(readOnly ? QStringLiteral("reader") : QStringLiteral("writer"))
            .arg(QString::number(counter.fetch_add(1, std::memory_order_relaxed)));

    QSqlDatabase db =
        QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(databasePath);
    if (readOnly)
      db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));

    if (!db.open()) {
      qWarning() << "SearchIndex: could not open pooled connection" << name;
      db = QSqlDatabase();
      if (QCoreApplication::instance())
        QSqlDatabase::removeDatabase(name);
      return {};
    }

    // Set once per connection rather than on every search/write batch.
    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA busy_timeout=5000"));

    m_connections.insert(key, Connection{db, name, epoch});
    m_names.append(name);
    return db;
  }

private:
  struct Connection {
    QSqlDatabase database;
    QString name;
    quint64 epoch = 0;
  };

  QHash<QString, Connection> m_connections;
  QStringList m_names;
};

QThreadStorage<ThreadSqlConnections> &threadSqlConnections() {
  static QThreadStorage<ThreadSqlConnections> storage;
  return storage;
}

QString escapeLike(QString value) {
  value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
  value.replace(QLatin1Char('%'), QStringLiteral("\\%"));
  value.replace(QLatin1Char('_'), QStringLiteral("\\_"));
  return value;
}

// FTS5's unicode61 tokenizer removes diacritics ("cafe" matches "café"), but
// QString::indexOf() on the original text does not. Fold both sides while
// keeping a per-code-unit map back to the source so highlight offsets remain
// correct even when a character expands or shrinks during normalization.
QString foldForMatching(const QString &input, QVector<int> *offsetMap = nullptr) {
  QString folded;
  folded.reserve(input.size());

  if (offsetMap) {
    offsetMap->clear();
    offsetMap->reserve(input.size());
  }

  // Normalising every code unit with QString::normalized(KD) allocates a
  // temporary QString per character, and matchRanges() calls this for every
  // result row. Search text is overwhelmingly ASCII, so detect that case once
  // and copy the units straight through. The offset map stays one-to-one,
  // which is what highlight ranges rely on.
  bool allAscii = true;
  for (int i = 0; i < input.size(); ++i) {
    if (input.at(i).unicode() > 0x7F) {
      allAscii = false;
      break;
    }
  }

  if (allAscii) {
    for (int i = 0; i < input.size(); ++i) {
      folded.append(input.at(i));

      if (offsetMap)
        offsetMap->append(i);
    }

    return folded;
  }

  // Non-ASCII fallback. Combining marks must be dropped regardless of their
  // decomposition tag: NFD input already contains standalone marks such as
  // U+0301/U+0308, which have NoDecomposition and would otherwise survive the
  // fast path below. FTS5 folds them away, so keeping them here makes the
  // folded text unmatchable and drops an otherwise valid result. A unit with no
  // decomposition (and not an algorithmically decomposable Hangul syllable)
  // normalises to itself and cannot produce a mark, so append it directly
  // instead of paying for a temporary QString.
  for (int i = 0; i < input.size(); ++i) {
    const QChar character = input.at(i);
    const QChar::Category category = character.category();

    if (category == QChar::Mark_NonSpacing ||
        category == QChar::Mark_SpacingCombining ||
        category == QChar::Mark_Enclosing) {
      continue;
    }

    const auto code = character.unicode();
    const bool hangulSyllable = code >= 0xAC00 && code <= 0xD7A3;

    if (!hangulSyllable &&
        character.decompositionTag() == QChar::NoDecomposition) {
      folded.append(character);

      if (offsetMap)
        offsetMap->append(i);

      continue;
    }

    const QString decomposed =
        QString(character).normalized(QString::NormalizationForm_KD);

    for (const QChar &part : decomposed) {
      const QChar::Category partCategory = part.category();

      if (partCategory == QChar::Mark_NonSpacing ||
          partCategory == QChar::Mark_SpacingCombining ||
          partCategory == QChar::Mark_Enclosing) {
        continue;
      }

      folded.append(part);

      if (offsetMap)
        offsetMap->append(i);
    }
  }

  return folded;
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

// Returns true when the index was built from the current bytes on disk. This
// makes reopening an unchanged document free on the writer pool instead of
// deleting and reinserting every FTS row.
bool row_indexIsCurrent(QSqlDatabase &db, const QString &path) {
  const QFileInfo info(path);
  if (!info.exists())
    return false;

  QSqlQuery meta(db);
  meta.prepare(QStringLiteral(
      "SELECT modified_ms, size FROM search_meta WHERE document_path = ?"));
  meta.addBindValue(path);
  if (!meta.exec() || !meta.next())
    return false;

  if (meta.value(0).toLongLong() != info.lastModified().toMSecsSinceEpoch() ||
      meta.value(1).toLongLong() != info.size())
    return false;

  QSqlQuery rows(db);
  rows.prepare(
      QStringLiteral("SELECT 1 FROM search_rows WHERE document_path = ? LIMIT 1"));
  rows.addBindValue(path);
  return rows.exec() && rows.next();
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
  // Delete through the rowid map and update it when the new row is inserted,
  // otherwise later document/block removals would miss this row.
  if (!row_deleteBlockRow(db, path, blockId))
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

    QSqlQuery insertRow(db);
    insertRow.prepare(QStringLiteral(
        "INSERT INTO search_rows(document_path, block_id, rowid) "
        "VALUES(?, ?, ?)"));
    insertRow.addBindValue(path);
    insertRow.addBindValue(blockId);
    insertRow.addBindValue(insert.lastInsertId().toLongLong());

    if (!insertRow.exec())
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
  if (!row_deleteBlockRow(db, path, blockId))
    return false;

  QSqlQuery invalidate(db);
  invalidate.prepare(
      QStringLiteral("DELETE FROM search_meta WHERE document_path = ?"));
  invalidate.addBindValue(path);

  return invalidate.exec();
}

bool parseDocumentFile(const QString &normalized, QString *title,
                       QVector<WriteBlock> *blocks) {
  QFile file(normalized);

  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return false;

  QJsonParseError parseError{};
  const QJsonDocument parsed =
      QJsonDocument::fromJson(file.readAll(), &parseError);
  file.close();

  if (parseError.error != QJsonParseError::NoError || !parsed.isObject())
    return false;

  Document document;
  QString documentError;

  if (!document.fromJson(parsed.object(), &documentError))
    return false;

  QString parsedTitle =
      parsed.object().value(QStringLiteral("title")).toString().trimmed();

  if (parsedTitle.isEmpty())
    parsedTitle = QFileInfo(normalized).completeBaseName();

  blocks->clear();
  blocks->reserve(document.blocks().size());

  for (const Block &block : document.blocks())
    blocks->append({block.id, block.text});

  if (title)
    *title = parsedTitle;

  return true;
}

bool replaceFromFile(QSqlDatabase &db, const QString &path) {
  const QString normalized = normalizePath(path);

  if (!QFileInfo::exists(normalized)) {
    row_removeDocument(db, normalized);
    return false;
  }

  if (row_indexIsCurrent(db, normalized))
    return true;

  QString title;
  QVector<WriteBlock> blocks;

  if (!parseDocumentFile(normalized, &title, &blocks)) {
    row_removeDocument(db, normalized);
    return false;
  }

  return row_replaceDocument(db, normalized, title, blocks, true);
}

void performIncrementalSync(QSqlDatabase &db, const QString &root,
                            const QString &currentDocumentPath,
                            const QString &currentDocumentTitle,
                            const QVector<WriteBlock> &currentBlocks) {
  if (!db.isOpen() || root.isEmpty())
    return;

  struct Update {
    QString path;
    QString title;
    QVector<WriteBlock> blocks;
    bool updateMeta = true;
  };

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

  // Parse every changed file before opening a transaction. File IO and JSON
  // parsing used to happen inside one scan-wide transaction, which held the
  // SQLite write lock for the whole tree.
  QVector<Update> updates;
  QSet<QString> removals;
  QSet<QString> found;

  QDirIterator it(root, QDir::Files | QDir::Readable,
                  QDirIterator::Subdirectories);

  while (it.hasNext()) {
    const QFileInfo info(it.next());

    if (info.suffix().compare(QStringLiteral("templnote"),
                              Qt::CaseInsensitive) != 0) {
      continue;
    }

    const QString path = normalizePath(info.absoluteFilePath());
    found.insert(path);

    if (!currentDocumentPath.isEmpty() && path == currentDocumentPath) {
      updates.append(
          {path, currentDocumentTitle, currentBlocks, false});
      continue;
    }

    const auto meta = metadata.constFind(path);

    if (meta != metadata.constEnd() &&
        meta->first == info.lastModified().toMSecsSinceEpoch() &&
        meta->second == info.size()) {
      continue;
    }

    Update update;
    update.path = path;

    if (parseDocumentFile(path, &update.title, &update.blocks))
      updates.append(std::move(update));
    else
      removals.insert(path);
  }

  // Remove index entries and metadata for files that disappeared.
  QSet<QString> knownPaths;
  {
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

  // The document currently open can legitimately live outside documentsRoot
  // (e.g. after Save As). It was never visited by the directory scan above,
  // so add it explicitly before the removal pass; otherwise the live index
  // entry for the open document is wiped on every incremental sync.
  if (!currentDocumentPath.isEmpty() && !found.contains(currentDocumentPath)) {
    found.insert(currentDocumentPath);
    updates.append(
        {currentDocumentPath, currentDocumentTitle, currentBlocks, false});
  }

  // Other out-of-root documents (previously opened via Save As) are likewise
  // invisible to the directory scan. Keep any that still exist on disk and
  // refresh them when their recorded metadata is stale; only remove entries
  // whose file has actually disappeared.
  for (const QString &path : std::as_const(knownPaths)) {
    if (found.contains(path))
      continue;

    // The open document was handled above from the live in-memory blocks.
    if (!currentDocumentPath.isEmpty() && path == currentDocumentPath)
      continue;

    QFileInfo info(path);
    if (!info.exists()) {
      removals.insert(path);
      continue;
    }

    found.insert(path);
    const auto meta = metadata.constFind(path);
    const bool stale =
        meta == metadata.constEnd() ||
        meta->first != info.lastModified().toMSecsSinceEpoch() ||
        meta->second != info.size();

    if (stale) {
      Update update;
      update.path = path;
      if (parseDocumentFile(path, &update.title, &update.blocks))
        updates.append(std::move(update));
      // An unreadable file keeps its existing index entry until it can be
      // parsed again, rather than being treated as deleted.
    }
  }

  // Commit one document at a time. A single unreadable or malformed file can
  // no longer roll back an otherwise good scan, and live writes only ever wait
  // for a short transaction instead of the whole tree.
  for (const Update &update : std::as_const(updates)) {
    if (!db.transaction()) {
      qWarning() << "SearchIndex: sync transaction failed:"
                 << db.lastError().text();
      continue;
    }

    const bool ok = row_replaceDocument(db, update.path, update.title,
                                        update.blocks, update.updateMeta);

    if (!ok || !db.commit()) {
      db.rollback();
      qWarning() << "SearchIndex: sync update failed for" << update.path;
    }
  }

  for (const QString &path : std::as_const(removals)) {
    if (!db.transaction()) {
      qWarning() << "SearchIndex: sync transaction failed:"
                 << db.lastError().text();
      continue;
    }

    const bool ok = row_removeDocument(db, path);

    if (!ok || !db.commit()) {
      db.rollback();
      qWarning() << "SearchIndex: sync removal failed for" << path;
    }
  }
}

void executeClearIndex(const QString &databasePath) {
  QSqlDatabase db = threadSqlConnections().localData().connection(
      databasePath, /*readOnly=*/false,
      databaseEpoch().load(std::memory_order_relaxed));

  if (!db.isOpen())
    return;

  QSqlQuery query(db);
  query.exec(QStringLiteral("DELETE FROM search_index"));
  query.exec(QStringLiteral("DELETE FROM search_rows"));
  query.exec(QStringLiteral("DELETE FROM search_meta"));
}

void executeIncrementalSync(const QString &databasePath, const QString &root,
                            const QString &currentDocumentPath,
                            const QString &currentDocumentTitle,
                            const QVector<WriteBlock> &currentBlocks) {
  QSqlDatabase db = threadSqlConnections().localData().connection(
      databasePath, /*readOnly=*/false,
      databaseEpoch().load(std::memory_order_relaxed));

  if (db.isOpen()) {
    performIncrementalSync(db, root, currentDocumentPath, currentDocumentTitle,
                           currentBlocks);
  }
}

bool executeWriteBatch(const QString &databasePath, const QVector<WriteOp> &ops) {
  QSqlDatabase db = threadSqlConnections().localData().connection(
      databasePath, /*readOnly=*/false,
      databaseEpoch().load(std::memory_order_relaxed));

  bool ok = false;

  if (db.isOpen() && db.transaction()) {
    bool batchOk = true;

    for (const WriteOp &op : ops) {
      switch (op.type) {
      case WriteOp::Type::Upsert:
        batchOk = row_upsertBlock(db, op.path, op.title, op.blockId, op.text);
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

  if (!ok)
    qWarning() << "SearchIndex: background write batch failed";

  return ok;
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
  qRegisterMetaType<SearchOutcome>("Templnote::SearchIndex::SearchOutcome");
  m_cacheClock.start();

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

  // Indexing the document tree must not block the UI thread during startup.
  // The queued call also lets QML finish wiring the models before the scan
  // reads the current document.
  QTimer::singleShot(0, this, &SearchIndex::syncIncrementalAsync);
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

  if (!m_currentDocumentPath.isEmpty()) {
    queueFullDocument(m_currentDocumentPath);
    // A document already loaded when the model is attached has no subsequent
    // dataChanged signal to trigger the flush, so schedule it here.
    scheduleFlush();
  }

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
  pragma.exec(QStringLiteral("PRAGMA busy_timeout=5000"));

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

  // The rowid map lets deletes and renames target FTS5 rows directly instead
  // of scanning the whole index by unindexed document_path/block_id columns.
  if (!query.exec(QStringLiteral(
          "CREATE TABLE IF NOT EXISTS search_rows("
          "rowid INTEGER PRIMARY KEY,"
          "document_path TEXT NOT NULL,"
          "block_id TEXT NOT NULL)"))) {
    qWarning() << "SearchIndex: create search_rows failed:"
               << query.lastError().text();
    return false;
  }

  if (!query.exec(QStringLiteral(
          "CREATE INDEX IF NOT EXISTS search_rows_document_path "
          "ON search_rows(document_path)")) ||
      !query.exec(QStringLiteral(
          "CREATE INDEX IF NOT EXISTS search_rows_block "
          "ON search_rows(document_path, block_id)"))) {
    qWarning() << "SearchIndex: create search_rows indexes failed:"
               << query.lastError().text();
    return false;
  }

  // Databases written before search_rows existed already contain FTS rows.
  // Rebuild the map from those rows once so deletes and renames find them.
  QSqlQuery hasIndex(m_db);
  if (hasIndex.exec(QStringLiteral("SELECT 1 FROM search_index LIMIT 1")) &&
      hasIndex.next()) {
    QSqlQuery hasRows(m_db);
    if (hasRows.exec(QStringLiteral("SELECT 1 FROM search_rows LIMIT 1")) &&
        !hasRows.next()) {
      QSqlQuery rebuild(m_db);
      if (!rebuild.exec(QStringLiteral(
              "INSERT INTO search_rows(document_path, block_id, rowid) "
              "SELECT document_path, block_id, rowid FROM search_index"))) {
        // An unmappable legacy index is worse than no index: drop it so the
        // startup sync rebuilds everything from disk.
        query.exec(QStringLiteral("DELETE FROM search_index"));
        query.exec(QStringLiteral("DELETE FROM search_meta"));
      }
    }
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

  // Pooled reader/writer connections still point at the deleted file. Their
  // next use recreates them against the new one.
  databaseEpoch().fetch_add(1, std::memory_order_relaxed);

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

  invalidateSearchCache();
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

  invalidateSearchCache();
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

  invalidateSearchCache();
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

    watchWriteFuture(QtConcurrent::run(
        &m_writePool, [databasePath, write = std::move(write)]() {
          executeWriteBatch(databasePath, {write});
        }));
  }

  m_currentDocumentPath = newPath;

  if (!newPath.isEmpty())
    queueFullDocument(newPath);

  invalidateSearchCache();
  scheduleFlush();
}

void SearchIndex::onDocumentLoaded() {
  if (!m_blockModel)
    return;

  const QString path = normalizedPath(m_blockModel->currentFilePath());

  if (!path.isEmpty()) {
    queueFullDocument(path);
    invalidateSearchCache();
    scheduleFlush();
  }
}

void SearchIndex::onDocumentCreated() {
  if (!m_blockModel)
    return;

  const QString path = normalizedPath(m_blockModel->currentFilePath());

  if (!path.isEmpty()) {
    queueFullDocument(path);
    invalidateSearchCache();
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

  watchWriteFuture(QtConcurrent::run(&m_writePool, [databasePath, op]() {
    executeWriteBatch(databasePath, {op});
  }));
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

  watchWriteFuture(QtConcurrent::run(&m_writePool, [databasePath, op]() {
    executeWriteBatch(databasePath, {op});
  }));

  // Deleting a folder moves its children to the root, so the filesystem may
  // contain documents at paths that were not individually reported. Do this
  // in the background; a briefly stale result is acceptable.
  syncIncrementalAsync();
}

QString SearchIndex::pendingKey(const QString &path, const QString &blockId) {
  // U+001F cannot occur in a path or a block id, so path-only (full document)
  // and path+block keys cannot collide.
  return path + QChar(0x1F) + blockId;
}

void SearchIndex::pendingRebuildIndex() {
  m_pendingIndex.clear();
  m_pendingIndex.reserve(m_pending.size());

  for (int i = 0; i < m_pending.size(); ++i) {
    const PendingOp &op = m_pending.at(i);
    const QString blockId =
        op.type == PendingOp::Type::FullDocument ? QString() : op.blockId;
    m_pendingIndex.insert(pendingKey(op.path, blockId), i);
  }
}

void SearchIndex::pendingAppend(const PendingOp &op) {
  const QString blockId =
      op.type == PendingOp::Type::FullDocument ? QString() : op.blockId;
  m_pendingIndex.insert(pendingKey(op.path, blockId), m_pending.size());
  m_pending.append(op);
}

void SearchIndex::pendingInsert(int index, const PendingOp &op) {
  // Inserting shifts every later index; only used for the rare retry of a
  // full reindex that must run ahead of newer block edits. Rebuilding the
  // small hash is simpler and cheaper than patching every stale index.
  m_pending.insert(index, op);
  pendingRebuildIndex();
}

void SearchIndex::pendingCompact(const QVector<int> &removeIndices) {
  if (removeIndices.isEmpty())
    return;

  QVector<PendingOp> compacted;
  compacted.reserve(m_pending.size() - removeIndices.size());

  int next = 0;
  for (int i = 0; i < m_pending.size(); ++i) {
    if (next < removeIndices.size() && removeIndices.at(next) == i) {
      ++next;
      continue;
    }
    compacted.append(m_pending.at(i));
  }

  m_pending = std::move(compacted);
  pendingRebuildIndex();
}

void SearchIndex::pendingClear() {
  m_pending.clear();
  m_pendingIndex.clear();
}

void SearchIndex::queueBlockUpsert(const QString &path, const QString &title,
                                   const QString &blockId,
                                   const QString &text) {
  // Collapse repeated edits of the same block before the flush fires. This is
  // what keeps typing from enqueueing one SQL operation per keystroke.
  if (m_pendingIndex.contains(pendingKey(path, QString())))
    return;

  if (const auto it = m_pendingIndex.constFind(pendingKey(path, blockId));
      it != m_pendingIndex.cend()) {
    PendingOp &op = m_pending[*it];
    op.type = PendingOp::Type::Upsert;
    op.title = title;
    op.text = text;
    return;
  }

  PendingOp op;
  op.type = PendingOp::Type::Upsert;
  op.path = path;
  op.title = title;
  op.blockId = blockId;
  op.text = text;
  pendingAppend(op);
}

void SearchIndex::queueBlockRemove(const QString &path,
                                   const QString &blockId) {
  if (m_pendingIndex.contains(pendingKey(path, QString())))
    return;

  if (const auto it = m_pendingIndex.constFind(pendingKey(path, blockId));
      it != m_pendingIndex.cend()) {
    PendingOp &op = m_pending[*it];
    op.type = PendingOp::Type::Remove;
    op.text.clear();
    return;
  }

  PendingOp op;
  op.type = PendingOp::Type::Remove;
  op.path = path;
  op.blockId = blockId;
  pendingAppend(op);
}

void SearchIndex::queueFullDocument(const QString &path) {
  if (m_pendingIndex.contains(pendingKey(path, QString())))
    return;

  // A full reindex supersedes any queued block operations for this document.
  QVector<int> remove;
  for (int i = 0; i < m_pending.size(); ++i) {
    if (m_pending.at(i).path == path)
      remove.append(i);
  }
  pendingCompact(remove);

  PendingOp op;
  op.type = PendingOp::Type::FullDocument;
  op.path = path;
  op.title = QFileInfo(path).completeBaseName();
  pendingAppend(op);
}

void SearchIndex::scheduleFlush() {
  if (!m_db.isOpen())
    return;

  if (m_flushRetryCount > 0) {
    // A previous batch failed and is waiting on its backoff timer. Don't
    // restart it: a steady stream of edits would otherwise keep cancelling
    // the backoff and hammer the failing database. The timer's interval is
    // the backoff delay in this state (set in flushPending's callback).
    if (!m_flushTimer.isActive())
      m_flushTimer.start();
    return;
  }

  m_flushTimer.start();
}

void SearchIndex::watchWriteFuture(QFuture<void> future) {
  auto *watcher = new QFutureWatcher<void>(this);

  connect(watcher, &QFutureWatcherBase::finished, this,
          [this, watcher]() {
            watcher->deleteLater();
            invalidateSearchCache();
            emit indexUpdated();
          });

  watcher->setFuture(future);
}

void SearchIndex::flushPending() {
  if (m_pending.isEmpty() || !m_db.isOpen())
    return;

  // Share the snapshot with the retry callback instead of copying the whole
  // batch a second time. It is immutable from here on.
  const auto pending = std::make_shared<QVector<PendingOp>>(m_pending);
  pendingClear();

  // Snapshot everything the worker needs. A full reindex of the open document
  // must read the model on the GUI thread; file-backed reindexes can read the
  // file on the worker.
  QVector<WriteOp> ops;
  ops.reserve(pending->size());

  for (const PendingOp &op : *pending) {
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
      // A saved document is indexed from its file on the writer pool. Only an
      // unsaved in-memory document needs the GUI-thread snapshot path.
      if (op.path == m_currentDocumentPath && m_blockModel &&
          m_blockModel->isModified()) {
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

  // Keep the original pending ops so a failed transaction can be retried
  // instead of silently dropping index updates.
  auto *watcher = new QFutureWatcher<bool>(this);

  connect(watcher, &QFutureWatcherBase::finished, this,
          [this, watcher, pending]() {
            const bool ok = watcher->result();
            watcher->deleteLater();

            if (ok) {
              m_flushRetryCount = 0;
              m_flushTimer.setInterval(kFlushDelayMs);
            } else {
              requeuePending(*pending);
              ++m_flushRetryCount;

              const int shift = qMin(m_flushRetryCount, 6);
              // Persist the backoff on the timer itself so scheduleFlush()
              // can't silently restore the short debounce interval while a
              // failure is still pending.
              m_flushTimer.setInterval(
                  qMin(30000, kFlushDelayMs * (1 << shift)));
              m_flushTimer.start();
            }

            invalidateSearchCache();
            emit indexUpdated();
          });

  watcher->setFuture(QtConcurrent::run(
      &m_writePool, [databasePath, ops = std::move(ops)]() {
        return executeWriteBatch(databasePath, ops);
      }));
}

void SearchIndex::requeuePending(const QVector<PendingOp> &ops) {
  for (const PendingOp &op : ops) {
    if (op.path.isEmpty())
      continue;

    // Newer operations queued while the batch was in flight win, but a
    // failed full reindex must not be dropped just because block edits
    // arrived in the meantime: those edits are retried separately, while
    // skipping the full pass would leave the document partially indexed.
    // Whole-document removals are handled by discardPending() and never
    // appear here, so only another full reindex supersedes this op. The hash
    // answers both "is there a full reindex for this path" and "is this block
    // already queued" in constant time.
    const bool fullQueued =
        m_pendingIndex.contains(pendingKey(op.path, QString()));

    if (op.type == PendingOp::Type::FullDocument) {
      if (fullQueued)
        continue;
    } else if (fullQueued ||
               m_pendingIndex.contains(pendingKey(op.path, op.blockId))) {
      continue;
    }

    if (op.type == PendingOp::Type::FullDocument) {
      // A retried full reindex must run before block edits that arrived while
      // the failed batch was in flight. Appending it would let a file-backed
      // FileReplace rebuild from stale disk content and wipe those newer
      // edits, so insert ahead of the first queued op for the same document.
      const auto insertAt = std::find_if(m_pending.begin(), m_pending.end(),
                                         [&op](const PendingOp &current) {
                                           return current.path == op.path;
                                         });
      pendingInsert(static_cast<int>(std::distance(m_pending.begin(), insertAt)),
                    op);
    } else {
      pendingAppend(op);
    }
  }
}

void SearchIndex::discardPending(const QString &pathPrefix) {
  const QString childPrefix = pathPrefix + QLatin1Char('/');

  QVector<int> remove;
  for (int i = 0; i < m_pending.size(); ++i) {
    const QString &path = m_pending.at(i).path;
    if (path == pathPrefix || path.startsWith(childPrefix))
      remove.append(i);
  }
  pendingCompact(remove);
}

void SearchIndex::syncIncremental() {
  if (!m_db.isOpen() || m_databasePath.isEmpty())
    return;

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

  // The synchronous entry point is used by tests. Run the scan on the writer
  // pool so it is ordered with live edits, and wait only for this scan instead
  // of blocking on every queued job with waitForDone().
  const QString databasePath = m_databasePath;

  QFuture<void> future = QtConcurrent::run(
      &m_writePool,
      [databasePath, root = std::move(root), currentPath = std::move(currentPath),
       currentTitle = std::move(currentTitle),
       currentBlocks = std::move(currentBlocks)]() {
        executeIncrementalSync(databasePath, root, currentPath, currentTitle,
                               currentBlocks);
      });

  future.waitForFinished();

  invalidateSearchCache();
  emit indexUpdated();
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

            invalidateSearchCache();
            emit indexUpdated();
          });

  const QString databasePath = m_databasePath;

  watcher->setFuture(QtConcurrent::run(
      &m_writePool,
      [databasePath, root = std::move(root), currentPath = std::move(currentPath),
       currentTitle = std::move(currentTitle),
       currentBlocks = std::move(currentBlocks)]() {
        executeIncrementalSync(databasePath, root, currentPath, currentTitle,
                               currentBlocks);
      }));
}

void SearchIndex::rebuild() {
  if (!m_db.isOpen() || m_databasePath.isEmpty())
    return;

  // Wipe through the writer pool so queued live writes are applied first and
  // no in-flight batch can reinsert rows after the clear. The rescan itself is
  // queued behind the clear on the same single-thread pool.
  const QString databasePath = m_databasePath;

  auto *watcher = new QFutureWatcher<void>(this);

  connect(watcher, &QFutureWatcherBase::finished, this,
          [this, watcher]() {
            watcher->deleteLater();
            invalidateSearchCache();
            emit indexUpdated();
            syncIncrementalAsync();
          });

  watcher->setFuture(QtConcurrent::run(
      &m_writePool, [databasePath]() { executeClearIndex(databasePath); }));
}

void SearchIndex::deliverSearchOutcome(quint64 token,
                                       const SearchOutcome &outcome) {
  // All callers are queued onto the GUI thread, so no lock is needed. A reply
  // for a superseded token must not overwrite results of a newer query.
  if (m_lastDeliveredToken && token < *m_lastDeliveredToken)
    return;

  m_lastDeliveredToken = token;
  emit searchFinished(token, outcome);
}

void SearchIndex::invalidateSearchCache() {
  // Do not clear every entry on each indexUpdated()/live edit: that threw away
  // all 24 cached queries on every keystroke. Entries are generation-tagged
  // instead and discarded lazily by the lookup paths below.
  ++m_cacheGeneration;
}

bool SearchIndex::cacheEntryIsFresh(const CachedSearch &entry) const {
  return entry.generation == m_cacheGeneration;
}

bool SearchIndex::cacheEntryWithinGrace(const CachedSearch &entry) const {
  // Tradeoff: an async reply can lag the index by at most kCacheGraceMs after
  // the entry was produced, which keeps typing bursts from re-running FTS5 for
  // an unchanged query. Because the age is absolute (the clock is never
  // restarted), continuous edits cannot extend the grace period indefinitely;
  // search() deliberately has no grace so callers needing current data are
  // always exact.
  return entry.generation != m_cacheGeneration && m_cacheClock.isValid() &&
         m_cacheClock.elapsed() - entry.ageMs < kCacheGraceMs;
}

void SearchIndex::cacheSearchResult(const QString &query,
                                    const CachedSearch &cached) const {
  if (query.isEmpty())
    return;

  if (!m_searchCache.contains(query))
    m_searchCacheOrder.append(query);

  CachedSearch stored = cached;
  stored.generation = m_cacheGeneration;
  stored.ageMs = m_cacheClock.elapsed();
  m_searchCache.insert(query, stored);

  constexpr int kCacheSize = 24;
  while (m_searchCacheOrder.size() > kCacheSize)
    m_searchCache.remove(m_searchCacheOrder.takeFirst());
}

void SearchIndex::finalizeResults(QVector<Result> &results) {
  QHash<QString, double> bestRank;
  QHash<QString, int> documentCounts;

  for (const Result &result : std::as_const(results)) {
    const double rank = bestRank.value(result.documentPath, result.rank);
    bestRank.insert(result.documentPath, qMin(rank, result.rank));
    documentCounts[result.documentPath] += result.matchCount;
  }

  for (Result &result : results)
    result.documentMatchCount = documentCounts.value(result.documentPath);

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

                     return left.documentPath < right.documentPath;
                   });
}

SearchIndex::SearchOutcome SearchIndex::executeSearch(const QSqlDatabase &db,
                                                      const QString &query) {
  SearchOutcome outcome;
  const QStringList terms = queryTerms(query);

  if (terms.isEmpty() || !db.isOpen())
    return outcome;

  // Tokenize/fold the query once instead of once per result row.
  const HighlightTerms highlight = prepareHighlightTerms(terms);
  if (highlight.needles.isEmpty())
    return outcome;

  QSqlQuery sql(db);
  sql.prepare(QStringLiteral(
      "SELECT document_path, document_title, block_id, block_text, "
      "bm25(search_index) "
      "FROM search_index WHERE search_index MATCH ? "
      "ORDER BY bm25(search_index) LIMIT ?"));
  sql.addBindValue(buildFtsQuery(terms));
  sql.addBindValue(kMaxResults + 1);

  if (!sql.exec()) {
    qWarning() << "SearchIndex: query failed:" << sql.lastError().text();
    return outcome;
  }

  int sqlRowCount = 0;
  while (sql.next()) {
    ++sqlRowCount;

    Result result;
    result.documentPath = sql.value(0).toString();
    result.documentTitle = sql.value(1).toString();
    result.blockId = sql.value(2).toString();
    result.blockText = sql.value(3).toString();
    result.rank = sql.value(4).toDouble();

    result.ranges = matchRanges(result.blockText, highlight);
    if (result.ranges.isEmpty())
      continue;

    result.matchCount = result.ranges.size();
    result.snippet =
        makeSnippet(result.blockText, result.ranges, &result.snippetRanges);
    outcome.results.append(std::move(result));
  }

  // FTS returns at most kMaxResults + 1 rows. Count them before highlight
  // filtering so an unhighlightable row cannot hide that there were more
  // matches; still cap the visible results at kMaxResults.
  outcome.truncated = sqlRowCount > kMaxResults;

  if (outcome.results.size() > kMaxResults)
    outcome.results.resize(kMaxResults);

  finalizeResults(outcome.results);
  return outcome;
}

SearchIndex::SearchOutcome
SearchIndex::executeSearchInConnection(const QString &databasePath,
                                       const QString &query) {
  // The connection is owned by this worker thread's ThreadSqlConnections, so
  // searches reuse it instead of opening/closing SQLite on every keystroke.
  const QSqlDatabase db = threadSqlConnections().localData().connection(
      databasePath, /*readOnly=*/true,
      databaseEpoch().load(std::memory_order_relaxed));

  if (!db.isOpen())
    return {};

  return executeSearch(db, query);
}

void SearchIndex::searchAsync(const QString &query, quint64 token) {
  const QString key = query.simplified();

  if (key.isEmpty() || !m_db.isOpen()) {
    QMetaObject::invokeMethod(
        this,
        [this, token]() { deliverSearchOutcome(token, SearchOutcome{}); },
        Qt::QueuedConnection);
    return;
  }

  if (const auto cached = m_searchCache.constFind(key);
      cached != m_searchCache.constEnd() &&
      (cacheEntryIsFresh(*cached) || cacheEntryWithinGrace(*cached))) {
    SearchOutcome outcome{cached->results, cached->truncated};
    const quint64 generation = m_cacheGeneration;
    QMetaObject::invokeMethod(
        this,
        [this, token, outcome, generation]() {
          if (generation == m_cacheGeneration)
            deliverSearchOutcome(token, outcome);
        },
        Qt::QueuedConnection);
    return;
  }

  const QString databasePath = m_databasePath;
  const quint64 generation = m_cacheGeneration;

  auto *watcher = new QFutureWatcher<SearchOutcome>(this);

  connect(watcher, &QFutureWatcherBase::finished, this,
          [this, watcher, key, token, generation]() {
            const SearchOutcome outcome = watcher->result();
            watcher->deleteLater();

            // The index may have changed while the worker was running; do not
            // repopulate the cache with a stale result.
            if (generation != m_cacheGeneration)
              return;

            cacheSearchResult(key,
                              CachedSearch{outcome.results, outcome.truncated});
            deliverSearchOutcome(token, outcome);
          });

  watcher->setFuture(
      QtConcurrent::run(executeSearchInConnection, databasePath, key));
}

QVector<SearchIndex::Result> SearchIndex::search(const QString &query,
                                                bool *truncated) const {
  QVector<Result> results;

  if (truncated)
    *truncated = false;

  const QString key = query.simplified();

  if (key.isEmpty() || !m_db.isOpen())
    return results;

  // Strict: only the current generation is usable here. Tests and callers of
  // the synchronous API expect the result to reflect the latest completed
  // write; the grace window applies to searchAsync() only.
  if (const auto cached = m_searchCache.constFind(key);
      cached != m_searchCache.constEnd() && cacheEntryIsFresh(*cached)) {
    if (truncated)
      *truncated = cached->truncated;
    return cached->results;
  }

  // Only the exact-key cache is used. A previous optimisation refined a longer
  // query by filtering the rows of a cached prefix and inventing a surrogate
  // rank, but bm25 scores are query-specific: the surrogate cannot reproduce
  // fresh ranking, so results differed depending on which queries had been
  // typed first. Always execute the search for a non-exact query instead.
  const SearchOutcome outcome = executeSearch(m_db, key);

  if (truncated)
    *truncated = outcome.truncated;

  cacheSearchResult(key, CachedSearch{outcome.results, outcome.truncated});
  return outcome.results;
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

SearchIndex::HighlightTerms
SearchIndex::prepareHighlightTerms(const QStringList &terms) {
  HighlightTerms prepared;

  for (const QString &term : terms) {
    // The FTS query tokenizes terms and matches token prefixes. Split the
    // term the same way so punctuation-containing queries still highlight the
    // words that actually caused the match.
    QStringList needles;
    QString token;

    for (const QChar character : term) {
      if (isTokenCharacter(character)) {
        token.append(character);
      } else if (!token.isEmpty()) {
        needles.append(token);
        token.clear();
      }
    }

    if (!token.isEmpty())
      needles.append(token);

    for (const QString &needle : needles) {
      const QString folded = foldForMatching(needle);

      if (folded.isEmpty())
        continue;

      bool isAscii = true;
      for (const QChar character : folded) {
        if (character.unicode() > 0x7F) {
          isAscii = false;
          break;
        }
      }

      prepared.needles.append(HighlightNeedle{folded, isAscii});
      if (!isAscii)
        prepared.allAscii = false;
    }
  }

  return prepared;
}

QVector<SearchIndex::MatchRange>
SearchIndex::matchRanges(const QString &text, const HighlightTerms &terms) {
  QVector<MatchRange> ranges;

  // A punctuation-only query produces no needles: short-circuit before
  // touching the haystack at all.
  if (text.isEmpty() || terms.needles.isEmpty())
    return ranges;

  int shortestNeedle = text.size() + 1;
  for (const HighlightNeedle &needle : terms.needles)
    shortestNeedle = qMin(shortestNeedle, needle.folded.size());

  if (shortestNeedle > text.size())
    return ranges;

  // When both sides are ASCII, folding would be a byte-for-byte copy and the
  // offset map would be the identity. Search the original text directly and
  // skip both allocations; only accented text needs the folded-copy fallback.
  bool textIsAscii = true;
  if (terms.allAscii) {
    for (const QChar character : text) {
      if (character.unicode() > 0x7F) {
        textIsAscii = false;
        break;
      }
    }
  }
  const bool useFastPath = terms.allAscii && textIsAscii;

  QVector<int> offsetMap;
  QString foldedText;
  if (!useFastPath)
    foldedText = foldForMatching(text, &offsetMap);

  const QString &searchText = useFastPath ? text : foldedText;
  const int searchLength = searchText.length();

  for (const HighlightNeedle &needle : terms.needles) {
    if (needle.folded.size() > searchLength)
      continue;

    int from = 0;

    while (from <= searchLength - needle.folded.length() &&
           ranges.size() < kMaxHighlightRanges) {
      const int start =
          searchText.indexOf(needle.folded, from, Qt::CaseInsensitive);

      if (start < 0)
        break;

      int originalStart = start;
      int originalEnd = start + needle.folded.length();

      if (!useFastPath) {
        originalStart = offsetMap.value(start, start);
        const int lastFolded = start + needle.folded.length() - 1;
        originalEnd = offsetMap.value(lastFolded, lastFolded) + 1;
      }
      const int length = originalEnd - originalStart;

      // Only a match at the beginning of a token is a real FTS prefix
      // match; "edit" must not highlight the middle of "credit".
      if (length > 0 &&
          (originalStart == 0 ||
           !isTokenCharacter(text.at(originalStart - 1)))) {
        ranges.append({originalStart, length});
        from = start + needle.folded.length();
      } else {
        from = start + 1;
      }
    }

    if (ranges.size() >= kMaxHighlightRanges)
      break;
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
