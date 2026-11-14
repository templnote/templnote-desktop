
#include <QtTest>

#include "../src/model/BlockModel.h"

using namespace Templnote;

class BlockModelTests : public QObject {
  Q_OBJECT

private slots:

  void initialBlocks() {
    BlockModel model;

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.count(), 2);
  }

  void exposesQmlRoles() {
    BlockModel model;
    const auto roles = model.roleNames();

    QCOMPARE(roles.value(BlockModel::BlockIdRole), QByteArray("blockId"));
    QCOMPARE(roles.value(BlockModel::ParentIdRole), QByteArray("parentId"));
    QCOMPARE(roles.value(BlockModel::BlockTypeRole), QByteArray("blockType"));
    QCOMPARE(roles.value(BlockModel::ContentRole), QByteArray("content"));
    QCOMPARE(roles.value(BlockModel::HeadingLevelRole),
             QByteArray("headingLevel"));
    QCOMPARE(roles.value(BlockModel::MetadataRole), QByteArray("metadata"));
    QCOMPARE(roles.value(BlockModel::NumberIndexRole), QByteArray("numberIndex"));

    const QModelIndex first = model.index(0, 0);
    QVERIFY(first.isValid());
    QVERIFY(!model.data(first, BlockModel::BlockIdRole).toString().isEmpty());
    QCOMPARE(model.data(first, BlockModel::ContentRole).toString(),
             QString("Welcome to Templnote"));
  }

  void appendBlock() {
    BlockModel model;

    const int previousCount = model.count();

    const QString id = model.appendBlock(static_cast<int>(BlockType::Paragraph),
                                         "New paragraph");

    QVERIFY(!id.isEmpty());
    QCOMPARE(model.count(), previousCount + 1);
  }

  void invalidBlockType() {
    BlockModel model;

    const int previousCount = model.count();

    const QString id = model.appendBlock(999, "Invalid");

    QVERIFY(id.isEmpty());
    QCOMPARE(model.count(), previousCount);
  }

  void updateContent() {
    BlockModel model;

    const QString id = model.appendBlock(0, "Original");

    QVERIFY(model.updateBlockText(id, "Modified"));

    const int row = model.document().indexOf(id);

    QCOMPARE(model.data(model.index(row), BlockModel::ContentRole).toString(),
             QString("Modified"));
  }

  void removeBlock() {
    BlockModel model;

    const QString id = model.appendBlock(0, "Delete me");

    QVERIFY(model.removeBlock(id));
    QVERIFY(!model.removeBlock(id));
  }

  void numberedListSequentialIndex() {
    BlockModel model;

    const QString id1 = model.appendBlock(static_cast<int>(BlockType::NumberedList), "Item 1");
    const QString id2 = model.appendBlock(static_cast<int>(BlockType::NumberedList), "Item 2");
    const QString id3 = model.appendBlock(static_cast<int>(BlockType::NumberedList), "Item 3");

    int r1 = model.document().indexOf(id1);
    int r2 = model.document().indexOf(id2);
    int r3 = model.document().indexOf(id3);

    QCOMPARE(model.data(model.index(r1), BlockModel::NumberIndexRole).toInt(), 1);
    QCOMPARE(model.data(model.index(r2), BlockModel::NumberIndexRole).toInt(), 2);
    QCOMPARE(model.data(model.index(r3), BlockModel::NumberIndexRole).toInt(), 3);

    // Delete middle item, count should auto-adjust
    QVERIFY(model.removeBlock(id2));
    r3 = model.document().indexOf(id3);
    QCOMPARE(model.data(model.index(r3), BlockModel::NumberIndexRole).toInt(), 2);
  }

  void undoAcrossBlocks() {
    BlockModel model;

    const QString firstId =
        model.document().blockAt(0)->id;

    const QString secondId =
        model.document().blockAt(1)->id;

    QVERIFY(model.updateBlockText(
        firstId,
        "First changed"));

    QVERIFY(model.updateBlockText(
        secondId,
        "Second changed"));

    QCOMPARE(
        model.data(
            model.index(0),
            BlockModel::ContentRole).toString(),
        QString("First changed"));

    QCOMPARE(
        model.data(
            model.index(1),
            BlockModel::ContentRole).toString(),
        QString("Second changed"));

    QVERIFY(model.undo());

    QCOMPARE(
        model.data(
            model.index(0),
            BlockModel::ContentRole).toString(),
        QString("First changed"));

    QCOMPARE(
        model.data(
            model.index(1),
            BlockModel::ContentRole).toString(),
        QString("Your local-first writing environment"));

    QVERIFY(model.undo());

    QCOMPARE(
        model.data(
            model.index(0),
            BlockModel::ContentRole).toString(),
        QString("Welcome to Templnote"));
  }

  void undoTextCharacterByCharacter() {
    BlockModel model;

    const QString id = model.document().blockAt(0)->id;
    const QString original = model.document().blockAt(0)->text;

    QVERIFY(model.updateBlockText(id, original + "!"));

    QCOMPARE(
        model.data(model.index(0), BlockModel::ContentRole).toString(),
        original + "!");

    QVERIFY(model.undo());
    QCOMPARE(
        model.data(model.index(0), BlockModel::ContentRole).toString(),
        original);

    QVERIFY(model.redo());
    QCOMPARE(
        model.data(model.index(0), BlockModel::ContentRole).toString(),
        original + "!");
  }

  void historyReportsBlockToFocus() {
    BlockModel model;

    const QString firstId = model.document().blockAt(0)->id;
    const QString secondId = model.document().blockAt(1)->id;

    QVERIFY(model.updateBlockText(firstId, "First changed"));
    QVERIFY(model.updateBlockText(secondId, "Second changed"));

    QVERIFY(model.undo());
    QCOMPARE(model.lastHistoryBlockId(), secondId);

    QVERIFY(model.undo());
    QCOMPARE(model.lastHistoryBlockId(), firstId);

    QVERIFY(model.redo());
    QCOMPARE(model.lastHistoryBlockId(), firstId);

    QVERIFY(model.redo());
    QCOMPARE(model.lastHistoryBlockId(), secondId);
  }

  void undoTextWordByWord() {
    BlockModel model;

    const QString id = model.document().blockAt(0)->id;
    const QString original = model.document().blockAt(0)->text;

    // Simulate typing one character at a time. BlockModel should group the
    // characters into word-sized undo steps instead of restoring the whole
    // block.
    QString text = original + "first";

    for (int i = original.size() + 1; i <= text.size(); ++i)
      QVERIFY(model.updateBlockText(id, text.left(i)));

    text += " second";

    for (int i = original.size() + 6; i <= text.size(); ++i)
      QVERIFY(model.updateBlockText(id, text.left(i)));

    QVERIFY(model.undo());
    QCOMPARE(
        model.data(model.index(0), BlockModel::ContentRole).toString(),
        original + "first ");

    QVERIFY(model.undo());
    QCOMPARE(
        model.data(model.index(0), BlockModel::ContentRole).toString(),
        original);

    QVERIFY(model.redo());
    QCOMPARE(
        model.data(model.index(0), BlockModel::ContentRole).toString(),
        original + "first ");

    QVERIFY(model.redo());
    QCOMPARE(
        model.data(model.index(0), BlockModel::ContentRole).toString(),
        original + "first second");
  }
};

QTEST_GUILESS_MAIN(BlockModelTests)

#include "tst_blockmodel.moc"
