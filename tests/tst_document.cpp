
#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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

    QVERIFY(document.updateBlockAlignment(block.id, "center"));
    QCOMPARE(document.findBlock(block.id)->metadata.value("align").toString(), QString("center"));

    QVERIFY(document.updateBlockAlignment(block.id, "left"));
    QVERIFY(!document.findBlock(block.id)->metadata.contains("align"));

    QVERIFY(document.updateBlockChecked(block.id, true));
    QCOMPARE(document.findBlock(block.id)->metadata.value("checked").toBool(), true);

    QVERIFY(document.updateBlockChecked(block.id, false));
    QVERIFY(!document.findBlock(block.id)->metadata.contains("checked"));
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

  void jsonRoundTrip() {
    Document document;

    Block heading = Block::create(BlockType::Heading, "Title");
    heading.headingLevel = 3;
    heading.metadata.insert("collapsed", true);

    Block paragraph = Block::create(BlockType::Paragraph, "Hello **world**");

    QVERIFY(document.appendBlock(heading));
    QVERIFY(document.appendBlock(paragraph));

    Document loaded;
    QString error;
    QVERIFY2(loaded.fromJson(document.toJson(), &error), qPrintable(error));

    QCOMPARE(loaded.count(), 2);
    QCOMPARE(loaded.blockAt(0)->id, heading.id);
    QCOMPARE(loaded.blockAt(0)->type, BlockType::Heading);
    QCOMPARE(loaded.blockAt(0)->headingLevel, 3);
    QCOMPARE(loaded.blockAt(0)->metadata.value("collapsed").toBool(), true);
    QCOMPARE(loaded.blockAt(1)->id, paragraph.id);
    QCOMPARE(loaded.blockAt(1)->text, QString("Hello **world**"));
  }

  void jsonIsHumanReadable() {
    Document document;
    QVERIFY(document.appendBlock(Block::create(BlockType::Paragraph, "Hi")));

    const QByteArray data =
        QJsonDocument(document.toJson()).toJson(QJsonDocument::Indented);

    QVERIFY(data.contains("\"format\": \"templnote\""));
    QVERIFY(data.contains("\"version\": 1"));
    QVERIFY(data.contains("\"type\": \"paragraph\""));
  }

  void jsonPreservesParentIds() {
    Document document;

    Block parent = Block::create(BlockType::Paragraph, "Parent");
    Block child = Block::create(BlockType::Paragraph, "Child");
    child.parentId = parent.id;

    QVERIFY(document.appendBlock(parent));
    QVERIFY(document.appendBlock(child));

    Document loaded;
    QVERIFY(loaded.fromJson(document.toJson()));
    QCOMPARE(loaded.blockAt(1)->parentId, parent.id);
  }

  void jsonRejectsDuplicateIds() {
    QJsonObject block{
        {"id", "same-id"}, {"type", "paragraph"}, {"text", "One"}};
    QJsonObject duplicate = block;
    duplicate.insert("text", "Two");

    const QJsonObject json{{"format", "templnote"},
                           {"version", 1},
                           {"blocks", QJsonArray{block, duplicate}}};

    Document document;
    QVERIFY(document.appendBlock(Block::create()));

    QString error;
    QVERIFY(!document.fromJson(json, &error));
    QVERIFY(!error.isEmpty());
    // The failed parse must not touch the open document.
    QCOMPARE(document.count(), 1);
  }

  void jsonRejectsBlockWithoutId() {
    const QJsonObject json{
        {"format", "templnote"},
        {"version", 1},
        {"blocks", QJsonArray{QJsonObject{{"type", "paragraph"},
                                          {"text", "No id"}}}}};

    Document document;
    QVERIFY(!document.fromJson(json));
  }

  void jsonRejectsNewerVersion() {
    const QJsonObject json{{"format", "templnote"},
                           {"version", 99},
                           {"blocks", QJsonArray{}}};

    Document document;
    QString error;
    QVERIFY(!document.fromJson(json, &error));
    QVERIFY(error.contains("newer"));
  }

  void jsonUnknownBlockTypeFallsBack() {
    const QJsonObject json{
        {"format", "templnote"},
        {"version", 1},
        {"blocks", QJsonArray{QJsonObject{{"id", "abc"},
                                          {"type", "future-block"},
                                          {"text", "Keep me"}}}}};

    Document document;
    QVERIFY(document.fromJson(json));
    QCOMPARE(document.count(), 1);
    QCOMPARE(document.blockAt(0)->type, BlockType::Paragraph);
    QCOMPARE(document.blockAt(0)->text, QString("Keep me"));
  }

  void jsonAllowsEmptyDocument() {
    Document document;
    QVERIFY(document.appendBlock(Block::create()));

    Document loaded;
    QVERIFY(loaded.fromJson(document.toJson()));

    Document empty;
    const QJsonObject json{{"format", "templnote"},
                           {"version", 1},
                           {"blocks", QJsonArray{}}};
    QVERIFY(empty.fromJson(json));
    QCOMPARE(empty.count(), 0);
  }

  void imageBlockMetadataRoundTrip() {
    Document document;
    Block block = Block::create(BlockType::Image, "A caption");
    QVERIFY(document.appendBlock(block));
    QVERIFY(document.updateBlockImageSource(block.id, "file:///path/to/test.png"));
    QVERIFY(document.updateBlockImageWidthRatio(block.id, 0.75));

    QJsonObject json = document.toJson();
    Document loaded;
    QVERIFY(loaded.fromJson(json));
    QCOMPARE(loaded.count(), 1);
    const Block *loadedBlock = loaded.blockAt(0);
    QCOMPARE(loadedBlock->type, BlockType::Image);
    QCOMPARE(loadedBlock->text, QString("A caption"));
    QCOMPARE(loadedBlock->metadata.value("imageSource").toString(), QString("file:///path/to/test.png"));
    QCOMPARE(loadedBlock->metadata.value("imageWidthRatio").toDouble(), 0.75);
  }
};

QTEST_GUILESS_MAIN(DocumentTests)

#include "tst_document.moc"
