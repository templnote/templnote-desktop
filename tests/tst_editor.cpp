#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QtTest>
#include <qtestwheel.h>

#include <algorithm>
#include <functional>

#include "../src/core/CodeHighlighter.h"
#include "../src/model/BlockModel.h"

using namespace Templnote;

static int g_cursorWarnings = 0;
static QtMessageHandler g_previousHandler = nullptr;
static void cursorWarningHandler(QtMsgType type, const QMessageLogContext &context,
                                 const QString &message) {
  if (type == QtWarningMsg && message.contains(QStringLiteral("setPosition")))
    ++g_cursorWarnings;
  if (g_previousHandler)
    g_previousHandler(type, context, message);
}

class EditorQmlTests : public QObject {
  Q_OBJECT

private:
  QQmlApplicationEngine *m_engine = nullptr;
  QQuickWindow *m_window = nullptr;

  static void collectEditors(QQuickItem *item, QList<QQuickItem *> &editors) {
    const QMetaObject *metaObject = item->metaObject();

    if (item->isVisible() &&
        metaObject->indexOfProperty("cursorPosition") >= 0 &&
        metaObject->indexOfProperty("length") >= 0 &&
        metaObject->indexOfProperty("wrapMode") >= 0 &&
        metaObject->indexOfProperty("background") >= 0) {
      editors.append(item);
    }

    const auto children = item->childItems();

    for (QQuickItem *child : children)
      collectEditors(child, editors);
  }

  QList<QQuickItem *> editors() const {
    QList<QQuickItem *> collected;
    collectEditors(m_window->contentItem(), collected);

    // Recycled delegates can still be parented/visible for a moment after a
    // block is removed. Ignore editors whose delegate index is no longer in
    // the model so tests only inspect live blocks.
    QList<QQuickItem *> result;
    const int count = model() ? model()->rowCount() : -1;

    for (QQuickItem *item : collected) {
      QQuickItem *parent = item->parentItem();
      int index = -1;

      while (parent) {
        const QMetaObject *metaObject = parent->metaObject();

        if (metaObject->indexOfProperty("index") >= 0) {
          index = parent->property("index").toInt();
          break;
        }

        parent = parent->parentItem();
      }

      if (index >= 0 && (count < 0 || index < count))
        result.append(item);
    }

    return result;
  }

  static void collectDelegates(QQuickItem *item,
                               QList<QQuickItem *> &delegates) {
    const QMetaObject *metaObject = item->metaObject();

    if (metaObject->indexOfProperty("index") >= 0 &&
        metaObject->indexOfProperty("blockId") >= 0) {
      delegates.append(item);
    }

    const auto children = item->childItems();

    for (QQuickItem *child : children)
      collectDelegates(child, delegates);
  }

  QList<QQuickItem *> listDelegates() const {
    QList<QQuickItem *> result;
    collectDelegates(m_window->contentItem(), result);
    std::sort(result.begin(), result.end(),
              [](QQuickItem *left, QQuickItem *right) {
                return left->property("index").toInt()
                     < right->property("index").toInt();
              });
    return result;
  }

  static void typeText(QQuickWindow *window, const QString &text) {
    for (const QChar character : text)
      QTest::keyClick(window, character.toLatin1());
  }

  BlockModel *model() const { return m_window->findChild<BlockModel *>(); }

  static QString blockIdForItem(QQuickItem *item) {
    for (QQuickItem *candidate = item; candidate;
         candidate = candidate->parentItem()) {
      if (candidate->metaObject()->indexOfProperty("blockId") >= 0 &&
          candidate->metaObject()->indexOfProperty("index") >= 0) {
        return candidate->property("blockId").toString();
      }
    }

    return {};
  }

  QQuickItem *editorForBlock(const QString &blockId) const {
    for (QQuickItem *editor : editors()) {
      if (blockIdForItem(editor) == blockId)
        return editor;
    }

    return nullptr;
  }

  QString focusedBlockId() const {
    return blockIdForItem(m_window->activeFocusItem());
  }

  QQuickItem *editorItem() const {
    return m_window->findChild<QQuickItem *>(QStringLiteral("editor"));
  }

  static QVariantMap matchAt(const QString &blockId, int start, int length) {
    QVariantMap match;
    match.insert(QStringLiteral("blockId"), blockId);
    match.insert(QStringLiteral("start"), start);
    match.insert(QStringLiteral("length"), length);
    return match;
  }

  // Puts the editor into the state the global search window leaves behind
  // after Enter: matches highlighted, first match active and focused.
  QVariantList beginSearchNavigation(const QString &firstId,
                                     const QString &secondId) {
    QVariantList matches;
    matches.append(matchAt(firstId, 0, 5));
    matches.append(matchAt(firstId, 10, 5));
    matches.append(matchAt(secondId, 0, 5));

    QQuickItem *editor = editorItem();
    if (!editor)
      return {};

    if (!QMetaObject::invokeMethod(editor, "setSearchMatches",
                                   Q_ARG(QVariant, matches),
                                   Q_ARG(QVariant, QVariant(0)))) {
      return {};
    }

    if (!QMetaObject::invokeMethod(editor, "setSearchNavigationActive",
                                   Q_ARG(QVariant, QVariant(true)))) {
      return {};
    }

    if (!QMetaObject::invokeMethod(editor, "focusSearchMatch",
                                   Q_ARG(QVariant, matches.at(0)))) {
      return {};
    }

    return matches;
  }

private slots:
  void initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("TemplnoteTest");
    QCoreApplication::setApplicationName("TemplnoteEditorTest");
    qmlRegisterType<BlockModel>("Templnote", 1, 0, "BlockModel");
    qmlRegisterType<CodeHighlighter>("Templnote", 1, 0, "CodeHighlighter");
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

  void backspaceOnEmptyBlockFocusesPreviousBlock() {
    BlockModel *blockModel = model();
    const QString firstId = blockModel->blockIdAt(0);
    const QString secondId = blockModel->blockIdAt(1);
    QVERIFY(blockModel->updateBlockText(secondId, ""));
    QTest::qWait(150);

    QQuickItem *editor = editorForBlock(secondId);
    QVERIFY(editor);
    editor->forceActiveFocus();
    editor->setProperty("cursorPosition", 0);
    QTest::qWait(50);

    QTest::keyClick(m_window, Qt::Key_Backspace);
    QTest::qWait(250);

    QCOMPARE(blockModel->rowCount(), 1);
    QCOMPARE(blockModel->blockIdAt(0), firstId);

    QObject *editorObject = m_window->findChild<QObject *>("editor");
    QVERIFY(editorObject);
    QCOMPARE(editorObject->property("caretBlockId").toString(), firstId);
  }

  void backspaceAfterLayoutFocusesPreviousEditableBlock() {
    BlockModel *blockModel = model();
    blockModel->createColumnsLayout(1, 2);

    const QString layoutId = blockModel->blockIdAt(1);
    const QStringList columns = blockModel->columnIdsOf(layoutId);
    const QString lastChild = blockModel->blocksOfColumn(columns.last()).last();

    // The block after a layout is preceded in the view by the Columns wrapper,
    // which cannot host a caret. Backspace must skip it and focus the last
    // editable block inside the layout instead of losing the caret.
    const QString emptyId = blockModel->appendBlock(0, "");
    QTest::qWait(250);

    QQuickItem *editor = editorForBlock(emptyId);
    QVERIFY(editor);
    editor->forceActiveFocus();
    editor->setProperty("cursorPosition", 0);
    QTest::qWait(50);

    QTest::keyClick(m_window, Qt::Key_Backspace);
    QTest::qWait(300);

    QCOMPARE(blockModel->indexOfBlock(emptyId), -1);

    QObject *editorObject = m_window->findChild<QObject *>("editor");
    QVERIFY(editorObject);
    QCOMPARE(editorObject->property("caretBlockId").toString(), lastChild);
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

  void multilineBlocksKeepTheirHeightBeforeFocusChanges() {
    BlockModel *blockModel = model();
    const QString secondId =
        blockModel->data(blockModel->index(1, 0), BlockModel::BlockIdRole)
            .toString();

    // Leave the first (focused) block short and make the second, unfocused
    // block multiline. Its delegate must still reserve room for every line
    // before the user ever focuses it.
    QVERIFY(blockModel->updateBlockText(secondId, "one\ntwo\nthree\nfour\nfive"));
    const QString thirdId =
        blockModel->appendBlock(static_cast<int>(BlockType::Paragraph));
    QVERIFY(!thirdId.isEmpty());
    QVERIFY(blockModel->updateBlockText(thirdId, "after"));

    QTest::qWait(300);

    const auto delegates = listDelegates();
    QVERIFY(delegates.size() >= 3);

    QQuickItem *second = delegates[1];
    QQuickItem *third = delegates[2];

    QVERIFY2(second->height() > 80,
             qPrintable(QStringLiteral("Second block height was only %1")
                            .arg(second->height())));
    QVERIFY2(third->y() >= second->y() + second->height() - 0.5,
             "The following block overlaps the multiline block");
  }

  void globalSearchNavigationCyclesMatchesAndEscapeDismisses() {
    BlockModel *blockModel = model();
    const QString firstId =
        blockModel->data(blockModel->index(0, 0), BlockModel::BlockIdRole)
            .toString();
    const QString secondId =
        blockModel->data(blockModel->index(1, 0), BlockModel::BlockIdRole)
            .toString();
    QVERIFY(blockModel->updateBlockText(firstId, "alpha one alpha"));
    QVERIFY(blockModel->updateBlockText(secondId, "alpha two"));
    QTest::qWait(100);

    QQuickItem *editor = editorItem();
    QVERIFY(editor);
    QVERIFY(!beginSearchNavigation(firstId, secondId).isEmpty());

    QTRY_COMPARE(editor->property("searchNavigationActive").toBool(), true);
    QTRY_COMPARE(editor->property("activeSearchMatch").toInt(), 0);
    QTRY_COMPARE(m_window->activeFocusItem(), editors()[0]);

    QTest::keyClick(m_window, Qt::Key_Down);
    QTRY_COMPARE(editor->property("activeSearchMatch").toInt(), 1);
    QCOMPARE(m_window->activeFocusItem(), editors()[0]);

    QTest::keyClick(m_window, Qt::Key_Down);
    QTRY_COMPARE(editor->property("activeSearchMatch").toInt(), 2);
    QTRY_COMPARE(m_window->activeFocusItem(), editors()[1]);

    // Wraps back to the first match, like the local find bar.
    QTest::keyClick(m_window, Qt::Key_Down);
    QTRY_COMPARE(editor->property("activeSearchMatch").toInt(), 0);
    QTRY_COMPARE(m_window->activeFocusItem(), editors()[0]);

    QTest::keyClick(m_window, Qt::Key_Up);
    QTRY_COMPARE(editor->property("activeSearchMatch").toInt(), 2);
    QTRY_COMPARE(m_window->activeFocusItem(), editors()[1]);

    QQuickItem *focusedEditor = m_window->activeFocusItem();
    const int caretBefore = focusedEditor->property("cursorPosition").toInt();
    const QString selectedBefore =
        focusedEditor->property("selectedText").toString();
    const QString textBefore = focusedEditor->property("text").toString();
    QCOMPARE(selectedBefore, QStringLiteral("alpha"));

    QTest::keyClick(m_window, Qt::Key_Escape);
    QTRY_COMPARE(editor->property("searchNavigationActive").toBool(), false);
    QCOMPARE(editor->property("searchMatches").toList().size(), 0);
    QCOMPARE(editor->property("activeSearchMatch").toInt(), -1);

    // Escape removes the highlights and the match selection, but keeps the
    // caret and text in place.
    QCOMPARE(focusedEditor->property("selectedText").toString(), QString());
    QCOMPARE(focusedEditor->property("cursorPosition").toInt(), caretBefore);
    QCOMPARE(focusedEditor->property("text").toString(), textBefore);
  }

  void globalSearchNavigationEndsOnEdit() {
    BlockModel *blockModel = model();
    const QString firstId =
        blockModel->data(blockModel->index(0, 0), BlockModel::BlockIdRole)
            .toString();
    const QString secondId =
        blockModel->data(blockModel->index(1, 0), BlockModel::BlockIdRole)
            .toString();
    QVERIFY(blockModel->updateBlockText(firstId, "alpha one alpha"));
    QVERIFY(blockModel->updateBlockText(secondId, "alpha two"));
    QTest::qWait(100);

    QQuickItem *editor = editorItem();
    QVERIFY(editor);
    QVERIFY(!beginSearchNavigation(firstId, secondId).isEmpty());
    QTRY_COMPARE(editor->property("searchNavigationActive").toBool(), true);
    QTRY_COMPARE(m_window->activeFocusItem(), editors()[0]);

    // The first match is selected; typing replaces it and ends navigation.
    typeText(m_window, "x");
    QTRY_COMPARE(editor->property("searchNavigationActive").toBool(), false);
    QCOMPARE(editor->property("searchMatches").toList().size(), 0);
    QCOMPARE(blockModel->document().blockAt(0)->text,
             QStringLiteral("x one alpha"));
  }

  void unfocusedMarkdownSearchOffsetsMapToRenderedText() {
    BlockModel *blockModel = model();
    const QString firstId =
        blockModel->data(blockModel->index(0, 0), BlockModel::BlockIdRole)
            .toString();
    QVERIFY(blockModel->updateBlockText(firstId, "**bold** plain"));
    QTest::qWait(100);

    QQuickItem *block = nullptr;
    const auto delegates = listDelegates();

    std::function<QQuickItem *(QQuickItem *)> findBlock =
        [&findBlock](QQuickItem *item) -> QQuickItem * {
      if (item->metaObject()->indexOfMethod(
              "rawToRenderedIndex(QVariant,QVariant)") >= 0)
        return item;

      for (QQuickItem *child : item->childItems()) {
        if (QQuickItem *found = findBlock(child))
          return found;
      }

      return nullptr;
    };

    for (QQuickItem *delegate : delegates) {
      block = findBlock(delegate);

      if (block)
        break;
    }

    QVERIFY(block);

    const auto mapIndex = [block](const QString &text, int index) {
      QVariant result;
      const bool ok = QMetaObject::invokeMethod(
          block, "rawToRenderedIndex", Q_RETURN_ARG(QVariant, result),
          Q_ARG(QVariant, text), Q_ARG(QVariant, index));
      return ok ? result.toInt() : -1;
    };

    // Unfocused blocks hide ** markers, so raw offsets shift left by four.
    QCOMPARE(mapIndex(QStringLiteral("**bold** plain"), 0), 0);
    QCOMPARE(mapIndex(QStringLiteral("**bold** plain"), 2), 0);
    QCOMPARE(mapIndex(QStringLiteral("**bold** plain"), 6), 4);
    QCOMPARE(mapIndex(QStringLiteral("**bold** plain"), 10), 6);
    QCOMPARE(mapIndex(QStringLiteral("**bold** plain"), 14), 10);

    // Unmatched inline tags stay literal, so only matched pairs are removed.
    QCOMPARE(mapIndex(QStringLiteral("<u>ok</u> <u>bad"), 0), 0);
    QCOMPARE(mapIndex(QStringLiteral("<u>ok</u> <u>bad"), 3), 0);
    QCOMPARE(mapIndex(QStringLiteral("<u>ok</u> <u>bad"), 9), 2);
    QCOMPARE(mapIndex(QStringLiteral("<u>ok</u> <u>bad"), 13), 6);
    QCOMPARE(mapIndex(QStringLiteral("<u>ok</u> <u>bad"), 16), 9);
  }

  void wheelScrollingStopsWhileOverlaysAreOpen() {
    BlockModel *blockModel = model();

    for (int i = 0; i < 30; ++i) {
      QVERIFY(!blockModel->appendBlock(static_cast<int>(BlockType::Paragraph),
                                       QStringLiteral("scroll line %1").arg(i))
                   .isEmpty());
    }

    QTest::qWait(300);

    QQuickItem *view = m_window->findChild<QQuickItem *>("documentView");
    QVERIFY(view);

    const QPoint center =
        view->mapToScene(QPointF(view->width() / 2, view->height() / 2))
            .toPoint();
    const auto scrollOnce = [&]() {
      QTest::wheelEvent(m_window, center, QPoint(0, -120));
      QTest::qWait(250);
    };

    // Baseline: an unobstructed editor scrolls with the wheel.
    view->setProperty("contentY", 0);
    QTest::qWait(50);
    scrollOnce();
    QVERIFY(view->property("contentY").toReal() > 0);

    QQuickItem *editor = editorItem();
    QVERIFY(editor);

    // A host surface covering the editor (settings, global search, dialogs)
    // disables background scrolling.
    view->setProperty("contentY", 0);
    editor->setProperty("scrollingEnabled", false);
    QTest::qWait(50);
    scrollOnce();
    QCOMPARE(view->property("contentY").toReal(), 0.0);
    editor->setProperty("scrollingEnabled", true);
    QTest::qWait(50);

    // The selection toolbar is open.
    QQuickItem *toolbar =
        m_window->findChild<QQuickItem *>("selectionToolbar");
    QVERIFY(toolbar);
    view->setProperty("contentY", 0);
    toolbar->setProperty("showToolbar", true);
    QTest::qWait(50);
    scrollOnce();
    QCOMPARE(view->property("contentY").toReal(), 0.0);
    toolbar->setProperty("showToolbar", false);
  }

  void ctrlEnterMiddleCodeBlockFocusesNewBlock() {
    BlockModel *blockModel = model();

    for (int i = 0; i < 3; ++i) {
      QVERIFY(!blockModel->appendBlock(static_cast<int>(BlockType::Paragraph),
                                       QStringLiteral("after %1").arg(i))
                   .isEmpty());
    }

    const QString codeId = blockModel->blockIdAt(2);
    QVERIFY(!codeId.isEmpty());
    const QString afterId = blockModel->blockIdAt(3);
    QVERIFY(blockModel->changeBlockType(codeId, 6, 0));
    QVERIFY(blockModel->updateBlockText(codeId, "middle();"));
    QTest::qWait(300);

    QQuickItem *codeEditor = editorForBlock(codeId);
    QVERIFY(codeEditor);
    codeEditor->forceActiveFocus();
    QTest::qWait(100);
    QCOMPARE(focusedBlockId(), codeId);

    QTest::keyClick(m_window, Qt::Key_Return, Qt::ControlModifier);
    QTest::qWait(300);

    QCOMPARE(blockModel->rowCount(), 6);
    QCOMPARE(focusedBlockId(), blockModel->blockIdAt(3));
    QVERIFY(blockModel->blockIdAt(3) != afterId);
    QCOMPARE(blockModel->blockIdAt(4), afterId);
  }

  void ctrlEnterCodeBlockNearDocumentEndFocusesNewBlock() {
    BlockModel *blockModel = model();

    for (int i = 0; i < 60; ++i) {
      QVERIFY(!blockModel->appendBlock(static_cast<int>(BlockType::Paragraph),
                                       QStringLiteral("filler %1").arg(i))
                   .isEmpty());
    }

    QQuickItem *view = m_window->findChild<QQuickItem *>("documentView");
    QVERIFY(view);
    QTest::qWait(300);
    view->setProperty("contentY",
                      view->property("contentHeight").toReal()
                          - view->height());
    QTest::qWait(300);

    const int codeRow = blockModel->rowCount() - 1;
    const QString codeId = blockModel->blockIdAt(codeRow);
    QVERIFY(blockModel->changeBlockType(codeId, 6, 0));
    QVERIFY(blockModel->updateBlockText(codeId, "deep();"));
    QTest::qWait(400);
    view->setProperty("contentY",
                      view->property("contentHeight").toReal()
                          - view->height());
    QTest::qWait(300);

    QQuickItem *codeEditor = editorForBlock(codeId);
    QVERIFY(codeEditor);
    codeEditor->forceActiveFocus();
    QTest::qWait(100);
    QCOMPARE(focusedBlockId(), codeId);

    QTest::keyClick(m_window, Qt::Key_Return, Qt::ControlModifier);
    QTest::qWait(500);

    QCOMPARE(blockModel->rowCount(), codeRow + 2);
    const QString nextId = blockModel->blockIdAt(codeRow + 1);
    QVERIFY(!nextId.isEmpty());
    QCOMPARE(focusedBlockId(), nextId);
  }

  QPoint closingFenceClickPoint(QQuickItem *codeEditor) const {
    const QString text = codeEditor->property("text").toString();
    const int fencePos = text.lastIndexOf('\n') + 1;
    QRectF lineRect;
    QMetaObject::invokeMethod(codeEditor, "positionToRectangle",
                              Q_RETURN_ARG(QRectF, lineRect),
                              Q_ARG(int, fencePos));
    return codeEditor
        ->mapToScene(QPointF(lineRect.x() + lineRect.width() / 2,
                             lineRect.y() + lineRect.height() / 2))
        .toPoint();
  }

  void clickingClosingFenceExitsCodeBlock() {
    BlockModel *blockModel = model();

    const QString afterId =
        blockModel->appendBlock(static_cast<int>(BlockType::Paragraph),
                                QStringLiteral("after"));
    QVERIFY(!afterId.isEmpty());

    const QString codeId = blockModel->blockIdAt(1);
    QVERIFY(!codeId.isEmpty());
    QVERIFY(blockModel->changeBlockType(codeId, 6, 0));
    QVERIFY(blockModel->updateBlockText(codeId, "const x = 1;"));
    QTest::qWait(300);

    QQuickItem *codeEditor = editorForBlock(codeId);
    QVERIFY(codeEditor);
    codeEditor->forceActiveFocus();
    QTest::qWait(100);
    QCOMPARE(focusedBlockId(), codeId);

    const QPoint click = closingFenceClickPoint(codeEditor);
    QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, click);
    QTest::qWait(300);

    // A new empty paragraph directly below the code block receives focus;
    // the pre-existing "after" block is pushed down.
    QCOMPARE(blockModel->rowCount(), 4);
    const QString nextId = blockModel->blockIdAt(2);
    QVERIFY(!nextId.isEmpty());
    QVERIFY(nextId != afterId);
    QCOMPARE(blockModel->data(blockModel->index(2, 0),
                              BlockModel::BlockTypeRole)
                 .toInt(),
             static_cast<int>(BlockType::Paragraph));
    QCOMPARE(blockModel->data(blockModel->index(2, 0),
                              BlockModel::ContentRole).toString(),
             QString());
    QCOMPARE(blockModel->blockIdAt(3), afterId);
    QCOMPARE(focusedBlockId(), nextId);
  }

  void clickingCodeLineDoesNotExit() {
    BlockModel *blockModel = model();

    const QString codeId = blockModel->blockIdAt(1);
    QVERIFY(!codeId.isEmpty());
    QVERIFY(blockModel->changeBlockType(codeId, 6, 0));
    QVERIFY(blockModel->updateBlockText(codeId, "const x = 1;"));
    QTest::qWait(300);

    QQuickItem *codeEditor = editorForBlock(codeId);
    QVERIFY(codeEditor);
    codeEditor->forceActiveFocus();
    QTest::qWait(100);

    // Click on a code body line, well above the closing fence.
    QRectF bodyRect;
    QMetaObject::invokeMethod(codeEditor, "positionToRectangle",
                              Q_RETURN_ARG(QRectF, bodyRect), Q_ARG(int, 5));
    const QPoint click =
        codeEditor->mapToScene(QPointF(bodyRect.x() + bodyRect.width() / 2,
                                       bodyRect.y() + bodyRect.height() / 2))
            .toPoint();
    QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, click);
    QTest::qWait(200);

    QCOMPARE(blockModel->rowCount(), 2);
    QCOMPARE(focusedBlockId(), codeId);
  }

  void longCodeBlockExitDoesNotWarnAboutCursorPosition() {
    BlockModel *blockModel = model();

    const QString codeId = blockModel->blockIdAt(1);
    QVERIFY(!codeId.isEmpty());
    QVERIFY(blockModel->changeBlockType(codeId, 6, 0));
    QVERIFY(blockModel->setBlockCodeLanguage(codeId, "cpp"));

    QString body;
    for (int i = 0; i < 30; ++i)
      body += QStringLiteral("const auto value%1 = computedValue%1;\n").arg(i);
    QVERIFY(blockModel->updateBlockText(codeId, body));
    QTest::qWait(300);

    g_cursorWarnings = 0;
    g_previousHandler = qInstallMessageHandler(cursorWarningHandler);

    QQuickItem *codeEditor = editorForBlock(codeId);
    QVERIFY(codeEditor);

    // Each exit shrinks the editor from raw markdown (with fences) to the
    // plain body while the caret sits at the end. This used to emit
    // "QTextCursor::setPosition: Position ... out of range".
    for (int cycle = 0; cycle < 2; ++cycle) {
      codeEditor->forceActiveFocus();
      QTest::qWait(50);
      codeEditor->setProperty("cursorPosition",
                              codeEditor->property("length").toInt());
      QTest::qWait(30);
      QTest::keyClick(m_window, Qt::Key_Return);
      QTest::qWait(100);

      codeEditor = editorForBlock(codeId);
      QVERIFY(codeEditor);
      QCOMPARE(focusedBlockId(), blockModel->blockIdAt(2));
    }

    qInstallMessageHandler(g_previousHandler);
    g_previousHandler = nullptr;
    QCOMPARE(g_cursorWarnings, 0);
  }

  void keyboardExitKeepsCodeLanguageTag() {
    BlockModel *blockModel = model();

    const QString codeId = blockModel->blockIdAt(1);
    QVERIFY(!codeId.isEmpty());
    QVERIFY(blockModel->changeBlockType(codeId, 6, 0));
    QVERIFY(blockModel->setBlockCodeLanguage(codeId, "python"));
    QVERIFY(blockModel->updateBlockText(codeId, "print(1)"));
    QTest::qWait(300);

    QQuickItem *codeEditor = editorForBlock(codeId);
    QVERIFY(codeEditor);
    codeEditor->forceActiveFocus();
    QTest::qWait(100);
    QVERIFY(codeEditor->property("text").toString().startsWith("```python"));

    QTest::keyClick(m_window, Qt::Key_Return, Qt::ControlModifier);
    QTest::qWait(300);

    QCOMPARE(blockModel->data(blockModel->index(1, 0),
                              BlockModel::CodeLanguageRole).toString(),
             QStringLiteral("python"));
  }

  void typedLanguageTagSurvivesKeyboardExit() {
    BlockModel *blockModel = model();

    const QString codeId = blockModel->blockIdAt(1);
    QVERIFY(!codeId.isEmpty());
    QVERIFY(blockModel->changeBlockType(codeId, 6, 0));
    QVERIFY(blockModel->setBlockCodeLanguage(codeId, ""));
    QVERIFY(blockModel->updateBlockText(codeId, "print(1)"));
    QTest::qWait(300);

    QQuickItem *codeEditor = editorForBlock(codeId);
    QVERIFY(codeEditor);
    codeEditor->forceActiveFocus();
    QTest::qWait(100);

    // Type the tag into the opening fence: ``` -> ```python
    codeEditor->setProperty("cursorPosition", 3);
    QTest::qWait(30);
    typeText(m_window, "python");
    QTest::qWait(100);
    QCOMPARE(codeEditor->property("text").toString(),
             QStringLiteral("```python\nprint(1)\n```"));
    QCOMPARE(blockModel->data(blockModel->index(1, 0),
                              BlockModel::CodeLanguageRole).toString(),
             QStringLiteral("python"));

    QTest::keyClick(m_window, Qt::Key_Return, Qt::ControlModifier);
    QTest::qWait(300);

    QCOMPARE(blockModel->data(blockModel->index(1, 0),
                              BlockModel::CodeLanguageRole).toString(),
             QStringLiteral("python"));
  }

  void typingMultipleCodeLinesDoesNotExit() {
    BlockModel *blockModel = model();

    const QString codeId = blockModel->blockIdAt(1);
    QVERIFY(!codeId.isEmpty());
    QVERIFY(blockModel->changeBlockType(codeId, 6, 0));
    QVERIFY(blockModel->updateBlockText(codeId, ""));
    QTest::qWait(300);

    QQuickItem *codeEditor = editorForBlock(codeId);
    QVERIFY(codeEditor);
    codeEditor->forceActiveFocus();
    QTest::qWait(100);

    typeText(m_window, "first");
    QTest::qWait(50);
    QTest::keyClick(m_window, Qt::Key_Return);
    QTest::qWait(50);
    typeText(m_window, "second");
    QTest::qWait(50);
    QTest::keyClick(m_window, Qt::Key_Return);
    QTest::qWait(50);
    typeText(m_window, "third");
    QTest::qWait(50);

    QCOMPARE(blockModel->rowCount(), 2);
    QCOMPARE(focusedBlockId(), codeId);
    const QString body = blockModel->data(blockModel->index(1, 0),
                                          BlockModel::ContentRole).toString();
    QCOMPARE(body, QStringLiteral("first\nsecond\nthird"));
  }

  void enterInFreshlyFocusedCodeBlockDoesNotExit() {
    BlockModel *blockModel = model();

    const QString codeId = blockModel->blockIdAt(1);
    QVERIFY(!codeId.isEmpty());
    QVERIFY(blockModel->changeBlockType(codeId, 6, 0));
    QVERIFY(blockModel->updateBlockText(codeId, ""));
    QTest::qWait(300);

    QQuickItem *codeEditor = editorForBlock(codeId);
    QVERIFY(codeEditor);
    codeEditor->forceActiveFocus();
    QTest::qWait(100);
    QCOMPARE(focusedBlockId(), codeId);

    // A newly focused (possibly empty) code block must keep its caret in the
    // body, not on the closing fence: the first Enter inserts a newline.
    const QString text = codeEditor->property("text").toString();
    const int fenceStart = text.lastIndexOf('\n') + 1;
    QVERIFY(codeEditor->property("cursorPosition").toInt() < fenceStart);

    QTest::keyClick(m_window, Qt::Key_Return);
    QTest::qWait(100);

    QCOMPARE(blockModel->rowCount(), 2);
    QCOMPARE(focusedBlockId(), codeId);
    const QString body = blockModel->data(blockModel->index(1, 0),
                                          BlockModel::ContentRole).toString();
    QVERIFY(body.contains('\n'));
  }

  void enterAtEndOfLastCodeLineInsertsNewline() {
    BlockModel *blockModel = model();

    const QString codeId = blockModel->blockIdAt(1);
    QVERIFY(!codeId.isEmpty());
    QVERIFY(blockModel->changeBlockType(codeId, 6, 0));
    QVERIFY(blockModel->updateBlockText(codeId, "const x = 1;"));
    QTest::qWait(300);

    QQuickItem *codeEditor = editorForBlock(codeId);
    QVERIFY(codeEditor);
    codeEditor->forceActiveFocus();
    QTest::qWait(100);

    // Caret at the end of the final code line, i.e. on the newline that
    // precedes the closing fence. Enter here must insert a newline, not exit.
    const QString text = codeEditor->property("text").toString();
    const int lastNewline = text.lastIndexOf('\n');
    QVERIFY(lastNewline > 0);
    codeEditor->setProperty("cursorPosition", lastNewline);
    QTest::qWait(30);
    QTest::keyClick(m_window, Qt::Key_Return);
    QTest::qWait(100);

    QCOMPARE(blockModel->rowCount(), 2);
    QCOMPARE(focusedBlockId(), codeId);
    const QString body = blockModel->data(blockModel->index(1, 0),
                                          BlockModel::ContentRole).toString();
    QVERIFY(body.contains('\n'));
  }

  void enterInsideCodeBodyInsertsNewline() {
    BlockModel *blockModel = model();

    const QString codeId = blockModel->blockIdAt(1);
    QVERIFY(!codeId.isEmpty());
    QVERIFY(blockModel->changeBlockType(codeId, 6, 0));
    QVERIFY(blockModel->updateBlockText(codeId, "const x = 1;"));
    QTest::qWait(300);

    QQuickItem *codeEditor = editorForBlock(codeId);
    QVERIFY(codeEditor);
    codeEditor->forceActiveFocus();
    QTest::qWait(100);
    QCOMPARE(focusedBlockId(), codeId);

    // Place the caret inside the code body (before the closing fence) and
    // press Enter: it must insert a newline, not exit the block.
    codeEditor->setProperty("cursorPosition", 6);
    QTest::qWait(30);
    QTest::keyClick(m_window, Qt::Key_Return);
    QTest::qWait(100);

    const int rowsBefore = blockModel->rowCount();
    QCOMPARE(rowsBefore, 2);
    QCOMPARE(focusedBlockId(), codeId);
    const QString body = blockModel->data(blockModel->index(1, 0),
                                          BlockModel::ContentRole).toString();
    QVERIFY(body.contains('\n'));
  }

  void imageBlockLoadsAndResizes() {
    BlockModel *blockModel = model();
    const QString imgId = blockModel->appendBlock(8, "My Caption");
    QVERIFY(!imgId.isEmpty());
    QTest::qWait(100);

    QVERIFY(blockModel->setBlockImageSource(imgId, "file:///tmp/test.png"));
    QVERIFY(blockModel->setBlockImageWidthRatio(imgId, 0.65));
    QTest::qWait(100);

    const int imgRow = blockModel->indexOfBlock(imgId);
    QVERIFY(imgRow >= 0);
    const QModelIndex idx = blockModel->index(imgRow, 0);
    QCOMPARE(blockModel->data(idx, BlockModel::BlockTypeRole).toInt(), 8);
    QCOMPARE(blockModel->data(idx, BlockModel::ImageSourceRole).toString(), QString("file:///tmp/test.png"));
    QCOMPARE(blockModel->data(idx, BlockModel::ImageWidthRatioRole).toDouble(), 0.65);
    QCOMPARE(blockModel->data(idx, BlockModel::ContentRole).toString(), QString("My Caption"));
  }

  void columnLayoutRendersAndFocusesChild() {
    BlockModel *blockModel = model();
    const QString childId = blockModel->createColumnsLayout(0, 2);
    QVERIFY(!childId.isEmpty());
    QVERIFY(blockModel->updateBlockText(childId, ""));

    QTest::qWait(300);
    m_window->grabWindow();
    QTest::qWait(100);

    // Two column children plus the original second top-level block.
    QVERIFY2(editors().size() >= 3, "ColumnsBlock should render its child editors");

    QVERIFY(QMetaObject::invokeMethod(m_window, "focusBlock",
                                      Q_ARG(QVariant, childId),
                                      Q_ARG(QVariant, 0)));
    QTest::qWait(300);
    QVERIFY(m_window->activeFocusItem());

    typeText(m_window, "abc");
    QTest::qWait(100);
    QCOMPARE(blockModel->document().findBlock(childId)->text, QString("abc"));
  }

  void nestedGutterPillVisibleOnHover() {
    BlockModel *blockModel = model();
    const QString firstChildId = blockModel->createColumnsLayout(0, 2);
    QVERIFY(!firstChildId.isEmpty());
    QTest::qWait(400);
    m_window->grabWindow();
    QTest::qWait(100);

    // The nested reorder handles are Repeater delegates, which are not
    // reachable through findChild(); walk the visual tree instead.
    QList<QQuickItem *> pills;
    std::function<void(QQuickItem *)> findPills = [&](QQuickItem *item) {
      if (!item)
        return;
      if (item->objectName() == "nestedChildPill")
        pills.append(item);
      for (QQuickItem *child : item->childItems())
        findPills(child);
    };
    findPills(m_window->contentItem());
    QCOMPARE(pills.size(), 2);
    QVERIFY2(pills[0]->opacity() < 0.01,
             "Nested gutter pill should start hidden");

    auto centerOf = [](QQuickItem *item) {
      return item->mapToScene(
        QPointF(item->width() / 2, item->height() / 2)).toPoint();
    };

    const QPoint source = centerOf(pills[0]);
    QTest::mouseMove(m_window, QPoint(400, 400));
    QTest::qWait(50);
    QTest::mouseMove(m_window, source);
    QTest::qWait(250);

    // Hover is routed through ColumnsBlock.hoveredChildBlockId and compared
    // against the Repeater's modelData. modelData is a QML context property,
    // so qualifying it as childHandle.modelData makes it undefined and the
    // pill never appears.
    QVERIFY2(pills[0]->opacity() > 0.9,
             "Nested gutter pill should appear when its block is hovered");

    // The same modelData is passed to beginChildDrag, so a cross-column drag
    // from the pill must reparent the correct block.
    const QString layoutId = blockModel->blockIdAt(0);
    const QStringList columnIds = blockModel->columnIdsOf(layoutId);
    QCOMPARE(columnIds.size(), 2);

    QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, source);
    QTest::qWait(260);
    const QPoint target = centerOf(pills[1]) + QPoint(220, 0);
    QTest::mouseMove(m_window, target);
    QTest::qWait(80);
    QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier, target);
    QTest::qWait(200);

    QCOMPARE(blockModel->columnIdOfBlock(firstChildId), columnIds.at(1));
  }

  void eachColumnHasIndependentResizeSlider() {
    BlockModel *blockModel = model();
    blockModel->createColumnsLayout(0, 2);
    QTest::qWait(400);
    m_window->grabWindow();
    QTest::qWait(100);

    QList<QQuickItem *> dividers;
    QQuickItem *flickable = nullptr;
    std::function<void(QQuickItem *)> findDividers = [&](QQuickItem *item) {
      if (!item)
        return;
      if (item->objectName() == "columnResizeDivider" && item->isVisible())
        dividers.append(item);
      if (item->objectName() == "columnsFlickable")
        flickable = item;
      for (QQuickItem *child : item->childItems())
        findDividers(child);
    };
    findDividers(m_window->contentItem());
    QCOMPARE(dividers.size(), 2);

    const QString layoutId = blockModel->blockIdAt(0);
    const QStringList columns = blockModel->columnIdsOf(layoutId);
    QCOMPARE(columns.size(), 2);

    auto centerOf = [](QQuickItem *item) {
      return item->mapToScene(
        QPointF(item->width() / 2, item->height() / 2)).toPoint();
    };

    const double left0 = blockModel->columnWidth(columns[0]);
    const double right0 = blockModel->columnWidth(columns[1]);

    // Drag the first pane's own slider: only it grows.
    const QPoint first = centerOf(dividers[0]);
    QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, first);
    QTest::mouseMove(m_window, first + QPoint(100, 0));
    QTest::qWait(80);
    QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier,
                        first + QPoint(100, 0));
    QTest::qWait(200);

    QVERIFY2(blockModel->columnWidth(columns[0]) > left0 + 80,
             "First pane should grow via its own slider");
    QVERIFY2(qFuzzyCompare(blockModel->columnWidth(columns[1]), right0),
             "Dragging one slider must not resize its neighbour");

    // The last pane has its own slider too and can be resized independently.
    // After growing the first pane the layout overflows, so scroll it into view.
    QVERIFY(flickable);
    flickable->setProperty("contentX",
                           flickable->property("contentWidth").toDouble()
                           - flickable->width());
    QTest::qWait(100);

    const double middle = blockModel->columnWidth(columns[1]);
    const QPoint second = centerOf(dividers[1]);
    QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, second);
    QTest::mouseMove(m_window, second + QPoint(100, 0));
    QTest::qWait(80);
    QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier,
                        second + QPoint(100, 0));
    QTest::qWait(200);

    QVERIFY2(blockModel->columnWidth(columns[1]) > middle + 80,
             "Last pane should grow via its own slider");
    QVERIFY(blockModel->columnWidth(columns[0]) > left0 + 80);
  }

  void columnResizeAutoScrollsAtEdge() {
    BlockModel *blockModel = model();
    blockModel->createColumnsLayout(0, 2);
    QTest::qWait(400);
    m_window->grabWindow();
    QTest::qWait(100);

    QList<QQuickItem *> dividers;
    QQuickItem *flickable = nullptr;
    std::function<void(QQuickItem *)> find = [&](QQuickItem *item) {
      if (!item)
        return;
      if (item->objectName() == "columnResizeDivider" && item->isVisible())
        dividers.append(item);
      if (item->objectName() == "columnsFlickable")
        flickable = item;
      for (QQuickItem *child : item->childItems())
        find(child);
    };
    find(m_window->contentItem());
    QVERIFY(flickable);
    QVERIFY2(!dividers.isEmpty(), "Every pane should have a resize slider");

    const QString layoutId = blockModel->blockIdAt(0);
    const QStringList columns = blockModel->columnIdsOf(layoutId);
    QCOMPARE(columns.size(), 2);

    const double initial = blockModel->columnWidth(columns.first());
    const QQuickItem *divider = dividers.first();
    const QPoint start = divider->mapToScene(
      QPointF(divider->width() / 2, divider->height() / 2)).toPoint();

    // Hold the slider just inside the right edge of the Flickable viewport.
    const int viewportRight = flickable->mapToScene(
      QPointF(flickable->width(), 0)).toPoint().x();

    QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(m_window, QPoint(viewportRight - 10, start.y()));
    QTest::qWait(500);

    const double contentX = flickable->property("contentX").toDouble();
    const double widthAfter = blockModel->columnWidth(columns.first());
    QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier,
                        QPoint(viewportRight - 10, start.y()));
    QTest::qWait(200);

    QVERIFY2(contentX > 5.0,
             "Holding the slider at the edge should auto-scroll horizontally");
    QVERIFY2(widthAfter > initial + 100.0,
             "The pane should keep growing while the layout scrolls");
  }

  void gutterHoverAndClick() {
    QObject *editorObject = m_window->findChild<QObject *>("editor");
    QVERIFY(editorObject);

    const QString secondId = model()->blockIdAt(1);
    QVERIFY(!secondId.isEmpty());

    // documentView starts around x=50; the handle sits in its 30px left margin.
    QTest::mouseMove(m_window, QPoint(400, 400));
    QTest::qWait(50);
    QTest::mouseMove(m_window, QPoint(36, 190));
    QTest::qWait(150);
    QCOMPARE(editorObject->property("gutterHoverBlockId").toString(), secondId);

    QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, QPoint(36, 190));
    QTest::qWait(150);
    QObject *menu = m_window->findChild<QObject *>("gutterOptionsMenu");
    QVERIFY(menu);
    QVERIFY(menu->property("visible").toBool());
    QCOMPARE(menu->property("blockId").toString(), secondId);
  }

  void gutterDragReordersBlocks() {
    const QString firstBefore = model()->blockIdAt(0);
    const QString secondBefore = model()->blockIdAt(1);
    QVERIFY(!firstBefore.isEmpty() && !secondBefore.isEmpty());

    // Hold the second block's gutter handle, then drag it above the first.
    QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, QPoint(36, 190));
    QTest::qWait(260);
    QTest::mouseMove(m_window, QPoint(36, 140));
    QTest::qWait(80);
    QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier,
                        QPoint(36, 140));
    QTest::qWait(200);

    QCOMPARE(model()->blockIdAt(0), secondBefore);
    QCOMPARE(model()->blockIdAt(1), firstBefore);
  }

  void clickingEmptyFooterCreatesBlock() {
    const int before = model()->count();
    QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, QPoint(400, 300));
    QTest::qWait(200);

    QCOMPARE(model()->count(), before + 1);
    QObject *editorObject = m_window->findChild<QObject *>("editor");
    QVERIFY(editorObject);
    QCOMPARE(editorObject->property("caretBlockId").toString(),
             model()->blockIdAt(before));
  }

  void clickingColumnTailCreatesBlock() {
    BlockModel *blockModel = model();
    blockModel->createColumnsLayout(0, 2);
    QTest::qWait(400);

    const QString layoutId = blockModel->blockIdAt(0);
    const QString column = blockModel->columnIdsOf(layoutId).first();
    const int before = blockModel->blocksOfColumn(column).size();

    QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, QPoint(150, 180));
    QTest::qWait(200);

    const QStringList after = blockModel->blocksOfColumn(column);
    QCOMPARE(after.size(), before + 1);
    QObject *editorObject = m_window->findChild<QObject *>("editor");
    QVERIFY(editorObject);
    QCOMPARE(editorObject->property("caretBlockId").toString(), after.last());
  }

  void commandMenuCreatesColumnLayout() {
    auto textEditors = editors();
    QVERIFY(textEditors.size() >= 2);

    // The command menu only opens for paragraph blocks, and only when the
    // slash is the first character of the block.
    QVERIFY(model()->updateBlockText(model()->blockIdAt(1), ""));
    QTest::qWait(100);
    textEditors = editors();

    textEditors[1]->forceActiveFocus();
    QTest::qWait(50);
    typeText(m_window, "/col");
    QTest::qWait(200);
    QObject *menu = m_window->findChild<QObject *>("commandMenu");
    QVERIFY(menu && menu->property("visible").toBool());

    // Select "Columns" from the slash menu.
    QTest::keyClick(m_window, Qt::Key_Return);
    QTest::qWait(200);

    // Confirm the second-step count picker (default 2 columns).
    QTest::keyClick(m_window, Qt::Key_Return);
    QTest::qWait(400);

    QCOMPARE(model()->document().blockAt(1)->type, BlockType::Columns);

    const QString layoutId = model()->blockIdAt(1);
    const QStringList columns = model()->columnIdsOf(layoutId);
    QCOMPARE(columns.size(), 2);

    // A fresh layout fills the editor: the two panes split the available
    // width (editor width minus the gap and the add-column strip) evenly.
    QQuickItem *documentView = m_window->findChild<QQuickItem *>("documentView");
    QVERIFY(documentView);
    const double expected = documentView->width() - 8 - 18;
    double total = 0.0;
    for (const QString &id : columns)
      total += model()->columnWidth(id);
    QVERIFY2(qAbs(total - expected) < 1.0,
             "New columns should share the editor's available width");
    QVERIFY(qFuzzyCompare(model()->columnWidth(columns[0]),
                          model()->columnWidth(columns[1])));
  }

  void arrowNavigationCrossesColumns() {
    BlockModel *blockModel = model();
    blockModel->createColumnsLayout(0, 2);
    const QString layoutId = blockModel->blockIdAt(0);
    const QStringList columns = blockModel->columnIdsOf(layoutId);
    QCOMPARE(columns.size(), 2);

    const QString first = blockModel->blocksOfColumn(columns[0])[0];
    const QString second = blockModel->appendBlockToColumn(columns[0]);
    const QString inNextColumn = blockModel->blocksOfColumn(columns[1])[0];
    blockModel->updateBlockText(first, "a");
    blockModel->updateBlockText(second, "b");
    blockModel->updateBlockText(inNextColumn, "c");
    QTest::qWait(300);

    QVERIFY(QMetaObject::invokeMethod(m_window, "focusBlock",
                                      Q_ARG(QVariant, first),
                                      Q_ARG(QVariant, 0)));
    QTest::qWait(200);

    QObject *editorObject = m_window->findChild<QObject *>("editor");
    QVERIFY(editorObject);
    QCOMPARE(editorObject->property("caretBlockId").toString(), first);

    QTest::keyClick(m_window, Qt::Key_Down);
    QTest::qWait(150);
    const QString afterDown = editorObject->property("caretBlockId").toString();
    QCOMPARE(afterDown, second);

    QTest::keyClick(m_window, Qt::Key_Down);
    QTest::qWait(150);
    QCOMPARE(editorObject->property("caretBlockId").toString(), inNextColumn);
  }

  void tabNavigationCrossesColumns() {
    BlockModel *blockModel = model();
    blockModel->createColumnsLayout(0, 2);
    const QString layoutId = blockModel->blockIdAt(0);
    const QStringList columns = blockModel->columnIdsOf(layoutId);

    const QString first = blockModel->blocksOfColumn(columns[0])[0];
    const QString inNextColumn = blockModel->blocksOfColumn(columns[1])[0];
    QTest::qWait(300);

    QVERIFY(QMetaObject::invokeMethod(m_window, "focusBlock",
                                      Q_ARG(QVariant, first),
                                      Q_ARG(QVariant, 0)));
    QTest::qWait(200);

    QObject *editorObject = m_window->findChild<QObject *>("editor");
    QVERIFY(editorObject);
    QCOMPARE(editorObject->property("caretBlockId").toString(), first);

    QTest::keyClick(m_window, Qt::Key_Tab);
    QTest::qWait(150);
    QCOMPARE(editorObject->property("caretBlockId").toString(), inNextColumn);
  }

  void columnCollapseRemovesLayoutFromEditor() {
    BlockModel *blockModel = model();
    const QString childId = blockModel->createColumnsLayout(0, 2);
    QVERIFY(!childId.isEmpty());
    QTest::qWait(200);

    const QString layoutId = blockModel->blockIdAt(0);
    const QStringList columns = blockModel->columnIdsOf(layoutId);
    QCOMPARE(columns.size(), 2);
    QVERIFY(blockModel->removeColumn(columns.first()));
    QTest::qWait(200);

    QCOMPARE(blockModel->document().blockAt(0)->type, BlockType::Paragraph);
    QCOMPARE(blockModel->visibleCount(), 4);
    QVERIFY(editors().size() >= 4);
  }

  void columnLayoutUndoRestoresTopLevelBlocks() {
    BlockModel *blockModel = model();
    const int originalCount = blockModel->count();
    const QString childId = blockModel->createColumnsLayout(0, 3);
    QVERIFY(!childId.isEmpty());
    QTest::qWait(200);

    QVERIFY(blockModel->undo());
    QCOMPARE(blockModel->count(), originalCount);
    QCOMPARE(blockModel->document().blockAt(0)->type, BlockType::Heading);
  }

};

QTEST_MAIN(EditorQmlTests)

#include "tst_editor.moc"
