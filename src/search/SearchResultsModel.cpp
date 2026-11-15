#include "SearchResultsModel.h"

#include <QVariantMap>

namespace Templnote {
SearchResultsModel::SearchResultsModel(QObject *parent)
    : QAbstractListModel(parent) {}

int SearchResultsModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : m_results.size();
}

QVariant SearchResultsModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_results.size())
    return {};

  const SearchIndex::Result &result = m_results[index.row()];

  switch (role) {
  case DocumentTitleRole:
    return result.documentTitle;
  case DocumentPathRole:
    return result.documentPath;
  case BlockIdRole:
    return result.blockId;
  case BlockTextRole:
    return result.blockText;
  case SnippetRole:
    return result.snippet;
  case RangesRole:
    return rangesToList(result.ranges);
  case SnippetRangesRole:
    return rangesToList(result.snippetRanges);
  case MatchCountRole:
    return result.matchCount;
  case DocumentMatchCountRole:
    return result.documentMatchCount;
  case RankRole:
    return result.rank;
  case Qt::DisplayRole:
    return result.snippet;
  default:
    return {};
  }
}

QHash<int, QByteArray> SearchResultsModel::roleNames() const {
  return {{DocumentTitleRole, "documentTitle"},
          {DocumentPathRole, "documentPath"},
          {BlockIdRole, "blockId"},
          {BlockTextRole, "blockText"},
          {SnippetRole, "snippet"},
          {RangesRole, "ranges"},
          {SnippetRangesRole, "snippetRanges"},
          {MatchCountRole, "matchCount"},
          {DocumentMatchCountRole, "documentMatchCount"},
          {RankRole, "rank"}};
}

int SearchResultsModel::count() const { return m_results.size(); }

SearchIndex *SearchResultsModel::index() const { return m_index; }

void SearchResultsModel::setIndex(SearchIndex *index) {
  if (m_index == index)
    return;

  m_index = index;

  beginResetModel();
  m_results.clear();
  endResetModel();

  emit countChanged();
  emit indexChanged();
}

void SearchResultsModel::search(const QString &query) {
  const QVector<SearchIndex::Result> results =
      m_index ? m_index->search(query) : QVector<SearchIndex::Result>();

  beginResetModel();
  m_results = results;
  endResetModel();

  emit countChanged();
}

QVariantMap SearchResultsModel::resultAt(int row) const {
  QVariantMap map;

  if (row < 0 || row >= m_results.size())
    return map;

  const SearchIndex::Result &result = m_results[row];

  map.insert(QStringLiteral("documentTitle"), result.documentTitle);
  map.insert(QStringLiteral("documentPath"), result.documentPath);
  map.insert(QStringLiteral("blockId"), result.blockId);
  map.insert(QStringLiteral("blockText"), result.blockText);
  map.insert(QStringLiteral("snippet"), result.snippet);
  map.insert(QStringLiteral("ranges"), rangesToList(result.ranges));
  map.insert(QStringLiteral("snippetRanges"),
             rangesToList(result.snippetRanges));
  map.insert(QStringLiteral("matchCount"), result.matchCount);
  map.insert(QStringLiteral("documentMatchCount"),
             result.documentMatchCount);
  map.insert(QStringLiteral("rank"), result.rank);

  return map;
}

QString SearchResultsModel::groupTitle(const QString &documentPath) const {
  for (const SearchIndex::Result &result : m_results) {
    if (result.documentPath == documentPath)
      return result.documentTitle;
  }

  return documentPath;
}

int SearchResultsModel::groupMatchCount(const QString &documentPath) const {
  for (const SearchIndex::Result &result : m_results) {
    if (result.documentPath == documentPath)
      return result.documentMatchCount;
  }

  return 0;
}

QVariantList SearchResultsModel::rangesToList(
    const QVector<SearchIndex::MatchRange> &ranges) {
  QVariantList list;
  list.reserve(ranges.size());

  for (const SearchIndex::MatchRange &range : ranges) {
    list.append(QVariantMap{{QStringLiteral("start"), range.start},
                            {QStringLiteral("length"), range.length}});
  }

  return list;
}
} // namespace Templnote
