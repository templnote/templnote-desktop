#include "SearchResultsModel.h"

#include <QDir>
#include <QFileInfo>
#include <QVariantMap>

#include "storage/AppPaths.h"

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
  case DocumentRelativePathRole:
    return relativeDocumentPath(result.documentPath);
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
          {DocumentRelativePathRole, "documentRelativePath"},
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

bool SearchResultsModel::truncated() const { return m_truncated; }

SearchIndex *SearchResultsModel::index() const { return m_index; }

void SearchResultsModel::setIndex(SearchIndex *index) {
  if (m_index == index)
    return;

  if (m_index)
    disconnect(m_index, nullptr, this, nullptr);

  m_index = index;

  if (m_index) {
    connect(m_index, &SearchIndex::searchFinished, this,
            &SearchResultsModel::onSearchFinished);
  }

  ++m_searchToken;

  beginResetModel();
  m_results.clear();
  m_groupTitles.clear();
  m_groupCounts.clear();
  endResetModel();

  const bool hadTruncated = m_truncated;
  m_truncated = false;

  emit countChanged();

  if (hadTruncated)
    emit truncatedChanged();

  emit indexChanged();
}

void SearchResultsModel::search(const QString &query) {
  bool truncated = false;
  const QVector<SearchIndex::Result> results =
      m_index ? m_index->search(query, &truncated)
              : QVector<SearchIndex::Result>();

  applyResults(results, truncated);
}

void SearchResultsModel::searchAsync(const QString &query) {
  const quint64 token = ++m_searchToken;

  if (!m_index) {
    onSearchFinished(token, SearchIndex::SearchOutcome{});
    return;
  }

  m_index->searchAsync(query, token);
}

void SearchResultsModel::onSearchFinished(
    quint64 token, const SearchIndex::SearchOutcome &outcome) {
  if (token != m_searchToken)
    return;

  applyResults(outcome.results, outcome.truncated);
}

void SearchResultsModel::applyResults(
    const QVector<SearchIndex::Result> &results, bool truncated) {
  const bool truncationChanged = m_truncated != truncated;

  // Keep the leading rows that did not change so the view can reuse their
  // delegates instead of rebuilding the whole list on every keystroke.
  int common = 0;
  const int limit = qMin(m_results.size(), results.size());

  while (common < limit &&
         m_results[common].documentPath == results[common].documentPath &&
         m_results[common].blockId == results[common].blockId)
    ++common;

  if (common > 0)
    emit dataChanged(createIndex(0, 0), createIndex(common - 1, 0));

  if (m_results.size() > common) {
    beginRemoveRows({}, common, m_results.size() - 1);
    m_results.resize(common);
    endRemoveRows();
  }

  if (results.size() > common) {
    beginInsertRows({}, common, results.size() - 1);
    for (int row = common; row < results.size(); ++row)
      m_results.append(results[row]);
    endInsertRows();
  }

  m_truncated = truncated;

  m_groupTitles.clear();
  m_groupCounts.clear();

  for (const SearchIndex::Result &result : std::as_const(m_results)) {
    m_groupTitles.insert(result.documentPath, result.documentTitle);
    m_groupCounts.insert(result.documentPath, result.documentMatchCount);
  }

  emit countChanged();

  if (truncationChanged)
    emit truncatedChanged();
}

QVariantMap SearchResultsModel::resultAt(int row) const {
  QVariantMap map;

  if (row < 0 || row >= m_results.size())
    return map;

  const SearchIndex::Result &result = m_results[row];

  map.insert(QStringLiteral("documentTitle"), result.documentTitle);
  map.insert(QStringLiteral("documentPath"), result.documentPath);
  map.insert(QStringLiteral("documentRelativePath"),
             relativeDocumentPath(result.documentPath));
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
  return m_groupTitles.value(documentPath, documentPath);
}

QString SearchResultsModel::relativeDocumentPath(
    const QString &documentPath) const {
  if (documentPath.isEmpty())
    return {};

  const QString root = m_index ? m_index->documentsRoot()
                               : AppPaths::documentsDirectory();
  if (root.isEmpty())
    return documentPath;

  const QString rel = QDir(root).relativeFilePath(documentPath);
  if (rel.isEmpty() || rel == QLatin1String(".") ||
      rel.startsWith(QLatin1String("..")) || QDir::isAbsolutePath(rel)) {
    return QFileInfo(documentPath).fileName();
  }

  return QDir::cleanPath(rel);
}

int SearchResultsModel::groupMatchCount(const QString &documentPath) const {
  return m_groupCounts.value(documentPath, 0);
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
