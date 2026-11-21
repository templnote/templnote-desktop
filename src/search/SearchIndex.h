#pragma once

#include <QFuture>
#include <QHash>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>
#include <QVector>

#include <qtmetamacros.h>

#include "../model/BlockModel.h"
#include "../storage/DocumentListModel.h"

namespace Templnote {
// SQLite FTS5 index over every block of every .templnote document.
//
// The index is a derived cache: documents on disk are the source of truth.
// Live edits are written through debounced per-block operations, while a
// startup/on-demand sync reconciles files that changed outside the app.
class SearchIndex : public QObject {
  Q_OBJECT
  Q_PROPERTY(BlockModel *blockModel READ blockModel WRITE setBlockModel NOTIFY
                 blockModelChanged)
  Q_PROPERTY(DocumentListModel *documentList READ documentList WRITE
                 setDocumentList NOTIFY documentListChanged)

public:
  struct MatchRange {
    int start = 0;
    int length = 0;
  };

  struct Result {
    QString documentPath;
    QString documentTitle;
    QString blockId;
    QString blockText;
    QString snippet;
    QVector<MatchRange> ranges;
    QVector<MatchRange> snippetRanges;
    int matchCount = 0;
    int documentMatchCount = 0;
    double rank = 0.0;
  };

  struct SearchOutcome {
    QVector<Result> results;
    bool truncated = false;
  };

  explicit SearchIndex(QObject *parent = nullptr);

  // Explicit locations for tests and embedding. `documentsRoot` is the tree
  // that syncIncremental() scans; `databasePath` is the SQLite file to use.
  SearchIndex(const QString &documentsRoot, const QString &databasePath,
              QObject *parent = nullptr);
  ~SearchIndex() override;

  BlockModel *blockModel() const;
  void setBlockModel(BlockModel *model);

  DocumentListModel *documentList() const;
  void setDocumentList(DocumentListModel *model);

  QString documentsRoot() const;

  // Returns matching blocks grouped and ordered by the best rank per
  // document. Plain text queries are split into whitespace-separated terms;
  // every term must prefix-match a token in the block text (FTS5 AND).
  // At most 200 results are returned; when `truncated` is non-null it is set
  // to true if more matches existed.
  QVector<Result> search(const QString &query,
                         bool *truncated = nullptr) const;

  // Runs the same query on a pooled worker thread and delivers the result with
  // `token` attached so callers can discard stale replies. Cache hits are
  // delivered asynchronously too, keeping the caller's ordering predictable.
  Q_INVOKABLE void searchAsync(const QString &query, quint64 token);

  // Reconciles the index with files on disk. Used at startup, before opening
  // global search, and after destructive document-list operations.
  Q_INVOKABLE void syncIncremental();

  // Runs the same reconciliation on the writer pool instead of blocking the
  // caller. Emits indexUpdated() when the scan finishes.
  Q_INVOKABLE void syncIncrementalAsync();

  // Drops the complete index and rebuilds it from disk.
  Q_INVOKABLE void rebuild();

signals:
  void blockModelChanged();
  void documentListChanged();
  void indexUpdated();
  void searchFinished(quint64 token, const SearchOutcome &outcome);

private:
  struct CachedSearch {
    QVector<Result> results;
    bool truncated = false;
  };

  struct PendingOp {
    enum class Type { Upsert, Remove, FullDocument };

    Type type = Type::Upsert;
    QString path;
    QString title;
    QString blockId;
    QString text;
  };

  bool openDatabase();
  bool createSchema();
  bool resetDatabaseFile();

  void connectModelSignals();
  void disconnectModelSignals();

  void onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight,
                     const QList<int> &roles);
  void onRowsInserted(const QModelIndex &parent, int first, int last);
  void onRowsAboutToBeRemoved(const QModelIndex &parent, int first, int last);
  void onCurrentFilePathChanged();
  void onDocumentLoaded();
  void onDocumentCreated();
  void onPathRenamed(const QString &oldPath, const QString &newPath);
  void onPathRemoved(const QString &path);

  void invalidateSearchCache();
  void cacheSearchResult(const QString &query, const CachedSearch &cached) const;

  void queueBlockUpsert(const QString &path, const QString &title,
                        const QString &blockId, const QString &text);
  void queueBlockRemove(const QString &path, const QString &blockId);
  void queueFullDocument(const QString &path);
  void scheduleFlush();
  void watchWriteFuture(QFuture<void> future);
  void flushPending();
  void requeuePending(const QVector<PendingOp> &ops);
  void discardPending(const QString &pathPrefix);

  static QString connectionNameFor(const SearchIndex *index);
  static QString normalizedPath(const QString &path);
  static void finalizeResults(QVector<Result> &results);
  static SearchOutcome executeSearch(const QSqlDatabase &db,
                                     const QString &query);
  static SearchOutcome executeSearchInConnection(const QString &databasePath,
                                                 const QString &query);
  static QStringList queryTerms(const QString &query);
  static QString buildFtsQuery(const QStringList &terms);
  static QVector<MatchRange> matchRanges(const QString &text,
                                         const QStringList &terms);
  static QString makeSnippet(const QString &text,
                             const QVector<MatchRange> &ranges,
                             QVector<MatchRange> *snippetRanges);

  QSqlDatabase m_db;
  QString m_connectionName;
  QString m_documentsRoot;
  QString m_databasePath;
  QTimer m_flushTimer;
  QVector<PendingOp> m_pending;
  // Failed write batches are retried with exponential backoff instead of being
  // silently dropped.
  int m_flushRetryCount = 0;

  // Small LRU of recent queries. A longer query that starts with a cached
  // query can be answered by filtering the cached results instead of hitting
  // FTS5 again, which makes backspacing/typing feel immediate.
  mutable QHash<QString, CachedSearch> m_searchCache;
  mutable QStringList m_searchCacheOrder;
  mutable quint64 m_cacheGeneration = 0;

  // Live-edit writes are applied off the GUI thread. A single worker keeps
  // SQLite writers serialized while reads continue on the main connection.
  QThreadPool m_writePool;

  BlockModel *m_blockModel = nullptr;
  DocumentListModel *m_documentList = nullptr;
  QString m_currentDocumentPath;
  bool m_syncRunning = false;
  bool m_syncQueued = false;
};
} // namespace Templnote

Q_DECLARE_METATYPE(Templnote::SearchIndex::SearchOutcome)
