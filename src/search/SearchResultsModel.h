#pragma once

#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <qtmetamacros.h>

#include "SearchIndex.h"

namespace Templnote {
// Flat result list (one row per matching block) exposed to QML. Rows are
// grouped by document by the FTS ordering, so ResultsList can use the
// documentPath role as a ListView section property.
class SearchResultsModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int count READ count NOTIFY countChanged)
  Q_PROPERTY(bool truncated READ truncated NOTIFY truncatedChanged)
  Q_PROPERTY(SearchIndex *index READ index WRITE setIndex NOTIFY indexChanged)

public:
  enum Roles {
    DocumentTitleRole = Qt::UserRole + 1,
    DocumentPathRole,
    DocumentRelativePathRole,
    BlockIdRole,
    BlockTextRole,
    SnippetRole,
    RangesRole,
    SnippetRangesRole,
    MatchCountRole,
    DocumentMatchCountRole,
    RankRole
  };

  Q_ENUM(Roles)

  explicit SearchResultsModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  int count() const;
  bool truncated() const;

  SearchIndex *index() const;
  void setIndex(SearchIndex *index);

  Q_INVOKABLE void search(const QString &query);

  // Complete data for one row, used by the preview pane and by Enter.
  Q_INVOKABLE QVariantMap resultAt(int row) const;

  // Section header helpers, keyed by document path.
  Q_INVOKABLE QString groupTitle(const QString &documentPath) const;
  Q_INVOKABLE QString relativeDocumentPath(const QString &documentPath) const;
  Q_INVOKABLE int groupMatchCount(const QString &documentPath) const;

signals:
  void countChanged();
  void truncatedChanged();
  void indexChanged();

private:
  static QVariantList rangesToList(const QVector<SearchIndex::MatchRange> &ranges);

  SearchIndex *m_index = nullptr;
  QVector<SearchIndex::Result> m_results;
  bool m_truncated = false;
};
} // namespace Templnote
