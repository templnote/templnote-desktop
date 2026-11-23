
#include <QtTest>

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>

#include "../src/core/CodeHighlighter.h"
#include "core/Document.h"

using namespace Templnote;

namespace {
QColor charColorAt(const QTextDocument &document, int position) {
  const QTextBlock block = document.findBlock(position);

  if (!block.isValid() || !block.layout())
    return {};

  // QSyntaxHighlighter stores its formats in the block layout, not in the
  // document's text fragments, so inspect the layout ranges directly.
  const int offset = position - block.position();

  for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
    if (offset >= range.start && offset < range.start + range.length)
      return range.format.foreground().color();
  }

  return {};
}
} // namespace

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

  void multiLineCommentDelimitersInsideStrings() {
    const QColor stringColor("#98c379");
    const QColor commentColor("#7f848e");

    // `/*` inside a quoted string must not open a multi-line comment: the next
    // line has to be highlighted normally instead of as a comment body.
    {
      QTextDocument document;
      document.setPlainText(
          QStringLiteral("auto s = \"/*\";\nint x = 5;"));

      CodeHighlighter highlighter;
      highlighter.setLanguage(QStringLiteral("cpp"));
      highlighter.setDocument(&document);
      highlighter.rehighlight();

      const int start = document.toPlainText().indexOf(QStringLiteral("/*"));
      QVERIFY(start >= 0);
      QCOMPARE(charColorAt(document, start), stringColor);

      const int keyword = document.toPlainText().indexOf(QStringLiteral("int"));
      QVERIFY(keyword > start);
      QCOMPARE(charColorAt(document, keyword), QColor("#e5c07b"));
    }

    // Conversely, a `*/` inside a string must not end a comment that started
    // on a previous line.
    {
      QTextDocument document;
      document.setPlainText(QStringLiteral(
          "/* begin\nauto s = \"*/\";\nstill comment */"));

      CodeHighlighter highlighter;
      highlighter.setLanguage(QStringLiteral("cpp"));
      highlighter.setDocument(&document);
      highlighter.rehighlight();

      const int still =
          document.toPlainText().indexOf(QStringLiteral("still"));
      QVERIFY(still >= 0);
      QCOMPARE(charColorAt(document, still), commentColor);
    }
  }

  void singleLineMarkerDoesNotHideMultiLineCommentEnd() {
    const QColor commentColor("#7f848e");

    QTextDocument document;
    document.setPlainText(QStringLiteral(
        "/* start\n// foo */\nint after = 1;"));

    CodeHighlighter highlighter;
    highlighter.setLanguage(QStringLiteral("cpp"));
    highlighter.setDocument(&document);
    highlighter.rehighlight();

    const QString text = document.toPlainText();

    // The whole second line is block-comment text; its `//` marker must not
    // mask the `*/` that closes the comment.
    const int marker = text.indexOf(QStringLiteral("// foo"));
    QVERIFY(marker >= 0);
    QCOMPARE(charColorAt(document, marker), commentColor);

    // The third line must be ordinary code. An `after` identifier has no rule
    // of its own, so a block-comment range covering it is the thing to reject.
    const int after = text.indexOf(QStringLiteral("after"));
    QVERIFY(after >= 0);
    QVERIFY(charColorAt(document, after) != commentColor);
    QCOMPARE(document.findBlock(after).userState(), 0);
  }

  void lineCommentInsideOpeningBlockCommentDoesNotKeepItOpen() {
    const QColor commentColor("#7f848e");

    QTextDocument document;
    document.setPlainText(
        QStringLiteral("/* a // b */\nint after = 1;"));

    CodeHighlighter highlighter;
    highlighter.setLanguage(QStringLiteral("cpp"));
    highlighter.setDocument(&document);
    highlighter.rehighlight();

    const QString text = document.toPlainText();

    // The `//` is inside a block comment that opens on this very line. It must
    // not become a single-line comment range that masks the closing `*/`.
    const int marker = text.indexOf(QStringLiteral("// b"));
    QVERIFY(marker >= 0);
    QCOMPARE(charColorAt(document, marker), commentColor);

    // The next line must be ordinary code with a clean state.
    const int after = text.indexOf(QStringLiteral("after"));
    QVERIFY(after >= 0);
    QVERIFY(charColorAt(document, after) != commentColor);
    QCOMPARE(document.findBlock(after).userState(), 0);

    // A `//` that genuinely follows a closed block comment still behaves like
    // a line comment and does not reopen the block state.
    QTextDocument trailing;
    trailing.setPlainText(
        QStringLiteral("/* a */ // trailing\nint after = 2;"));

    CodeHighlighter trailingHighlighter;
    trailingHighlighter.setLanguage(QStringLiteral("cpp"));
    trailingHighlighter.setDocument(&trailing);
    trailingHighlighter.rehighlight();

    const QString trailingText = trailing.toPlainText();
    const int trailingMarker = trailingText.indexOf(QStringLiteral("trailing"));
    QVERIFY(trailingMarker >= 0);
    QCOMPARE(charColorAt(trailing, trailingMarker), commentColor);

    const int trailingAfter = trailingText.indexOf(QStringLiteral("after"));
    QVERIFY(trailingAfter >= 0);
    QVERIFY(charColorAt(trailing, trailingAfter) != commentColor);
    QCOMPARE(trailing.findBlock(trailingAfter).userState(), 0);
  }

  void trailingLineCommentOnClosingLineDoesNotReopenComment() {
    const QColor commentColor("#7f848e");

    QTextDocument document;
    document.setPlainText(QStringLiteral(
        "/* open\nclose */ // start /* fake\nline3 code\nline4 code"));

    CodeHighlighter highlighter;
    highlighter.setLanguage(QStringLiteral("cpp"));
    highlighter.setDocument(&document);
    highlighter.rehighlight();

    const QString text = document.toPlainText();
    const int fake = text.indexOf(QStringLiteral("/* fake"));
    QVERIFY(fake >= 0);
    QCOMPARE(charColorAt(document, fake), commentColor);

    // The block comment closed on line 2; the `/*` inside its trailing line
    // comment must not grey out the rest of the file.
    const int line3 = text.indexOf(QStringLiteral("line3 code"));
    QVERIFY(line3 >= 0);
    QVERIFY(charColorAt(document, line3) != commentColor);
    QCOMPARE(document.findBlock(line3).userState(), 0);
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

QTEST_MAIN(DocumentTests)

#include "tst_document.moc"
