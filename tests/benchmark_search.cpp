#include <QtTest>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <memory>

#include "../src/core/Block.h"
#include "../src/core/Document.h"
#include "../src/search/SearchIndex.h"
#include "../src/storage/DocumentListModel.h"

using namespace Templnote;

class BenchmarkSearch : public QObject {
  Q_OBJECT

  std::unique_ptr<QTemporaryDir> m_temp;
  std::unique_ptr<DocumentListModel> m_documents;
  std::unique_ptr<SearchIndex> m_index;

  static QString createLargeDocument(const QString &root, int blockCount) {
    const QString path = QDir(root).filePath("Large.templnote");
    Document document;

    for (int i = 0; i < blockCount; ++i) {
      const QString text =
          QStringLiteral("sample benchmark token%1 lorem ipsum dolor sit amet")
              .arg(i % 37);
      document.appendBlock(Block::create(BlockType::Paragraph, text));
    }

    QJsonObject json = document.toJson();
    json.insert(QStringLiteral("title"), QStringLiteral("Large"));
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

private slots:
  void initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("TemplnoteSearchBench");
    QCoreApplication::setApplicationName("TemplnoteSearchBench");
  }

  void init() {
    m_temp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_temp->isValid());
    const QString root = m_temp->filePath("documents");
    QVERIFY(QDir().mkpath(root));
    QVERIFY(!createLargeDocument(root, 10000).isEmpty());

    m_documents = std::make_unique<DocumentListModel>(root);
    m_index = std::make_unique<SearchIndex>(root,
                                            m_temp->filePath("search.sqlite"));
    m_index->setDocumentList(m_documents.get());
    QTRY_VERIFY_WITH_TIMEOUT(
        !m_index->search(QStringLiteral("sample")).isEmpty(), 10000);
  }

  void cleanup() {
    m_index.reset();
    m_documents.reset();
    m_temp.reset();
  }

  void commonPrefixQuery() {
    QBENCHMARK { (void)m_index->search(QStringLiteral("sample")); }
  }

  void selectivePrefixQuery() {
    QBENCHMARK { (void)m_index->search(QStringLiteral("token3")); }
  }

  void repeatedCommonPrefixQuery() {
    (void)m_index->search(QStringLiteral("sample"));
    QBENCHMARK { (void)m_index->search(QStringLiteral("sample")); }
  }

  // Each iteration uses a distinct key, so every reply comes from FTS5 on a
  // reader connection rather than from the cache. Before connection reuse this
  // opened and closed a SQLite connection per keystroke.
  void asyncReaderConnectionReuseManual() {
    constexpr int iterations = 20;
    QSignalSpy spy(m_index.get(), &SearchIndex::searchFinished);

    QElapsedTimer timer;
    timer.start();

    int received = 0;
    for (int i = 0; i < iterations; ++i) {
      const quint64 token = quint64(i + 1);
      m_index->searchAsync(QStringLiteral("sample%1").arg(i), token);
      QTRY_VERIFY_WITH_TIMEOUT(spy.count() > received, 5000);
      ++received;
    }

    qInfo().noquote()
        << QStringLiteral("ASYNC_CONNECTION_REUSE avg=%1 ms")
               .arg(timer.nsecsElapsed() / iterations / 1e6, 0, 'f', 3);
  }

  // A generation change used to clear all 24 cache entries. Entries are now
  // generation-tagged and kept, so the first query after indexUpdated() pays
  // for FTS5 again while later repeats hit the retained cache.
  void cacheGenerationManual() {
    constexpr int iterations = 20;
    QElapsedTimer timer;

    (void)m_index->search(QStringLiteral("sample"));
    timer.start();
    for (int i = 0; i < iterations; ++i)
      (void)m_index->search(QStringLiteral("sample"));
    const double warmMs = timer.nsecsElapsed() / iterations / 1e6;

    timer.start();
    for (int i = 0; i < iterations; ++i) {
      m_index->syncIncremental();
      (void)m_index->search(QStringLiteral("sample"));
    }

    qInfo().noquote()
        << QStringLiteral("CACHE_GENERATION warm=%1 ms invalidated=%2 ms")
               .arg(warmMs, 0, 'f', 4)
               .arg(timer.nsecsElapsed() / iterations / 1e6, 0, 'f', 3);
  }

  // Measures only the GUI-thread cost of dispatching an async search; the
  // FTS5 query and highlighting run on a worker pool.
  void asyncDispatchManual() {
    constexpr int iterations = 25;
    QElapsedTimer timer;
    timer.start();

    for (int i = 0; i < iterations; ++i)
      m_index->searchAsync(QStringLiteral("sample"), quint64(i));

    qInfo().noquote()
        << QStringLiteral("ASYNC_DISPATCH avg=%1 ms")
               .arg(timer.nsecsElapsed() / iterations / 1e6, 0, 'f', 4);
  }

  // A fresh SearchIndex has an empty result cache, so this measures the cold
  // FTS5 + highlight path rather than the cached typing/backspacing path.
  void coldQueryManual() {
    constexpr int iterations = 50;
    qint64 totalNs = 0;
    QElapsedTimer timer;

    for (int i = 0; i < iterations; ++i) {
      SearchIndex index(m_temp->filePath("documents"),
                        m_temp->filePath("search.sqlite"));

      // Startup indexing is queued with QTimer::singleShot(0). Without
      // spinning the event loop the index stays empty and this measures a
      // cold query against an empty database.
      QSignalSpy indexed(&index, &SearchIndex::indexUpdated);

      if (!indexed.wait(10000))
        qWarning() << "COLD_QUERY: initial index did not finish";

      timer.start();
      (void)index.search(QStringLiteral("sample"));
      totalNs += timer.nsecsElapsed();
    }

    qInfo().noquote() << QStringLiteral("COLD_QUERY avg=%1 ms")
                             .arg(totalNs / iterations / 1e6, 0, 'f', 3);
  }
};

QTEST_MAIN(BenchmarkSearch)
#include "benchmark_search.moc"
