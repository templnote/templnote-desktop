#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QString>
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

  // Returns matching blocks grouped and ordered by the best rank per
  // document. Plain text queries are split into whitespace-separated terms;
  // every term must prefix-match a token in the block text (FTS5 AND).
  QVector<Result> search(const QString &query) const;

  // Reconciles the index with files on disk. Used at startup, before opening
  // global search, and after destructive document-list operations.
  Q_INVOKABLE void syncIncremental();

  // Drops the complete index and rebuilds it from disk.
  Q_INVOKABLE void rebuild();

signals:
  void blockModelChanged();
  void documentListChanged();

private:
  struct BlockRecord {
    QString blockId;
    QString text;
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

  void queueBlockUpsert(const QString &path, const QString &title,
                        const QString &blockId, const QString &text);
  void queueBlockRemove(const QString &path, const QString &blockId);
  void queueFullDocument(const QString &path);
  void scheduleFlush();
  void flushPending();
  void discardPending(const QString &pathPrefix);

  bool replaceDocument(const QString &path, const QString &title,
                       const QVector<BlockRecord> &blocks, bool updateMeta);
  bool reindexDocumentFromFile(const QString &path);
  bool reindexCurrentDocument();
  bool upsertBlock(const QString &path, const QString &title,
                   const QString &blockId, const QString &text);
  bool removeBlock(const QString &path, const QString &blockId);
  bool removeDocument(const QString &path);
  bool removePathPrefix(const QString &path);
  bool renamePath(const QString &oldPath, const QString &newPath);
  bool invalidateMeta(const QString &path);
  bool updateMeta(const QString &path, const QString &title);
  bool readMeta(const QString &path, qint64 *modifiedMs, qint64 *size) const;

  static QString connectionNameFor(const SearchIndex *index);
  static QString normalizedPath(const QString &path);
  static QStringList queryTerms(const QString &query);
  static QString buildFtsQuery(const QStringList &terms);
  static QVector<MatchRange> matchRanges(const QString &text,
                                         const QStringList &terms);
  static QString makeSnippet(const QString &text,
                             const QVector<MatchRange> &ranges,
                             QVector<MatchRange> *snippetRanges);

  QString documentsRoot() const;

  QSqlDatabase m_db;
  QString m_connectionName;
  QString m_documentsRoot;
  QString m_databasePath;
  QTimer m_flushTimer;
  QVector<PendingOp> m_pending;

  BlockModel *m_blockModel = nullptr;
  DocumentListModel *m_documentList = nullptr;
  QString m_currentDocumentPath;
};
} // namespace Templnote
