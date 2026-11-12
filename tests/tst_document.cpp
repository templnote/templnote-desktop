
#include <QtTest>

#include "../src/core/Document.h"

using namespace Templnote;

class DocumentTests : public QObject {
  Q_OBJECT

private slots:

  void appendBlock() {
    Document document;

    Block block = Block::create(BlockType::Paragraph, "Hello");

    QVERIFY(document.appendBlock(block));

    QCOMPARE(document.count(), 1);
    QCOMPARE(document.blockAt(0)->text, QString("Hello"));
  }

  void rejectDuplicateIds() {
    Document document;

    Block block = Block::create();

    QVERIFY(document.appendBlock(block));
    QVERIFY(!document.appendBlock(block));

    QCOMPARE(document.count(), 1);
  }

  void updateText() {
    Document document;

    Block block = Block::create();

    document.appendBlock(block);

    QVERIFY(document.updateBlockText(block.id, "Updated content"));

    QCOMPARE(document.findBlock(block.id)->text, QString("Updated content"));
  }

  void changeBlockType() {
    Document document;

    Block block = Block::create();

    document.appendBlock(block);

    QVERIFY(document.updateBlockType(block.id, BlockType::Heading));

    QCOMPARE(document.findBlock(block.id)->type, BlockType::Heading);
  }

  void removeBlock() {
    Document document;

    Block block = Block::create();

    document.appendBlock(block);

    QVERIFY(document.removeBlock(block.id));

    QCOMPARE(document.count(), 0);
    QVERIFY(!document.removeBlock(block.id));
  }

  void moveBlock() {
    Document document;

    Block first = Block::create(BlockType::Paragraph, "First");

    Block second = Block::create(BlockType::Paragraph, "Second");

    document.appendBlock(first);
    document.appendBlock(second);

    QVERIFY(document.moveBlock(first.id, 1));

    QCOMPARE(document.blockAt(0)->text, QString("Second"));

    QCOMPARE(document.blockAt(1)->text, QString("First"));
  }
};

QTEST_GUILESS_MAIN(DocumentTests)

#include "tst_document.moc"
