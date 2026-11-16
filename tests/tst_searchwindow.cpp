#include <QAbstractListModel>
#include <QDir>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <utility>

#include "../src/core/Block.h"
#include "../src/model/BlockModel.h"

using namespace Templnote;

namespace {
// Minimal stand-in for SearchResultsModel. The global search window only
// depends on the QML-facing contract, so driving it with fake rows keeps the
// UI test independent of the SQLite index.
class FakeSearchResultsModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
  enum Roles {
    DocumentTitleRole = Qt::UserRole + 1,
    DocumentPathRole,
    BlockIdRole,
    BlockTextRole,
    SnippetRole,
    RangesRole,
    SnippetRangesRole,
    MatchCountRole,
    DocumentMatchCountRole,
    RankRole
  };

  struct Row {
    QString documentTitle;
    QString documentPath;
    QString blockId;
    QString blockText;
    QString snippet;
    QVariantList ranges;
    QVariantList snippetRanges;
    int matchCount = 1;
    int documentMatchCount = 1;
    double rank = 0.0;
  };

  explicit FakeSearchResultsModel(QObject *parent = nullptr)
      : QAbstractListModel(parent) {}

  int count() const { return m_visible.size(); }

  void addRow(const Row &row) { m_all.append(row); }

  int rowCount(const QModelIndex &parent = QModelIndex()) const override {
    return parent.isValid() ? 0 : m_visible.size();
  }

  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override {
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= m_visible.size()) {
      return {};
    }

    const Row &row = m_visible.at(index.row());

    switch (role) {
    case DocumentTitleRole:
      return row.documentTitle;
    case DocumentPathRole:
      return row.documentPath;
    case BlockIdRole:
      return row.blockId;
    case BlockTextRole:
      return row.blockText;
    case SnippetRole:
      return row.snippet;
    case RangesRole:
      return row.ranges;
    case SnippetRangesRole:
      return row.snippetRanges;
    case MatchCountRole:
      return row.matchCount;
    case DocumentMatchCountRole:
      return row.documentMatchCount;
    case RankRole:
      return row.rank;
    case Qt::DisplayRole:
      return row.snippet;
    default:
      return {};
    }
  }

  QHash<int, QByteArray> roleNames() const override {
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

  Q_INVOKABLE void search(const QString &query) {
    beginResetModel();
    m_visible.clear();

    if (!query.isEmpty()) {
      for (const Row &row : std::as_const(m_all)) {
        if (row.blockText.contains(query, Qt::CaseInsensitive) ||
            row.documentTitle.contains(query, Qt::CaseInsensitive)) {
          m_visible.append(row);
        }
      }
    }

    endResetModel();
    emit countChanged();
  }

  Q_INVOKABLE QVariantMap resultAt(int row) const {
    QVariantMap map;

    if (row < 0 || row >= m_visible.size())
      return map;

    const Row &result = m_visible.at(row);
    map.insert(QStringLiteral("documentTitle"), result.documentTitle);
    map.insert(QStringLiteral("documentPath"), result.documentPath);
    map.insert(QStringLiteral("documentRelativePath"),
               QFileInfo(result.documentPath).fileName());
    map.insert(QStringLiteral("blockId"), result.blockId);
    map.insert(QStringLiteral("blockText"), result.blockText);
    map.insert(QStringLiteral("snippet"), result.snippet);
    map.insert(QStringLiteral("ranges"), result.ranges);
    map.insert(QStringLiteral("snippetRanges"), result.snippetRanges);
    map.insert(QStringLiteral("matchCount"), result.matchCount);
    map.insert(QStringLiteral("documentMatchCount"),
               result.documentMatchCount);
    map.insert(QStringLiteral("rank"), result.rank);
    return map;
  }

  Q_INVOKABLE QString groupTitle(const QString &documentPath) const {
    for (const Row &row : m_visible) {
      if (row.documentPath == documentPath)
        return row.documentTitle;
    }

    return documentPath;
  }

  Q_INVOKABLE QString relativeDocumentPath(const QString &documentPath) const {
    return QFileInfo(documentPath).fileName();
  }

  Q_INVOKABLE int groupMatchCount(const QString &documentPath) const {
    for (const Row &row : m_visible) {
      if (row.documentPath == documentPath)
        return row.documentMatchCount;
    }

    return 0;
  }

signals:
  void countChanged();

private:
  QVector<Row> m_all;
  QVector<Row> m_visible;
};
} // namespace

class SearchWindowTests : public QObject {
  Q_OBJECT

private:
  QQmlApplicationEngine *m_engine = nullptr;
  QQuickWindow *m_window = nullptr;
  QQuickItem *m_searchWindow = nullptr;
  QObject *m_editorStub = nullptr;
  FakeSearchResultsModel *m_results = nullptr;

  static FakeSearchResultsModel::Row makeRow(const QString &title,
                                             const QString &path,
                                             const QString &blockId,
                                             const QString &text) {
    FakeSearchResultsModel::Row row;
    row.documentTitle = title;
    row.documentPath = path;
    row.blockId = blockId;
    row.blockText = text;
    row.snippet = text;
    row.ranges = QVariantList{QVariantMap{
        {QStringLiteral("start"), text.indexOf(QStringLiteral("alpha"))},
        {QStringLiteral("length"), 5}}};
    row.snippetRanges = row.ranges;
    row.matchCount = 1;
    row.documentMatchCount = 1;
    return row;
  }

  void openSearch() {
    QVERIFY(QMetaObject::invokeMethod(m_searchWindow, "open"));
    QTRY_VERIFY(m_searchWindow->isVisible());
  }

  int currentIndex() const {
    return m_searchWindow->property("currentIndex").toInt();
  }

  QString query() const {
    return m_searchWindow->property("query").toString();
  }

  void setQuery(const QString &text) {
    QVERIFY(m_searchWindow->setProperty("query", text));
  }

private slots:
  void initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("TemplnoteTest");
    QCoreApplication::setApplicationName("TemplnoteSearchWindowTest");
    qmlRegisterType<BlockModel>("TemplnoteSearchTest", 1, 0, "BlockModel");
  }

  void init() {
    m_engine = new QQmlApplicationEngine;
    m_results = new FakeSearchResultsModel(m_engine);
    m_engine->rootContext()->setContextProperty(QStringLiteral("fakeResults"),
                                                m_results);
    m_engine->loadFromModule("TemplnoteSearchTest", "SearchWindowTest");

    QVERIFY2(!m_engine->rootObjects().isEmpty(),
             "The global search test QML failed to load");

    m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
    QVERIFY(m_window);

    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window));
    m_window->requestActivate();
    QTest::qWait(100);

    m_searchWindow = m_window->findChild<QQuickItem *>(
        QStringLiteral("searchWindow"));
    QVERIFY(m_searchWindow);
    m_editorStub = m_window->findChild<QObject *>(QStringLiteral("editorStub"));
    QVERIFY(m_editorStub);
  }

  void cleanup() {
    delete m_engine;
    m_engine = nullptr;
    m_window = nullptr;
    m_searchWindow = nullptr;
    m_editorStub = nullptr;
    m_results = nullptr;
  }

  void openFocusesQueryFieldAndSearches() {
    m_results->addRow(makeRow(QStringLiteral("One"), QStringLiteral("/one"),
                              QStringLiteral("block-1"),
                              QStringLiteral("alpha one")));
    m_results->addRow(makeRow(QStringLiteral("Two"), QStringLiteral("/two"),
                              QStringLiteral("block-2"),
                              QStringLiteral("alpha two")));

    openSearch();
    QCOMPARE(currentIndex(), -1);

    QQuickItem *focus = m_window->activeFocusItem();
    QVERIFY(focus);
    QCOMPARE(focus->property("placeholderText").toString(),
             QStringLiteral("Search all documents"));

    setQuery(QStringLiteral("alpha"));
    QTRY_COMPARE(m_results->count(), 2);
    QTRY_COMPARE(currentIndex(), 0);

    const QVariantMap result =
        m_searchWindow->property("currentResult").toMap();
    QCOMPARE(result.value(QStringLiteral("documentTitle")).toString(),
             QStringLiteral("One"));
    QCOMPARE(result.value(QStringLiteral("blockId")).toString(),
             QStringLiteral("block-1"));
    QVERIFY(result.value(QStringLiteral("blockText"))
                .toString()
                .contains(QStringLiteral("alpha")));
  }

  void upAndDownChangeSelection() {
    m_results->addRow(makeRow(QStringLiteral("One"), QStringLiteral("/one"),
                              QStringLiteral("block-1"),
                              QStringLiteral("alpha one")));
    m_results->addRow(makeRow(QStringLiteral("Two"), QStringLiteral("/two"),
                              QStringLiteral("block-2"),
                              QStringLiteral("alpha two")));

    openSearch();
    setQuery(QStringLiteral("alpha"));
    QTRY_COMPARE(currentIndex(), 0);

    QTest::keyClick(m_window, Qt::Key_Down);
    QTRY_COMPARE(currentIndex(), 1);
    QCOMPARE(m_searchWindow->property("currentResult")
                 .toMap()
                 .value(QStringLiteral("blockId"))
                 .toString(),
             QStringLiteral("block-2"));

    QTest::keyClick(m_window, Qt::Key_Down);
    QTRY_COMPARE(currentIndex(), 0);

    QTest::keyClick(m_window, Qt::Key_Up);
    QTRY_COMPARE(currentIndex(), 1);
  }

  void escapeClosesAndClearsQuery() {
    m_results->addRow(makeRow(QStringLiteral("One"), QStringLiteral("/one"),
                              QStringLiteral("block-1"),
                              QStringLiteral("alpha one")));

    openSearch();
    setQuery(QStringLiteral("alpha"));
    QTRY_COMPARE(currentIndex(), 0);

    QTest::keyClick(m_window, Qt::Key_Escape);
    QTRY_VERIFY(!m_searchWindow->isVisible());
    QCOMPARE(query(), QString());
    QCOMPARE(m_results->count(), 0);
  }

  void enterOpensDocumentAndHighlightsMatches() {
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString path = temp.filePath(QStringLiteral("Target.templnote"));

    BlockModel writer;
    QVERIFY(writer.createDocument(path));
    const QString blockId =
        writer.data(writer.index(0, 0), BlockModel::BlockIdRole).toString();
    QVERIFY(!blockId.isEmpty());
    QVERIFY(writer.updateBlockText(blockId,
                                   QStringLiteral("alpha body alpha")));
    QVERIFY(writer.saveCurrentDocument());

    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    m_results->addRow(makeRow(QStringLiteral("Target"), absolutePath, blockId,
                              QStringLiteral("alpha body alpha")));

    openSearch();
    setQuery(QStringLiteral("alpha"));
    QTRY_COMPARE(currentIndex(), 0);

    QTest::keyClick(m_window, Qt::Key_Return);
    QTRY_VERIFY(!m_searchWindow->isVisible());

    BlockModel *model = m_window->findChild<BlockModel *>();
    QVERIFY(model);
    QTRY_COMPARE(model->currentFilePath(), absolutePath);

    QTRY_VERIFY(m_editorStub->property("lastMatches").toList().size() >= 1);
    QCOMPARE(m_editorStub->property("lastMatches").toList().size(), 2);
    QVERIFY(m_editorStub->property("lastActive").toInt() >= 0);
    QVERIFY(m_editorStub->property("navigationActive").toBool());

    const QVariantMap focused =
        m_editorStub->property("focusedMatch").toMap();
    QCOMPARE(focused.value(QStringLiteral("blockId")).toString(), blockId);
  }

  void emptyQueryKeepsSelectionEmpty() {
    m_results->addRow(makeRow(QStringLiteral("One"), QStringLiteral("/one"),
                              QStringLiteral("block-1"),
                              QStringLiteral("alpha one")));

    openSearch();
    QCOMPARE(currentIndex(), -1);

    setQuery(QStringLiteral("missing"));
    QTRY_COMPARE(m_results->count(), 0);
    QCOMPARE(currentIndex(), -1);

    QTest::keyClick(m_window, Qt::Key_Down);
    QTest::qWait(50);
    QCOMPARE(currentIndex(), -1);
  }
};

QTEST_MAIN(SearchWindowTests)

#include "tst_searchwindow.moc"
