#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

#include "../src/model/BlockModel.h"

using namespace Templnote;

class EditorQmlTests : public QObject {
  Q_OBJECT

private:
  QQmlApplicationEngine *m_engine = nullptr;
  QQuickWindow *m_window = nullptr;

  static void collectEditors(QQuickItem *item, QList<QQuickItem *> &editors) {
    const QMetaObject *metaObject = item->metaObject();

    if (metaObject->indexOfProperty("cursorPosition") >= 0 &&
        metaObject->indexOfProperty("length") >= 0) {
      editors.append(item);
    }

    const auto children = item->childItems();

    for (QQuickItem *child : children)
      collectEditors(child, editors);
  }

  QList<QQuickItem *> editors() const {
    QList<QQuickItem *> result;
    collectEditors(m_window->contentItem(), result);
    return result;
  }

  static void typeText(QQuickWindow *window, const QString &text) {
    for (const QChar character : text)
      QTest::keyClick(window, character.toLatin1());
  }

  BlockModel *model() const { return m_window->findChild<BlockModel *>(); }

private slots:
  void initTestCase() {
    qmlRegisterType<BlockModel>("Templnote", 1, 0, "BlockModel");
  }

  void init() {
    m_engine = new QQmlApplicationEngine;
    m_engine->loadFromModule("Templnote", "EditorTestWindow");

    QVERIFY2(!m_engine->rootObjects().isEmpty(),
             "The editor test QML failed to load");

    m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().first());
    QVERIFY(m_window);

    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window));

    // The ListView creates its delegates lazily; force one frame before
    // looking for the text editors.
    QTest::qWait(100);
    m_window->grabWindow();
    QTest::qWait(200);

    QVERIFY(model());
  }

  void cleanup() {
    delete m_engine;
    m_engine = nullptr;
    m_window = nullptr;
  }

  void undoMovesFocusAcrossBlocks() {
    auto textEditors = editors();
    QCOMPARE(textEditors.size(), 2);

    textEditors[0]->forceActiveFocus();
    textEditors[0]->setProperty("cursorPosition",
                                textEditors[0]->property("length"));
    QTest::qWait(30);
    typeText(m_window, "AAA");
    QTest::qWait(50);

    QVERIFY(textEditors[0]->property("text").toString().endsWith("AAA"));

    const QPoint click =
        textEditors[1]->mapToScene(QPointF(20, 10)).toPoint();
    QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, click);
    QTest::qWait(30);
    typeText(m_window, "BBB");
    QTest::qWait(50);

    QTest::keyClick(m_window, Qt::Key_Z, Qt::ControlModifier);
    QTest::qWait(150);

    textEditors = editors();
    QVERIFY(textEditors.size() >= 2);
    QCOMPARE(m_window->activeFocusItem(), textEditors[1]);
    QVERIFY(textEditors[1]->property("cursorVisible").toBool());
    QCOMPARE(textEditors[1]->property("text").toString(),
             model()->document().blockAt(1)->text);
    QVERIFY(!textEditors[1]->property("text").toString().contains("BBB"));

    QTest::keyClick(m_window, Qt::Key_Z, Qt::ControlModifier);
    QTest::qWait(150);

    QCOMPARE(m_window->activeFocusItem(), textEditors[0]);
    QVERIFY(textEditors[0]->property("cursorVisible").toBool());
    QCOMPARE(textEditors[0]->property("text").toString(),
             model()->document().blockAt(0)->text);

    QTest::keyClick(m_window, Qt::Key_Z,
                    Qt::ControlModifier | Qt::ShiftModifier);
    QTest::qWait(150);

    QCOMPARE(m_window->activeFocusItem(), textEditors[0]);
    QVERIFY(textEditors[0]->property("text").toString().endsWith("AAA"));

    QTest::keyClick(m_window, Qt::Key_Z,
                    Qt::ControlModifier | Qt::ShiftModifier);
    QTest::qWait(150);

    textEditors = editors();
    QCOMPARE(m_window->activeFocusItem(), textEditors[1]);
    QVERIFY(textEditors[1]->property("text").toString().contains("BBB"));
  }

  void caretRestoresToEditPosition() {
    auto textEditors = editors();
    QVERIFY(textEditors.size() >= 1);

    QQuickItem *first = textEditors[0];
    first->forceActiveFocus();
    first->setProperty("cursorPosition", 5);
    QTest::qWait(30);

    typeText(m_window, "XY");
    QTest::qWait(50);
    QVERIFY(first->property("text").toString().contains("XY"));

    QTest::keyClick(m_window, Qt::Key_Z, Qt::ControlModifier);
    QTest::qWait(150);

    textEditors = editors();
    QCOMPARE(textEditors[0]->property("cursorPosition").toInt(), 5);

    QTest::keyClick(m_window, Qt::Key_Z,
                    Qt::ControlModifier | Qt::ShiftModifier);
    QTest::qWait(150);

    textEditors = editors();
    QCOMPARE(textEditors[0]->property("cursorPosition").toInt(), 7);
  }

  void clickingBlockPaddingKeepsCursorVisible() {
    auto textEditors = editors();
    QVERIFY(textEditors.size() >= 2);

    textEditors[0]->forceActiveFocus();
    QTest::qWait(30);
    QVERIFY(textEditors[0]->property("cursorVisible").toBool());

    // Click inside the second block's left padding rather than its TextArea.
    const QPoint click =
        textEditors[1]->mapToScene(QPointF(-20, 10)).toPoint();
    QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, click);
    QTest::qWait(100);

    textEditors = editors();
    QCOMPARE(m_window->activeFocusItem(), textEditors[1]);
    QVERIFY(textEditors[1]->property("cursorVisible").toBool());
  }

  void structuralUndoKeepsCursorVisible() {
    auto textEditors = editors();
    QVERIFY(textEditors.size() >= 1);

    textEditors[0]->forceActiveFocus();
    textEditors[0]->setProperty("cursorPosition",
                                textEditors[0]->property("length"));
    QTest::qWait(30);

    QTest::keyClick(m_window, Qt::Key_Return);
    QTest::qWait(200);

    textEditors = editors();
    QCOMPARE(textEditors.size(), 3);
    QCOMPARE(m_window->activeFocusItem(), textEditors[2]);
    QVERIFY(textEditors[2]->property("cursorVisible").toBool());

    QTest::keyClick(m_window, Qt::Key_Z, Qt::ControlModifier);
    QTest::qWait(200);

    textEditors = editors();
    QCOMPARE(textEditors.size(), 2);
    QCOMPARE(m_window->activeFocusItem(), textEditors[0]);
    QVERIFY(textEditors[0]->property("cursorVisible").toBool());

    QTest::keyClick(m_window, Qt::Key_Z,
                    Qt::ControlModifier | Qt::ShiftModifier);
    QTest::qWait(200);

    textEditors = editors();
    QCOMPARE(textEditors.size(), 3);
    QCOMPARE(m_window->activeFocusItem(), textEditors[2]);
    QVERIFY(textEditors[2]->property("cursorVisible").toBool());
  }

  void documentLoadedFocusesFirstBlock() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString docPath = tempDir.filePath("sample.templnote");

    BlockModel tempModel;
    QVERIFY(tempModel.createDocument(docPath));
    const QString id0 = tempModel.data(tempModel.index(0, 0), BlockModel::BlockIdRole).toString();
    tempModel.updateBlockText(id0, "First block content");
    const QString id1 = tempModel.appendBlock(static_cast<int>(BlockType::Paragraph));
    tempModel.updateBlockText(id1, "Second block content");
    QVERIFY(tempModel.saveCurrentDocument());

    // Load the document into the window's editor model
    QVERIFY(model()->loadDocument(docPath));

    // Allow ListView to materialize delegate and process focus
    QTest::qWait(300);

    const auto textEditors = editors();
    QVERIFY(textEditors.size() >= 1);
    QCOMPARE(m_window->activeFocusItem(), textEditors[0]);
    QCOMPARE(textEditors[0]->property("cursorPosition").toInt(), 0);
    QVERIFY(textEditors[0]->property("cursorVisible").toBool());
  }
};

QTEST_MAIN(EditorQmlTests)

#include "tst_editor.moc"
