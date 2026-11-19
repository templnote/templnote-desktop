#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "../src/core/Block.h"
#include "../src/core/Document.h"
#include "../src/model/BlockModel.h"
#include "../src/search/SearchIndex.h"
#include "../src/search/SearchResultsModel.h"
#include "../src/storage/DocumentListModel.h"

using namespace Templnote;

namespace {
QString writeDocument(const QString &root, const QString &relativePath,
                      const QString &title,
                      const QVector<QString> &blockTexts) {
  const QString path = QDir(root).filePath(relativePath);

  if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    return {};

  Document document;

  for (const QString &text : blockTexts)
    document.appendBlock(Block::create(BlockType::Paragraph, text));

  QJsonObject json = document.toJson();
  json.insert(QStringLiteral("title"), title);
  json.insert(QStringLiteral("createdAt"),
              QStringLiteral("2026-01-01T00:00:00Z"));
  json.insert(QStringLiteral("modifiedAt"),
              QStringLiteral("2026-01-01T00:00:00Z"));

  QFile file(path);

  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return {};

  file.write(QJsonDocument(json).toJson());
  file.close();

  return QFileInfo(path).absoluteFilePath();
}

QStringList resultPaths(const QVector<SearchIndex::Result> &results) {
  QStringList paths;

  for (const SearchIndex::Result &result : results)
    paths.append(result.documentPath);

  return paths;
}

// Startup indexing runs on the writer pool now. Tests that inspect the index
// immediately must let the queued scan finish first.
void waitForInitialIndex(SearchIndex &index) {
  QSignalSpy spy(&index, &SearchIndex::indexUpdated);
  QVERIFY(spy.wait(5000));
}
} // namespace

class SearchIndexTests : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("TemplnoteSearchTest");
    QCoreApplication::setApplicationName("TemplnoteSearchTest");
  }

  void indexesDocumentsOnStartup() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString root = temp.filePath("documents");

    const QString alpha = writeDocument(
        root, QStringLiteral("Alpha.templnote"), QStringLiteral("Alpha"),
        {QStringLiteral("alpha body")});
    const QString beta = writeDocument(
        root, QStringLiteral("Nested/Beta.templnote"), QStringLiteral("Beta"),
        {QStringLiteral("beta body")});
    QVERIFY(!alpha.isEmpty());
    QVERIFY(!beta.isEmpty());

    SearchIndex index(root, temp.filePath("search.sqlite"));
    waitForInitialIndex(index);

    const auto alphaResults = index.search(QStringLiteral("alpha"));
    QCOMPARE(alphaResults.size(), 1);
    QCOMPARE(alphaResults.first().documentPath, alpha);
    QCOMPARE(alphaResults.first().documentTitle, QStringLiteral("Alpha"));
    QVERIFY(!alphaResults.first().blockId.isEmpty());
    QCOMPARE(alphaResults.first().snippet, QStringLiteral("alpha body"));
    QCOMPARE(alphaResults.first().matchCount, 1);
    QCOMPARE(alphaResults.first().documentMatchCount, 1);
    QCOMPARE(alphaResults.first().ranges.size(), 1);
    QCOMPARE(alphaResults.first().ranges.first().start, 0);
    QCOMPARE(alphaResults.first().ranges.first().length, 5);

    const auto betaResults = index.search(QStringLiteral("beta"));
    QCOMPARE(betaResults.size(), 1);
    QCOMPARE(betaResults.first().documentPath, beta);
    QCOMPARE(betaResults.first().documentTitle, QStringLiteral("Beta"));
  }

  void legacyIndexWithoutRowidMapIsMigrated() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString root = temp.filePath("documents");
    const QString path = writeDocument(
        root, QStringLiteral("Legacy.templnote"), QStringLiteral("Legacy"),
        {QStringLiteral("legacy token")});
    QVERIFY(!path.isEmpty());

    const QString databasePath = temp.filePath("search.sqlite");

    // Reproduce a database written before search_rows existed: FTS rows are
    // present but there is no rowid map.
    {
      QSqlDatabase db =
          QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                    QStringLiteral("legacy-index-setup"));
      db.setDatabaseName(databasePath);
      QVERIFY(db.open());

      QSqlQuery query(db);
      QVERIFY(query.exec(QStringLiteral(
          "CREATE VIRTUAL TABLE search_index USING fts5("
          "document_path UNINDEXED, document_title, block_id UNINDEXED, "
          "block_text)")));
      QVERIFY(query.exec(QStringLiteral(
          "CREATE TABLE search_meta(document_path TEXT PRIMARY KEY, "
          "document_title TEXT, modified_ms INTEGER, size INTEGER)")));

      QSqlQuery insert(db);
      QVERIFY(insert.prepare(QStringLiteral(
          "INSERT INTO search_index(document_path, document_title, block_id, "
          "block_text) VALUES(?, ?, ?, ?)")));
      insert.addBindValue(QFileInfo(path).absoluteFilePath());
      insert.addBindValue(QStringLiteral("Legacy"));
      insert.addBindValue(QStringLiteral("legacy-block"));
      insert.addBindValue(QStringLiteral("legacy token"));
      QVERIFY(insert.exec());

      db.close();
    }

    QSqlDatabase::removeDatabase(QStringLiteral("legacy-index-setup"));

    SearchIndex index(root, databasePath);
    waitForInitialIndex(index);

    // The legacy row must be found through the rebuilt map and replaced, not
    // duplicated next to the freshly indexed copy.
    const auto results = index.search(QStringLiteral("legacy"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().documentPath, QFileInfo(path).absoluteFilePath());
  }

  void prefixesAndAllTerms() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString root = temp.filePath("documents");
    const QString path = writeDocument(
        root, QStringLiteral("Search.templnote"), QStringLiteral("Search"),
        {QStringLiteral("multi tabs for editing")});
    QVERIFY(!path.isEmpty());

    SearchIndex index(root, temp.filePath("search.sqlite"));
    waitForInitialIndex(index);

    QCOMPARE(index.search(QStringLiteral("mult")).size(), 1);
    QCOMPARE(index.search(QStringLiteral("edi")).size(), 1);
    QCOMPARE(index.search(QStringLiteral("mult tabs")).size(), 1);
    QCOMPARE(index.search(QStringLiteral("mult editing")).size(), 1);
    QCOMPARE(index.search(QStringLiteral("mult zebra")).size(), 0);
    QCOMPARE(index.search(QStringLiteral("")).size(), 0);
    QCOMPARE(index.search(QStringLiteral("   ")).size(), 0);
  }

  void resultsAreGroupedByDocument() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString root = temp.filePath("documents");

    const QString first = writeDocument(
        root, QStringLiteral("One.templnote"), QStringLiteral("One"),
        {QStringLiteral("needle one"), QStringLiteral("needle two")});
    const QString second = writeDocument(
        root, QStringLiteral("Two.templnote"), QStringLiteral("Two"),
        {QStringLiteral("needle three")});
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());

    SearchIndex index(root, temp.filePath("search.sqlite"));
    waitForInitialIndex(index);

    const auto results = index.search(QStringLiteral("needle"));
    QCOMPARE(results.size(), 3);

    QSet<QString> seen;
    QString current;

    for (const SearchIndex::Result &result : results) {
      if (result.documentPath != current) {
        QVERIFY(!seen.contains(result.documentPath));
        seen.insert(result.documentPath);
        current = result.documentPath;
      }

      QCOMPARE(result.documentMatchCount,
               result.documentPath == first ? 2 : 1);
    }

    QCOMPARE(seen.size(), 2);
    QVERIFY(seen.contains(first));
    QVERIFY(seen.contains(second));
  }

  void reportsEveryOccurrenceInBlock() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString root = temp.filePath("documents");
    const QString path = writeDocument(
        root, QStringLiteral("Repeats.templnote"), QStringLiteral("Repeats"),
        {QStringLiteral("cat catalog cat")});
    QVERIFY(!path.isEmpty());

    SearchIndex index(root, temp.filePath("search.sqlite"));
    waitForInitialIndex(index);

    const auto results = index.search(QStringLiteral("cat"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().matchCount, 3);
    QCOMPARE(results.first().ranges.size(), 3);
    QCOMPARE(results.first().ranges.at(0).start, 0);
    QCOMPARE(results.first().ranges.at(1).start, 4);
    QCOMPARE(results.first().ranges.at(2).start, 12);
    QCOMPARE(results.first().snippetRanges.size(), 3);
  }

  void syncPicksUpExternalChangesAndDeletes() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString root = temp.filePath("documents");
    const QString path = writeDocument(
        root, QStringLiteral("External.templnote"), QStringLiteral("External"),
        {QStringLiteral("originalword")});
    QVERIFY(!path.isEmpty());

    SearchIndex index(root, temp.filePath("search.sqlite"));
    waitForInitialIndex(index);
    QCOMPARE(index.search(QStringLiteral("originalword")).size(), 1);

    writeDocument(root, QStringLiteral("External.templnote"),
                  QStringLiteral("External"),
                  {QStringLiteral("changedword and longer text")});

    index.syncIncremental();

    QCOMPARE(index.search(QStringLiteral("originalword")).size(), 0);
    QCOMPARE(index.search(QStringLiteral("changedword")).size(), 1);

    QVERIFY(QFile::remove(path));
    index.syncIncremental();

    QCOMPARE(index.search(QStringLiteral("changedword")).size(), 0);
  }

  void liveBlockChangesAreDebounced() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString root = temp.filePath("documents");
    QVERIFY(QDir().mkpath(root));

    BlockModel model;
    const QString path = QDir(root).filePath(QStringLiteral("Live.templnote"));
    QVERIFY(model.createDocument(path));

    SearchIndex index(root, temp.filePath("search.sqlite"));
    waitForInitialIndex(index);
    index.setBlockModel(&model);

    const QString firstId =
        model.data(model.index(0, 0), BlockModel::BlockIdRole).toString();
    QVERIFY(!firstId.isEmpty());

    QVERIFY(model.updateBlockText(firstId,
                                  QStringLiteral("livezebra first")));
    QTRY_COMPARE_WITH_TIMEOUT(index.search(QStringLiteral("livezebra")).size(),
                              1, 3000);

    const QString inserted =
        model.appendBlock(static_cast<int>(BlockType::Paragraph),
                          QStringLiteral("insertpanda text"));
    QVERIFY(!inserted.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(
        index.search(QStringLiteral("insertpanda")).size(), 1, 3000);

    QVERIFY(model.updateBlockText(firstId,
                                  QStringLiteral("livezebra second")));
    QTRY_VERIFY_WITH_TIMEOUT(
        !index.search(QStringLiteral("livezebra")).isEmpty() &&
            index.search(QStringLiteral("livezebra")).first().blockText ==
                QStringLiteral("livezebra second"),
        3000);

    QVERIFY(model.removeBlock(firstId));
    QTRY_COMPARE_WITH_TIMEOUT(index.search(QStringLiteral("livezebra")).size(),
                              0, 3000);
    QCOMPARE(index.search(QStringLiteral("insertpanda")).size(), 1);
  }

  void capsLargeResultSets() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString root = temp.filePath("documents");
    QVector<QString> texts;

    for (int i = 0; i < 205; ++i)
      texts.append(QStringLiteral("capblock %1").arg(i));

    QVERIFY(!writeDocument(root, QStringLiteral("Many.templnote"),
                           QStringLiteral("Many"), texts).isEmpty());

    SearchIndex index(root, temp.filePath("search.sqlite"));
    waitForInitialIndex(index);

    bool truncated = false;
    const auto results = index.search(QStringLiteral("capblock"), &truncated);
    QCOMPARE(results.size(), 200);
    QVERIFY(truncated);
  }

  void renamesAndDeletesUpdateIndex() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString root = temp.filePath("documents");
    const QString path = writeDocument(
        root, QStringLiteral("Old.templnote"), QStringLiteral("Old"),
        {QStringLiteral("renametoken body")});
    QVERIFY(!path.isEmpty());

    DocumentListModel documents(root);
    SearchIndex index(root, temp.filePath("search.sqlite"));
    waitForInitialIndex(index);
    index.setDocumentList(&documents);

    QCOMPARE(index.search(QStringLiteral("renametoken")).size(), 1);

    const QString renamed =
        documents.renameDocument(path, QStringLiteral("Renamed"));
    QVERIFY(!renamed.isEmpty());
    QVERIFY(renamed != path);

    QTRY_COMPARE_WITH_TIMEOUT(
        index.search(QStringLiteral("renametoken")).first().documentPath,
        renamed, 3000);
    QCOMPARE(
        index.search(QStringLiteral("renametoken")).first().documentTitle,
        QStringLiteral("Renamed"));

    QVERIFY(documents.deleteEntry(renamed));
    QTRY_COMPARE_WITH_TIMEOUT(
        index.search(QStringLiteral("renametoken")).size(), 0, 3000);
  }

  void corruptDatabaseIsRebuilt() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString root = temp.filePath("documents");
    const QString path = writeDocument(
        root, QStringLiteral("Recover.templnote"), QStringLiteral("Recover"),
        {QStringLiteral("recovertoken")});
    QVERIFY(!path.isEmpty());

    const QString databasePath = temp.filePath("search.sqlite");
    QFile database(databasePath);
    QVERIFY(database.open(QIODevice::WriteOnly | QIODevice::Truncate));
    database.write("not a sqlite database");
    database.close();

    SearchIndex index(root, databasePath);
    waitForInitialIndex(index);

    const auto results = index.search(QStringLiteral("recovertoken"));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().documentPath, path);
  }

  void resultsModelExposesRolesAndMaps() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString root = temp.filePath("documents");
    const QString path = writeDocument(
        root, QStringLiteral("Model.templnote"), QStringLiteral("Model"),
        {QStringLiteral("modelneedle one"), QStringLiteral("modelneedle two")});
    QVERIFY(!path.isEmpty());

    SearchIndex index(root, temp.filePath("search.sqlite"));
    waitForInitialIndex(index);
    SearchResultsModel model;
    model.setIndex(&index);
    model.search(QStringLiteral("modelneedle"));

    QCOMPARE(model.count(), 2);
    QCOMPARE(model.rowCount(), 2);

    const auto roles = model.roleNames();
    QCOMPARE(roles.value(SearchResultsModel::DocumentTitleRole),
             QByteArray("documentTitle"));
    QCOMPARE(roles.value(SearchResultsModel::DocumentPathRole),
             QByteArray("documentPath"));
    QCOMPARE(roles.value(SearchResultsModel::BlockIdRole),
             QByteArray("blockId"));
    QCOMPARE(roles.value(SearchResultsModel::SnippetRole),
             QByteArray("snippet"));
    QCOMPARE(roles.value(SearchResultsModel::RangesRole),
             QByteArray("ranges"));
    QCOMPARE(roles.value(SearchResultsModel::MatchCountRole),
             QByteArray("matchCount"));

    const QModelIndex first = model.QAbstractListModel::index(0, 0);
    QCOMPARE(model.data(first, SearchResultsModel::DocumentPathRole).toString(),
             path);
    QCOMPARE(model.data(first, SearchResultsModel::DocumentTitleRole).toString(),
             QStringLiteral("Model"));
    QVERIFY(!model.data(first, SearchResultsModel::BlockIdRole)
                 .toString()
                 .isEmpty());
    QVERIFY(!model.data(first, SearchResultsModel::SnippetRole)
                 .toString()
                 .isEmpty());
    QCOMPARE(model.data(first, SearchResultsModel::MatchCountRole).toInt(), 1);
    QCOMPARE(model.data(first, SearchResultsModel::DocumentMatchCountRole)
                 .toInt(),
             2);
    QCOMPARE(model.data(first, SearchResultsModel::RangesRole)
                 .toList()
                 .size(),
             1);

    const QVariantMap mapped = model.resultAt(0);
    QCOMPARE(mapped.value(QStringLiteral("documentPath")).toString(), path);
    QCOMPARE(mapped.value(QStringLiteral("documentTitle")).toString(),
             QStringLiteral("Model"));
    QVERIFY(!mapped.value(QStringLiteral("blockId")).toString().isEmpty());
    QCOMPARE(mapped.value(QStringLiteral("matchCount")).toInt(), 1);
    QCOMPARE(mapped.value(QStringLiteral("ranges")).toList().size(), 1);

    QVERIFY(model.groupTitle(path) == QStringLiteral("Model"));
    QCOMPARE(model.groupMatchCount(path), 2);

    model.search(QString());
    QCOMPARE(model.count(), 0);
  }

  void connectionNamesAreUnique() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    SearchIndex first(temp.filePath("first"), temp.filePath("first.sqlite"));
    SearchIndex second(temp.filePath("second"), temp.filePath("second.sqlite"));

    QVERIFY(first.search(QStringLiteral("anything")).isEmpty());
    QVERIFY(second.search(QStringLiteral("anything")).isEmpty());
  }
};

QTEST_GUILESS_MAIN(SearchIndexTests)

#include "tst_searchindex.moc"
