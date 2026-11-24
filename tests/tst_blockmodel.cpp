
#include <QtTest>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QUrl>

#include "../src/model/BlockModel.h"
#include "../src/engine/ChangeFeed.h"
#include "../src/storage/AppPaths.h"

using namespace Templnote;

namespace {
QJsonObject readDocumentJson(const QString &path) {
  QFile file(path);

  if (!file.open(QIODevice::ReadOnly))
    return {};

  return QJsonDocument::fromJson(file.readAll()).object();
}

QString attachmentManifestPath() {
  return QDir(AppPaths::configDirectory())
      .filePath(QStringLiteral("attachment-documents.json"));
}

QStringList manifestPaths() {
  QFile file(attachmentManifestPath());

  if (!file.open(QIODevice::ReadOnly))
    return {};

  QStringList paths;

  for (const QJsonValue &value :
       QJsonDocument::fromJson(file.readAll()).array())
    paths.append(value.toString());

  return paths;
}

// The attachment sweep skips its work when the cache is empty, so drop a
// placeholder file in the cache to exercise registry writes in tests.
void touchAttachmentCacheFile() {
  const QString directory = AppPaths::attachmentsDirectory();

  if (directory.isEmpty())
    return;

  QFile file(QDir(directory).filePath(QStringLiteral("placeholder.png")));

  if (file.open(QIODevice::WriteOnly))
    file.write("placeholder");
}
} // namespace

class BlockModelTests : public QObject {
  Q_OBJECT

private slots:

  void initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("TemplnoteTest");
    QCoreApplication::setApplicationName("TemplnoteTest");
  }

  void initialBlocks() {
    BlockModel model;

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.count(), 2);
  }

  void engineSeesHumanEdits() {
    BlockModel model;
    auto *engine = model.engineSession();
    QVERIFY(engine);
    const int r0 = engine->revision();
    Engine::ChangeFeed feed(engine, &model);

    const QString id = model.blockIdAt(0);
    QVERIFY(!id.isEmpty());
    QVERIFY(model.updateBlockText(id, QStringLiteral("Human typed this")));
    QCOMPARE(model.document().findBlock(id)->text,
             QStringLiteral("Human typed this"));
    QCOMPARE(engine->document().findBlock(id)->text,
             QStringLiteral("Human typed this"));
    QVERIFY(engine->revision() > r0);
    QCOMPARE(feed.count(), 1);
    QCOMPARE(feed.events().first().patch.actor.kind, Engine::Actor::Kind::Human);

    QVERIFY(model.changeBlockType(id, static_cast<int>(BlockType::Heading), 2));
    QCOMPARE(engine->document().findBlock(id)->type, BlockType::Heading);
    QCOMPARE(feed.count(), 2);
  }

  void streamedTextDoesNotMarkDirty() {
    BlockModel model;
    const QString id = model.blockIdAt(0);

    int changes = 0;
    connect(&model, &QAbstractItemModel::dataChanged, &model,
            [&changes] { ++changes; });

    QVERIFY(model.applyStreamedText(id, QStringLiteral("Hel")));
    QVERIFY(model.applyStreamedText(id, QStringLiteral("Hello")));
    QCOMPARE(model.document().findBlock(id)->text, QStringLiteral("Hello"));
    QVERIFY(changes >= 2);
    QVERIFY(!model.isModified());

    model.setAgentBusy(true);
    QVERIFY(model.agentBusy());
    model.setAgentBusy(false);
    QVERIFY(!model.agentBusy());
  }

  void engineAndModelShareDocument() {
    BlockModel model;
    const QString id = model.blockIdAt(0);
    model.updateBlockText(id, QStringLiteral("shared"));
    QCOMPARE(&model.document(), &model.engineSession()->document());
    QCOMPARE(model.engineSession()->document().findBlock(id)->text,
             QStringLiteral("shared"));
  }

  void remoteEnginePatchReachesModel() {
    BlockModel model;
    auto *engine = model.engineSession();
    const QString id = model.blockIdAt(0);
    const auto result = engine->apply(Engine::Command::setText(
        id, QStringLiteral("From agent")));
    QVERIFY(result.ok());
    // LiveBridge pushes engine patches via applyRemoteDocument; simulate it.
    QVERIFY(model.applyRemoteDocument(engine->toJson(), result.value().toJson()));
    QCOMPARE(model.document().findBlock(id)->text, QStringLiteral("From agent"));
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
    QCOMPARE(roles.value(BlockModel::AlignmentRole), QByteArray("alignment"));
    QCOMPARE(roles.value(BlockModel::CheckedRole), QByteArray("checked"));
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

  void undoMoveBlock() {
    BlockModel model;

    const QString firstId = model.document().blockAt(0)->id;
    const QString secondId = model.document().blockAt(1)->id;
    const QString thirdId = model.appendBlock(0, "Third");

    QVERIFY(model.moveBlock(thirdId, 0));
    QCOMPARE(model.document().blockAt(0)->id, thirdId);

    QVERIFY(model.undo());
    QCOMPARE(model.document().blockAt(0)->id, firstId);
    QCOMPARE(model.document().blockAt(1)->id, secondId);
    QCOMPARE(model.document().blockAt(2)->id, thirdId);
    QCOMPARE(model.lastHistoryBlockId(), thirdId);

    QVERIFY(model.redo());
    QCOMPARE(model.document().blockAt(0)->id, thirdId);
    QCOMPARE(model.lastHistoryBlockId(), thirdId);
  }

  void structuralUndoDoesNotResetModel() {
    BlockModel model;

    const QString id = model.document().blockAt(0)->id;
    const QString original = model.document().blockAt(0)->text;
    const int originalCount = model.count();

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy removedSpy(&model, &QAbstractItemModel::rowsRemoved);

    QVERIFY(!model.splitBlock(id, 5, original).isEmpty());
    QVERIFY(model.undo());

    QCOMPARE(model.count(), originalCount);
    QCOMPARE(model.document().blockAt(0)->text, original);
    QCOMPARE(model.lastHistoryCursorPosition(), -1);
    QCOMPARE(resetSpy.count(), 0);
    QVERIFY(removedSpy.count() > 0);

    QVERIFY(model.redo());
    QCOMPARE(model.count(), originalCount + 1);
    QCOMPARE(model.document().blockAt(0)->text, original.left(5));
    QCOMPARE(resetSpy.count(), 0);
  }

  void undoAppendAndInsert() {
    BlockModel model;

    const int originalCount = model.count();
    const QString appended = model.appendBlock(0, "Appended");
    QVERIFY(model.insertBlock(1, 0, "Inserted"));
    const QString inserted = model.document().blockAt(1)->id;

    QCOMPARE(model.count(), originalCount + 2);

    QVERIFY(model.undo());
    QCOMPARE(model.count(), originalCount + 1);
    QCOMPARE(model.document().indexOf(inserted), -1);

    QVERIFY(model.undo());
    QCOMPARE(model.count(), originalCount);
    QCOMPARE(model.document().indexOf(appended), -1);

    QVERIFY(model.redo());
    QCOMPARE(model.document().blockAt(model.count() - 1)->id, appended);

    QVERIFY(model.redo());
    QCOMPARE(model.document().blockAt(1)->id, inserted);
  }

  void undoRemoveBlock() {
    BlockModel model;

    const QString id = model.document().blockAt(1)->id;
    const QString text = model.document().blockAt(1)->text;

    QVERIFY(model.removeBlock(id));
    QCOMPARE(model.count(), 1);

    QVERIFY(model.undo());
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.document().blockAt(1)->id, id);
    QCOMPARE(model.document().blockAt(1)->text, text);

    QVERIFY(model.redo());
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.document().indexOf(id), -1);
  }

  void undoMergeAndChangeType() {
    BlockModel model;

    const QString secondId = model.document().blockAt(1)->id;
    const QString secondText = model.document().blockAt(1)->text;

    QVERIFY(model.mergeWithPrevious(secondId));
    QCOMPARE(model.count(), 1);

    QVERIFY(model.undo());
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.document().blockAt(1)->id, secondId);
    QCOMPARE(model.document().blockAt(1)->text, secondText);

    QVERIFY(model.redo());
    QCOMPARE(model.count(), 1);

    BlockModel typed;
    const QString id = typed.document().blockAt(1)->id;

    QVERIFY(typed.changeBlockType(id, static_cast<int>(BlockType::Heading), 2));
    QCOMPARE(typed.document().blockAt(1)->type, BlockType::Heading);
    QCOMPARE(typed.document().blockAt(1)->headingLevel, 2);

    QVERIFY(typed.undo());
    QCOMPARE(typed.document().blockAt(1)->type, BlockType::Paragraph);
    QCOMPARE(typed.document().blockAt(1)->headingLevel, 0);

    QVERIFY(typed.redo());
    QCOMPARE(typed.document().blockAt(1)->type, BlockType::Heading);
    QCOMPARE(typed.document().blockAt(1)->headingLevel, 2);
  }

  void historyIsCapped() {
    BlockModel model;

    const int originalCount = model.count();
    const int extra = 5;

    for (int i = 0; i < BlockModel::kMaxHistoryEntries + extra; ++i)
      QVERIFY(!model.appendBlock(0, QString("Block %1").arg(i)).isEmpty());

    int undone = 0;

    while (model.undo())
      ++undone;

    QCOMPARE(undone, BlockModel::kMaxHistoryEntries);
    QCOMPARE(model.count(), originalCount + extra);
  }

  void undoTextPunctuationBoundary() {
    BlockModel model;

    const QString id = model.document().blockAt(0)->id;
    const QString original = model.document().blockAt(0)->text;

    QString text = original;

    const auto type = [&](const QString &chunk) {
      for (const QChar character : chunk) {
        text += character;
        QVERIFY(model.updateBlockText(id, text));
      }
    };

    type("one");
    type(",");
    type("two");

    QCOMPARE(model.data(model.index(0), BlockModel::ContentRole).toString(),
             original + "one,two");

    QVERIFY(model.undo());
    QCOMPARE(model.data(model.index(0), BlockModel::ContentRole).toString(),
             original + "one,");

    QVERIFY(model.undo());
    QCOMPARE(model.data(model.index(0), BlockModel::ContentRole).toString(),
             original);
  }

  void pasteIsItsOwnHistoryStep() {
    BlockModel model;

    const QString id = model.document().blockAt(0)->id;
    const QString original = model.document().blockAt(0)->text;

    QVERIFY(model.updateBlockText(id, original + "pasted"));
    QVERIFY(model.updateBlockText(id, original + "pastedX"));

    QVERIFY(model.undo());
    QCOMPARE(model.data(model.index(0), BlockModel::ContentRole).toString(),
             original + "pasted");

    QVERIFY(model.undo());
    QCOMPARE(model.data(model.index(0), BlockModel::ContentRole).toString(),
             original);
  }

  void historyReportsCursorPositionForTextEdits() {
    BlockModel model;

    const QString id = model.document().blockAt(0)->id;
    const QString original = model.document().blockAt(0)->text;
    const int position = 4;

    QString edited = original;
    edited.insert(position, "XY");

    QVERIFY(model.updateBlockText(id, edited));

    QVERIFY(model.undo());
    QCOMPARE(model.lastHistoryCursorPosition(), position);

    QVERIFY(model.redo());
    QCOMPARE(model.lastHistoryCursorPosition(), position + 2);

    BlockModel deleting;
    const QString deleteId = deleting.document().blockAt(0)->id;
    QString shortened = original;
    shortened.remove(position, 2);

    QVERIFY(deleting.updateBlockText(deleteId, shortened));

    QVERIFY(deleting.undo());
    QCOMPARE(deleting.lastHistoryCursorPosition(), position + 2);

    QVERIFY(deleting.redo());
    QCOMPARE(deleting.lastHistoryCursorPosition(), position);
  }

  void numberedIndicesFollowMovesAndTypes() {
    BlockModel model;

    const QString first =
        model.appendBlock(static_cast<int>(BlockType::NumberedList), "One");
    const QString second =
        model.appendBlock(static_cast<int>(BlockType::NumberedList), "Two");
    const QString third =
        model.appendBlock(static_cast<int>(BlockType::NumberedList), "Three");

    const auto numberAt = [&](const QString &id) {
      return model
          .data(model.index(model.document().indexOf(id)),
                BlockModel::NumberIndexRole)
          .toInt();
    };

    QCOMPARE(numberAt(first), 1);
    QCOMPARE(numberAt(second), 2);
    QCOMPARE(numberAt(third), 3);

    QVERIFY(model.moveBlock(third, model.document().indexOf(first)));
    QCOMPARE(numberAt(third), 1);
    QCOMPARE(numberAt(first), 2);
    QCOMPARE(numberAt(second), 3);

    QVERIFY(model.changeBlockType(first, static_cast<int>(BlockType::Paragraph)));
    QCOMPARE(numberAt(third), 1);
    QCOMPARE(numberAt(first), 0);
    QCOMPARE(numberAt(second), 1);

    QVERIFY(model.undo());
    QCOMPARE(numberAt(third), 1);
    QCOMPARE(numberAt(first), 2);
    QCOMPARE(numberAt(second), 3);
  }

  void listStartUndoRedoNotifiesNumberIndex() {
    BlockModel model;

    const QString first =
        model.appendBlock(static_cast<int>(BlockType::NumberedList), "One");
    const QString second =
        model.appendBlock(static_cast<int>(BlockType::NumberedList), "Two");
    const int row = model.document().indexOf(first);

    QVariantMap metadata =
        model.data(model.index(row), BlockModel::MetadataRole).toMap();
    metadata.insert(QStringLiteral("listStart"), 5);
    QVERIFY(model.setBlockMetadata(first, metadata));

    const auto numberAt = [&](const QString &id) {
      return model
          .data(model.index(model.document().indexOf(id)),
                BlockModel::NumberIndexRole)
          .toInt();
    };

    QCOMPARE(numberAt(first), 5);
    QCOMPARE(numberAt(second), 6);

    QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);

    QVERIFY(model.undo());

    bool notified = false;

    for (const QList<QVariant> &args : std::as_const(spy)) {
      if (args.value(2).value<QList<int>>().contains(
              BlockModel::NumberIndexRole))
        notified = true;
    }

    // Update ops carry no from/to rows, so applyPatch() cannot derive this
    // notification from numberStarts; a metadata-only listStart edit must
    // emit NumberIndexRole itself.
    QVERIFY(notified);
    QCOMPARE(numberAt(first), 1);

    spy.clear();
    QVERIFY(model.redo());
    notified = false;

    for (const QList<QVariant> &args : std::as_const(spy)) {
      if (args.value(2).value<QList<int>>().contains(
              BlockModel::NumberIndexRole))
        notified = true;
    }

    QVERIFY(notified);
    QCOMPARE(numberAt(first), 5);
    QCOMPARE(numberAt(second), 6);
  }

  void nonPositiveListStartIsClamped() {
    BlockModel model;

    const QString first =
        model.appendBlock(static_cast<int>(BlockType::NumberedList), "One");
    const QString second =
        model.appendBlock(static_cast<int>(BlockType::NumberedList), "Two");
    const int row = model.document().indexOf(first);

    QVariantMap metadata =
        model.data(model.index(row), BlockModel::MetadataRole).toMap();
    metadata.insert(QStringLiteral("listStart"), 0);
    QVERIFY(model.setBlockMetadata(first, metadata));

    QCOMPARE(model.data(model.index(row), BlockModel::MetadataRole)
                 .toMap()
                 .value(QStringLiteral("listStart"))
                 .toInt(),
             1);
    QCOMPARE(model.data(model.index(row), BlockModel::NumberIndexRole).toInt(),
             1);
    QCOMPARE(model.data(model.index(model.document().indexOf(second)),
                        BlockModel::NumberIndexRole)
                 .toInt(),
             2);
  }

  void setBlockMetadataWithTextIsOneUndoStep() {
    BlockModel model;

    const QString id = model.appendBlock(
        static_cast<int>(BlockType::Paragraph), QStringLiteral("before"));
    const int row = model.document().indexOf(id);

    QVariantMap metadata =
        model.data(model.index(row), BlockModel::MetadataRole).toMap();
    metadata.insert(QStringLiteral("align"), QStringLiteral("center"));

    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy revisionSpy(&model, &BlockModel::contentRevisionChanged);

    QVERIFY(model.setBlockMetadataWithText(id, metadata,
                                           QStringLiteral("after")));
    QCOMPARE(dataSpy.count(), 1);
    QCOMPARE(revisionSpy.count(), 1);
    QCOMPARE(model.data(model.index(row), BlockModel::ContentRole).toString(),
             QStringLiteral("after"));

    QVERIFY(model.undo());
    QCOMPARE(model.data(model.index(row), BlockModel::ContentRole).toString(),
             QStringLiteral("before"));
    QVERIFY(model.data(model.index(row), BlockModel::MetadataRole)
                .toMap()
                .value(QStringLiteral("align"))
                .toString()
                .isEmpty());

    QVERIFY(model.redo());
    QCOMPARE(model.data(model.index(row), BlockModel::ContentRole).toString(),
             QStringLiteral("after"));
    QCOMPARE(model.data(model.index(row), BlockModel::MetadataRole)
                 .toMap()
                 .value(QStringLiteral("align"))
                 .toString(),
             QStringLiteral("center"));
  }

  void splitBlockRejectsInvalidPositionWithoutEditingText() {
    BlockModel model;

    const QString id = model.appendBlock(
        static_cast<int>(BlockType::Paragraph), QStringLiteral("original"));
    const int row = model.document().indexOf(id);
    const int count = model.count();
    const int revision = model.contentRevision();

    QVERIFY(model.splitBlock(id, 999, QStringLiteral("editor text")).isEmpty());
    QCOMPARE(model.data(model.index(row), BlockModel::ContentRole).toString(),
             QStringLiteral("original"));
    QCOMPARE(model.count(), count);
    QCOMPARE(model.contentRevision(), revision);
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

  void saveDocumentRoundTrip() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;

    const QString id =
        model.appendBlock(static_cast<int>(BlockType::Heading), "Saved title");
    QVERIFY(model.changeBlockType(id, static_cast<int>(BlockType::Heading), 2));

    const QString base = directory.filePath("My Notes");
    const QString expected =
        QFileInfo(base + ".templnote").absoluteFilePath();

    QVERIFY(model.saveDocument(base));
    QVERIFY(QFile::exists(expected));
    QCOMPARE(model.currentFilePath(), expected);
    QCOMPARE(model.documentTitle(), QString("My Notes"));
    QVERIFY(!model.isModified());

    QFile file(expected);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray data = file.readAll();
    QVERIFY(data.contains("\"format\": \"templnote\""));
    QVERIFY(data.contains("\"title\": \"My Notes\""));

    BlockModel loaded;
    QVERIFY(loaded.loadDocument(base));
    QCOMPARE(loaded.count(), model.count());
    QCOMPARE(loaded.currentFilePath(), expected);
    QVERIFY(!loaded.isModified());

    const int last = loaded.count() - 1;
    QCOMPARE(loaded.document().blockAt(last)->text, QString("Saved title"));
    QCOMPARE(loaded.document().blockAt(last)->type, BlockType::Heading);
    QCOMPARE(loaded.document().blockAt(last)->headingLevel, 2);
  }

  void saveCreatesParentDirectory() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;
    const QString path = directory.filePath("nested/deeper/Note");

    QVERIFY(model.saveDocument(path));
    QVERIFY(QFile::exists(path + ".templnote"));
  }

  void saveOverwritesExistingDocument() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;
    const QString path = directory.filePath("Overwrite");

    QVERIFY(model.saveDocument(path));
    const QString id = model.appendBlock(0, "Second save");
    QVERIFY(!id.isEmpty());
    QVERIFY(model.saveDocument(path));

    BlockModel loaded;
    QVERIFY(loaded.loadDocument(path));
    QCOMPARE(loaded.count(), model.count());
    QCOMPARE(loaded.document().blockAt(loaded.count() - 1)->text,
             QString("Second save"));
  }

  void loadDocumentAsyncAppliesOnModelThread() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel seed;
    QVERIFY(seed.createDocument(directory.filePath(QStringLiteral("Async"))));
    const QString path = seed.currentFilePath();

    BlockModel model;
    QSignalSpy finished(&model, &BlockModel::documentLoadFinished);

    bool callbackOk = false;
    model.loadDocumentAsync(path,
                            [&callbackOk](bool ok) { callbackOk = ok; });

    QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 4000);
    QVERIFY(callbackOk);
    QVERIFY(finished.first().at(0).toBool());
    QCOMPARE(model.currentFilePath(), path);
    QCOMPARE(model.count(), 1);
    QVERIFY(!model.isModified());
  }

  void prewarmedParseCanBeAppliedSynchronously() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel seed;
    QVERIFY(seed.createDocument(directory.filePath(QStringLiteral("Prewarm"))));
    const QString path = seed.currentFilePath();

    BlockModel model;
    model.prewarmDocument(path);
    QVERIFY(model.loadDocument(path));
    QCOMPARE(model.currentFilePath(), path);
    QCOMPARE(model.count(), 1);
  }

  void loadInvalidDocumentLeavesCurrentDocumentUntouched() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString path = directory.filePath("Broken.templnote");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("this is not json");
    file.close();

    BlockModel model;
    const QString beforeId = model.document().blockAt(0)->id;
    const int beforeCount = model.count();

    QVERIFY(!model.loadDocument(path));
    QVERIFY(!model.lastError().isEmpty());
    QCOMPARE(model.count(), beforeCount);
    QCOMPARE(model.document().blockAt(0)->id, beforeId);
    QVERIFY(model.currentFilePath().isEmpty());
  }

  void loadMissingDocumentFails() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;
    const int beforeCount = model.count();

    QVERIFY(!model.loadDocument(directory.filePath("Missing.templnote")));
    QCOMPARE(model.count(), beforeCount);
    QVERIFY(!model.lastError().isEmpty());
  }

  void dirtyStateFollowsEditsAndSaves() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;
    QVERIFY(!model.isModified());

    const QString id = model.appendBlock(0, "Changed");
    QVERIFY(model.isModified());

    QVERIFY(model.saveDocument(directory.filePath("Dirty")));
    QVERIFY(!model.isModified());

    QVERIFY(model.updateBlockText(id, "Changed again"));
    QVERIFY(model.isModified());
  }

  void undoAndRedoTrackSavedState() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;
    QVERIFY(model.createDocument(directory.filePath("State")));
    QVERIFY(!model.isModified());

    const QString id = model.blockIdAt(0);
    QVERIFY(model.updateBlockText(id, QStringLiteral("changed")));
    QVERIFY(model.isModified());

    QVERIFY(model.undo());
    QCOMPARE(model.document().blockAt(0)->text, QString());
    QVERIFY(!model.isModified());

    QVERIFY(model.redo());
    QCOMPARE(model.document().blockAt(0)->text, QStringLiteral("changed"));
    QVERIFY(model.isModified());
  }

  void saveClosesTextGroupSoUndoReturnsToSavedState() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;
    QVERIFY(model.createDocument(directory.filePath("Group")));
    const QString id = model.blockIdAt(0);

    QVERIFY(model.updateBlockText(id, QStringLiteral("a")));
    QVERIFY(model.saveCurrentDocument());
    QVERIFY(!model.isModified());

    QVERIFY(model.updateBlockText(id, QStringLiteral("ab")));
    QVERIFY(model.isModified());

    // The save must not be merged into the next undo step.
    QVERIFY(model.undo());
    QCOMPARE(model.document().blockAt(0)->text, QStringLiteral("a"));
    QVERIFY(!model.isModified());
  }

  void applyingAgentSnapshotRestoresDirtyState() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;
    QVERIFY(model.createDocument(directory.filePath(QStringLiteral("Agent"))));
    QVERIFY(!model.isModified());

    QJsonObject json = model.document().toJson();
    QJsonArray blocks = json.value(QStringLiteral("blocks")).toArray();
    QVERIFY(!blocks.isEmpty());

    QJsonObject block = blocks.first().toObject();
    block.insert(QStringLiteral("text"), QStringLiteral("agent text"));
    blocks[0] = block;
    json.insert(QStringLiteral("blocks"), blocks);

    QVERIFY(model.applyAgentDocument(json, QJsonObject{}));
    QCOMPARE(model.document().blockAt(0)->text, QStringLiteral("agent text"));
    // The engine-applied snapshot resets the saved-state ids; the model must
    // still restore the dirty flag so autosave writes the agent edit back.
    QVERIFY(model.isModified());
  }

  void noOpSaveSkipsAttachmentSweep() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;
    const QString doc = directory.filePath(QStringLiteral("NoOp"));
    QVERIFY(model.saveDocument(doc));

    const QString orphan =
        QDir(AppPaths::attachmentsDirectory())
            .filePath(QStringLiteral("orphan-") +
                      QUuid::createUuid().toString(QUuid::WithoutBraces) +
                      QStringLiteral(".png"));
    {
      QFile file(orphan);
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("x");
    }
    QVERIFY(QFile::exists(orphan));

    // An unchanged save does not sweep attachments.
    QVERIFY(model.saveDocument(doc));
    QVERIFY(QFile::exists(orphan));

    // The next save that changes content does sweep and reclaim the orphan.
    QVERIFY(!model.appendBlock(0).isEmpty());
    QVERIFY(model.saveDocument(doc));
    QVERIFY(!QFile::exists(orphan));
  }

  void autosaveWritesAfterIdle() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;
    const QString path = directory.filePath("Auto");
    QVERIFY(model.createDocument(path));

    model.setAutosaveEnabled(true);

    const QString id = model.blockIdAt(0);
    QVERIFY(!id.isEmpty());
    QVERIFY(model.updateBlockText(id, QStringLiteral("autosaved text")));
    QVERIFY(model.isModified());

    // The debounced timer must persist the edit and clear the dirty flag.
    QTRY_VERIFY_WITH_TIMEOUT(!model.isModified(), 4000);

    QFile file(directory.filePath("Auto.templnote"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readAll().contains("autosaved text"));
  }

  void saveCurrentDocumentRequiresAPath() {
    BlockModel model;

    QVERIFY(!model.saveCurrentDocument());
    QVERIFY(!model.lastError().isEmpty());
  }

  void renameDocumentRoundTrip() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;
    const QString oldPath = directory.filePath("My Notes");
    QVERIFY(model.saveDocument(oldPath));

    const QJsonObject before = readDocumentJson(oldPath + ".templnote");
    QVERIFY(!before.isEmpty());

    QVERIFY(model.renameDocument("Ideas"));

    const QString newPath =
        QFileInfo(directory.filePath("Ideas.templnote")).absoluteFilePath();

    QVERIFY(QFile::exists(newPath));
    QVERIFY(!QFile::exists(oldPath + ".templnote"));
    QCOMPARE(model.currentFilePath(), newPath);
    QCOMPARE(model.documentTitle(), QString("Ideas"));
    QCOMPARE(model.lastError(), QString());

    const QJsonObject after = readDocumentJson(newPath);
    QCOMPARE(after.value("title").toString(), QString("Ideas"));
    QCOMPARE(after.value("modifiedAt").toString(),
             before.value("modifiedAt").toString());
  }

  void renameCollisionGetsSuffix() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;
    QVERIFY(model.saveDocument(directory.filePath("First")));
    QVERIFY(model.saveDocument(directory.filePath("Second")));
    QVERIFY(model.loadDocument(directory.filePath("First")));

    QVERIFY(model.renameDocument("Second"));
    QCOMPARE(model.documentTitle(), QString("Second 2"));
    QVERIFY(QFile::exists(directory.filePath("Second 2.templnote")));
    QVERIFY(QFile::exists(directory.filePath("Second.templnote")));
    QVERIFY(!QFile::exists(directory.filePath("First.templnote")));
  }

  void renameInvalidNameFallsBackToUntitled() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;
    QVERIFY(model.saveDocument(directory.filePath("Notes")));
    QVERIFY(model.renameDocument("///"));

    QCOMPARE(model.documentTitle(), QString("Untitled"));
    QVERIFY(QFile::exists(directory.filePath("Untitled.templnote")));
  }

  void renameStripsTypedExtension() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;
    QVERIFY(model.saveDocument(directory.filePath("Notes")));
    QVERIFY(model.renameDocument("Ideas.templnote"));

    QCOMPARE(model.documentTitle(), QString("Ideas"));
    QVERIFY(QFile::exists(directory.filePath("Ideas.templnote")));
    QVERIFY(!QFile::exists(directory.filePath("Ideas.templnote.templnote")));
  }

  void renameToSameNameIsNoOp() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;
    QVERIFY(model.saveDocument(directory.filePath("Notes")));
    const QString path = model.currentFilePath();

    QVERIFY(model.renameDocument("Notes"));
    QCOMPARE(model.currentFilePath(), path);
    QVERIFY(QFile::exists(path));
  }

  void renameWithoutAPathFails() {
    BlockModel model;

    QVERIFY(!model.renameDocument("Anything"));
    QVERIFY(!model.lastError().isEmpty());
  }

  void adoptRenamedPathKeepsUnsavedEdits() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;
    QVERIFY(model.saveDocument(directory.filePath("Draft")));
    const QString oldPath = model.currentFilePath();

    QVERIFY(model.updateBlockText(model.blockIdAt(0), "unsaved text"));

    // Simulate the explorer renaming the parent folder on disk.
    const QString newPath =
        QFileInfo(directory.filePath("Moved.templnote")).absoluteFilePath();
    QVERIFY(QFile::rename(oldPath, newPath));

    model.adoptRenamedPath(newPath);

    QCOMPARE(model.currentFilePath(), newPath);
    QCOMPARE(model.documentTitle(), QString("Moved"));
    QVERIFY(model.isModified());

    QVERIFY(model.saveCurrentDocument());
    QVERIFY(QFile::exists(newPath));
    QVERIFY(!QFile::exists(oldPath));
    QCOMPARE(model.data(model.index(0, 0), BlockModel::ContentRole).toString(),
             QString("unsaved text"));
  }

  void documentFolderIsRelativeToTemplnote() {
    const QString documents = AppPaths::ensureDocumentsDirectory();
    QVERIFY(!documents.isEmpty());

    QTemporaryDir nested(QDir(documents).filePath("header-test-XXXXXX"));
    QVERIFY(nested.isValid());

    const QString folder = QDir(nested.path()).filePath("Projects/2026");
    QVERIFY(QDir().mkpath(folder));

    BlockModel model;
    QVERIFY(model.saveDocument(QDir(folder).filePath("Plan")));
    QCOMPARE(model.documentFolder(), QDir(documents).relativeFilePath(folder));

    // A document directly in the Templnote folder has no folder label.
    const QString rootPath = QDir(documents).filePath(
        QStringLiteral("Root-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    QVERIFY(model.saveDocument(rootPath));
    QCOMPARE(model.documentFolder(), QString());
    QFile::remove(model.currentFilePath());

    // A document outside the Templnote folder also has no folder label.
    QTemporaryDir outside;
    QVERIFY(outside.isValid());
    QVERIFY(model.saveDocument(outside.filePath("Elsewhere")));
    QCOMPARE(model.documentFolder(), QString());
  }

  void lastModifiedComesFromJson() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;
    QVERIFY(model.saveDocument(directory.filePath("Stamp")));

    const QJsonObject json =
        readDocumentJson(directory.filePath("Stamp.templnote"));
    const QDateTime expected = QDateTime::fromString(
        json.value("modifiedAt").toString(), Qt::ISODate);

    QVERIFY(expected.isValid());
    QCOMPARE(model.lastModified().toUTC().toString(Qt::ISODate),
             expected.toUTC().toString(Qt::ISODate));

    BlockModel loaded;
    QVERIFY(loaded.loadDocument(directory.filePath("Stamp")));
    QCOMPARE(loaded.lastModified().toUTC().toString(Qt::ISODate),
             expected.toUTC().toString(Qt::ISODate));
  }

  void lastModifiedFallsBackToFileTime() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString path = directory.filePath("Legacy.templnote");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{\"format\":\"templnote\",\"version\":1,\"blocks\":[]}");
    file.close();

    BlockModel model;
    QVERIFY(model.loadDocument(path));
    QVERIFY(model.lastModified().isValid());
  }

  void createDocumentPersistsPendingEdits() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;
    const QString firstPath = directory.filePath("First");
    QVERIFY(model.createDocument(firstPath));

    const QString id = model.blockIdAt(0);
    QVERIFY(!id.isEmpty());
    QVERIFY(model.updateBlockText(id, QStringLiteral("pending edit")));
    QVERIFY(model.isModified());

    const QString secondPath = directory.filePath("Second");
    QVERIFY(model.createDocument(secondPath));
    QVERIFY(!model.isModified());

    QFile firstFile(firstPath + ".templnote");
    QVERIFY(firstFile.open(QIODevice::ReadOnly));
    QVERIFY(firstFile.readAll().contains("pending edit"));
  }

  void createDocumentSavesUntitledFile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;
    const QString path = directory.filePath("Untitled");

    QVERIFY(model.createDocument(path));
    QCOMPARE(model.count(), 1);
    QVERIFY(QFile::exists(path + ".templnote"));
    QCOMPARE(model.documentTitle(), QString("Untitled"));
    QVERIFY(!model.isModified());
    QVERIFY(model.document().blockAt(0)->text.isEmpty());
  }

  void createDocumentPersistsEditsFromNeverSavedDocument() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;
    QVERIFY(model.closeDocument());

    const QString id = model.appendBlock(0, QStringLiteral("untitled content"));
    QVERIFY(!id.isEmpty());
    QVERIFY(model.isModified());
    QVERIFY(model.currentFilePath().isEmpty());

    QVERIFY(model.createDocument(directory.filePath("Fresh")));
    QCOMPARE(model.documentTitle(), QStringLiteral("Fresh"));
    QVERIFY(!model.isModified());

    // The old edits must survive in a fresh Untitled file rather than being
    // refused or silently discarded.
    QString recovered;
    QDirIterator it(AppPaths::documentsDirectory(),
                    QStringList{QStringLiteral("Untitled*.templnote")},
                    QDir::Files);

    while (it.hasNext()) {
      const QString path = it.next();
      QFile file(path);

      if (file.open(QIODevice::ReadOnly) &&
          file.readAll().contains("untitled content")) {
        recovered = path;
        break;
      }
    }

    QVERIFY(!recovered.isEmpty());
    QFile::remove(recovered);
  }

  void blocksOfColumnCacheTracksStructuralEdits() {
    BlockModel model;
    model.createColumnsLayout(0, 2);
    const QStringList columns = model.columnIdsOf(model.blockIdAt(0));
    QCOMPARE(columns.size(), 2);
    QCOMPARE(model.blocksOfColumn(columns.first()).size(), 1);

    const QString added = model.appendBlockToColumn(columns.first());
    QCOMPARE(model.blocksOfColumn(columns.first()).size(), 2);
    QVERIFY(model.removeBlock(added));
    QCOMPARE(model.blocksOfColumn(columns.first()).size(), 1);

    QVERIFY(!model.appendBlock(0, QStringLiteral("top level")).isEmpty());
    QCOMPARE(model.blocksOfColumn(columns.first()).size(), 1);
  }

  void attachmentCacheReclaimsUnreferencedFiles() {
    QTemporaryDir sourceDirectory;
    QTemporaryDir documentDirectory;
    QVERIFY(sourceDirectory.isValid());
    QVERIFY(documentDirectory.isValid());

    const QString source = sourceDirectory.filePath("source.png");
    {
      QFile file(source);
      QVERIFY(file.open(QIODevice::WriteOnly));
      QVERIFY(file.write("image data") > 0);
    }

    BlockModel model;
    QVERIFY(model.closeDocument());

    const QString url = model.importAttachment(QUrl::fromLocalFile(source));
    QVERIFY(url.startsWith(QStringLiteral("file://")));
    const QString cached = QUrl(url).toLocalFile();
    QVERIFY(QFile::exists(cached));

    // Nothing references the attachment, so saving reclaims it.
    QVERIFY(model.saveDocument(documentDirectory.filePath("Cleanup")));
    QVERIFY(!QFile::exists(cached));

    // A referenced attachment must survive the same sweep.
    const QString keptUrl = model.importAttachment(QUrl::fromLocalFile(source));
    const QString kept = QUrl(keptUrl).toLocalFile();
    QVERIFY(QFile::exists(kept));

    const QString blockId =
        model.appendBlock(static_cast<int>(BlockType::Image));
    QVERIFY(model.setBlockImageSource(blockId, keptUrl));
    QVERIFY(model.saveDocument(documentDirectory.filePath("Keep")));
    QVERIFY(QFile::exists(kept));

    QFile::remove(kept);
  }

  void attachmentOutsideDocumentsRootSurvivesClose() {
    QTemporaryDir sourceDirectory;
    QTemporaryDir outsideDirectory;
    QVERIFY(sourceDirectory.isValid());
    QVERIFY(outsideDirectory.isValid());

    const QString source = sourceDirectory.filePath("source.png");
    {
      QFile file(source);
      QVERIFY(file.open(QIODevice::WriteOnly));
      QVERIFY(file.write("image data") > 0);
    }

    BlockModel model;
    QVERIFY(model.closeDocument());

    const QString url = model.importAttachment(QUrl::fromLocalFile(source));
    const QString cached = QUrl(url).toLocalFile();
    QVERIFY(QFile::exists(cached));

    const QString blockId =
        model.appendBlock(static_cast<int>(BlockType::Image));
    QVERIFY(model.setBlockImageSource(blockId, url));

    // Save outside the default documents folder: the old sweep scanned only
    // that folder plus the currently open path and deleted the image.
    QVERIFY(model.saveDocument(outsideDirectory.filePath("Outside")));
    QVERIFY(QFile::exists(cached));

    // Unrelated saves in a second model must not reclaim the attachment.
    {
      BlockModel other;
      QVERIFY(other.closeDocument());
      QVERIFY(other.saveDocument(outsideDirectory.filePath("Unrelated")));
    }

    QVERIFY(QFile::exists(cached));
    QFile::remove(cached);
  }

  void twoLiveModelsDoNotOverwriteAttachmentPaths() {
    QTemporaryDir outside;
    QVERIFY(outside.isValid());

    const QString manifest = attachmentManifestPath();
    QFile::remove(manifest);

    const QString docB = outside.filePath("B.templnote");
    const QString docA = outside.filePath("A.templnote");
    const QString docB2 = outside.filePath("B2.templnote");

    // B caches the manifest while it is still empty.
    touchAttachmentCacheFile();
    BlockModel modelB;
    QVERIFY(modelB.saveDocument(docB));
    QVERIFY(manifestPaths().contains(QDir::cleanPath(docB)));

    // A saves after B's first read and registers a path B never saw.
    touchAttachmentCacheFile();
    BlockModel modelA;
    QVERIFY(modelA.saveDocument(docA));

    // B saves another document; reloading the manifest before writing keeps A.
    touchAttachmentCacheFile();
    QVERIFY(modelB.saveDocument(docB2));

    const QStringList paths = manifestPaths();
    QVERIFY(paths.contains(QDir::cleanPath(docA)));
    QVERIFY(paths.contains(QDir::cleanPath(docB2)));

    QFile::remove(manifest);
  }

  void attachmentManifestPrunesDeletedDocuments() {
    QTemporaryDir outside;
    QVERIFY(outside.isValid());

    const QString manifest = attachmentManifestPath();
    QFile::remove(manifest);

    const QString alive = outside.filePath("Alive.templnote");
    const QString gone = outside.filePath("Gone.templnote");

    for (const QString &path : {alive, gone}) {
      QFile file(path);
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("{}");
    }

    // Seed the manifest with both documents, then remove one behind the app's
    // back so its entry is stale when the manifest is next read.
    {
      QJsonArray paths{QDir::cleanPath(alive), QDir::cleanPath(gone)};
      QFile file(manifest);
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write(QJsonDocument(paths).toJson(QJsonDocument::Compact));
    }
    QVERIFY(QFile::remove(gone));

    touchAttachmentCacheFile();
    BlockModel model;
    QVERIFY(model.saveDocument(alive));

    const QStringList paths = manifestPaths();
    QVERIFY(paths.contains(QDir::cleanPath(alive)));
    QVERIFY(!paths.contains(QDir::cleanPath(gone)));

    QFile::remove(manifest);
  }

  void attachmentManifestSkipsUnchangedWrite() {
    QTemporaryDir outside;
    QVERIFY(outside.isValid());

    const QString manifest = attachmentManifestPath();
    QFile::remove(manifest);

    const QString doc = outside.filePath("Unchanged.templnote");
    touchAttachmentCacheFile();

    BlockModel model;
    QVERIFY(model.saveDocument(doc));
    QVERIFY(manifestPaths().contains(QDir::cleanPath(doc)));

    // Store a byte-different but set-equivalent manifest. If the next save
    // rewrites an unchanged set, this sentinel is clobbered by the compact
    // JSON form the production code writes.
    const QByteArray sentinel =
        QJsonDocument(QJsonArray{QDir::cleanPath(doc)})
            .toJson(QJsonDocument::Indented);
    {
      QFile file(manifest);
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write(sentinel);
    }

    touchAttachmentCacheFile();
    QVERIFY(model.saveDocument(doc));

    QFile file(manifest);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), sentinel);

    QFile::remove(manifest);
  }

  void saveSweepRegistersCurrentPathWithoutPriorManifest() {
    QTemporaryDir outside;
    QVERIFY(outside.isValid());

    const QString manifest = attachmentManifestPath();
    QFile::remove(manifest);

    // The sweep now relies on rememberAttachmentDocumentPath() as its single
    // manifest read/merge. A fresh model with no manifest on disk must still
    // end up registering the path it just saved.
    touchAttachmentCacheFile();
    BlockModel model;
    const QString doc = outside.filePath("Registered.templnote");
    QVERIFY(model.saveDocument(doc));

    const QStringList paths = manifestPaths();
    QVERIFY(paths.contains(QDir::cleanPath(doc)));

    QFile::remove(manifest);
  }

  void renameAndCloseRegisterAttachmentPaths() {
    QTemporaryDir outside;
    QVERIFY(outside.isValid());

    const QString manifest = attachmentManifestPath();
    QFile::remove(manifest);

    BlockModel model;
    const QString original = outside.filePath("Original.templnote");
    touchAttachmentCacheFile();
    QVERIFY(model.saveDocument(original));

    QVERIFY(model.renameDocument(QStringLiteral("Renamed")));
    const QString renamed = outside.filePath("Renamed.templnote");
    QVERIFY(manifestPaths().contains(QDir::cleanPath(renamed)));

    // Simulate another writer replacing the manifest, then close. The close
    // path must re-register its document before the path is cleared.
    QFile::remove(manifest);
    QVERIFY(model.closeDocument());
    QVERIFY(manifestPaths().contains(QDir::cleanPath(renamed)));

    QFile::remove(manifest);
  }

  void markdownTextImageReferenceSurvivesSweep() {
    QTemporaryDir sourceDirectory;
    QTemporaryDir documentDirectory;
    QVERIFY(sourceDirectory.isValid());
    QVERIFY(documentDirectory.isValid());

    const QString source = sourceDirectory.filePath("source.png");
    {
      QFile file(source);
      QVERIFY(file.open(QIODevice::WriteOnly));
      QVERIFY(file.write("image data") > 0);
    }

    BlockModel model;
    QVERIFY(model.closeDocument());

    const QString url = model.importAttachment(QUrl::fromLocalFile(source));
    const QString cached = QUrl(url).toLocalFile();
    QVERIFY(QFile::exists(cached));

    const QString blockId = model.appendBlock(0);
    QVERIFY(model.updateBlockText(
        blockId, QStringLiteral("![alt](%1)").arg(url)));

    // No imageSource metadata: the only reference is Markdown in block text.
    QVERIFY(model.saveDocument(documentDirectory.filePath("MarkdownRef")));
    QVERIFY(QFile::exists(cached));
    QFile::remove(cached);
  }

  void appendToEmptyDocumentCreatesFirstBlock() {
    BlockModel model;
    QVERIFY(model.closeDocument());
    QCOMPARE(model.count(), 0);

    const QString id = model.appendBlock(0, "");
    QVERIFY(!id.isEmpty());
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.blockIdAt(0), id);
  }

  void createColumnsLayout() {
    BlockModel model;
    const QString originalId = model.blockIdAt(0);
    const int originalCount = model.count();

    const QString focusId = model.createColumnsLayout(0, 3);

    QVERIFY(!focusId.isEmpty());
    QCOMPARE(model.count(), originalCount + 6);
    QCOMPARE(model.document().blockAt(0)->type, BlockType::Columns);
    QCOMPARE(model.document().blockAt(0)->id, originalId);

    const QStringList columns = model.columnIdsOf(originalId);
    QCOMPARE(columns.size(), 3);

    double total = 0.0;
    for (const QString &id : columns) {
      const Block *column = model.document().findBlock(id);
      QVERIFY(column);
      QCOMPARE(column->type, BlockType::Column);
      QCOMPARE(column->parentId, originalId);
      total += model.columnWidth(id);
      QCOMPARE(model.blocksOfColumn(id).size(), 1);
    }
    QVERIFY(qAbs(total - 800.0) < 0.0001);

    const Block *child = model.document().findBlock(focusId);
    QVERIFY(child);
    QCOMPARE(child->parentId, columns.first());
    QCOMPARE(model.columnIdOfBlock(focusId), columns.first());
  }

  void createColumnsLayoutFitsAvailableWidth() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);
    const QString focusId = model.createColumnsLayout(0, 3, 900.0);
    QVERIFY(!focusId.isEmpty());

    const QStringList columns = model.columnIdsOf(layoutId);
    QCOMPARE(columns.size(), 3);

    // The editor's available width is split evenly on creation.
    for (const QString &id : columns)
      QVERIFY(qFuzzyCompare(model.columnWidth(id), 300.0));

    // A single pane can still be resized afterwards without touching others.
    const double other = model.columnWidth(columns[2]);
    QVERIFY(model.setColumnWidth(columns.first(), 460.0));
    QVERIFY(qFuzzyCompare(model.columnWidth(columns.first()), 460.0));
    QVERIFY(qFuzzyCompare(model.columnWidth(columns[2]), other));
  }

  void createColumnsLayoutClampsNarrowWidth() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);
    QVERIFY(!model.createColumnsLayout(0, 3, 150.0).isEmpty());

    for (const QString &id : model.columnIdsOf(layoutId))
      QVERIFY(model.columnWidth(id) >= 80.0);
  }

  void createColumnsLayoutRejectsBadInput() {
    BlockModel model;
    const int originalCount = model.count();

    QVERIFY(model.createColumnsLayout(0, 1).isEmpty());
    QVERIFY(model.createColumnsLayout(0, 6).isEmpty());
    QVERIFY(model.createColumnsLayout(-1, 2).isEmpty());
    QCOMPARE(model.count(), originalCount);

    const QString focusId = model.createColumnsLayout(0, 2);
    QVERIFY(!focusId.isEmpty());
    QVERIFY(model.createColumnsLayout(0, 2).isEmpty());
  }

  void columnsVisibleModelAndMapping() {
    BlockModel model;
    model.createColumnsLayout(0, 2);

    const QString layoutId = model.blockIdAt(0);
    const QString childId = model.blocksOfColumn(model.columnIdsOf(layoutId)[1])[0];

    QCOMPARE(model.visibleCount(), 2);
    QCOMPARE(model.blockIdAtVisibleRow(0), layoutId);
    QCOMPARE(model.visibleRowOf(layoutId), 0);
    // A nested child maps to the visible row of its layout.
    QCOMPARE(model.visibleRowOf(childId), 0);
    QVERIFY(model.topLevelAncestorId(childId) == layoutId);
  }

  void columnsUndoRedo() {
    BlockModel model;
    const QString originalId = model.blockIdAt(0);
    const QString originalText = model.document().blockAt(0)->text;
    const int originalCount = model.count();

    model.createColumnsLayout(0, 2);
    QCOMPARE(model.document().blockAt(0)->type, BlockType::Columns);

    QVERIFY(model.undo());
    QCOMPARE(model.count(), originalCount);
    QCOMPARE(model.document().blockAt(0)->type, BlockType::Heading);
    QCOMPARE(model.document().blockAt(0)->text, originalText);
    QCOMPARE(model.document().blockAt(0)->id, originalId);

    QVERIFY(model.redo());
    QCOMPARE(model.document().blockAt(0)->type, BlockType::Columns);
    QCOMPARE(model.columnIdsOf(originalId).size(), 2);
  }

  void insertColumnKeepsExistingWidths() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);
    model.createColumnsLayout(0, 2);

    const QStringList before = model.columnIdsOf(layoutId);
    const double widthBefore = model.columnWidth(before.first());
    QVERIFY(model.insertColumn(layoutId, before.first()));

    const QStringList after = model.columnIdsOf(layoutId);
    QCOMPARE(after.size(), 3);
    QCOMPARE(after[0], before[0]);
    QCOMPARE(after[2], before[1]);
    QVERIFY(qFuzzyCompare(model.columnWidth(after[0]), widthBefore));
    QVERIFY(qFuzzyCompare(model.columnWidth(after[1]), widthBefore));
    QCOMPARE(model.blocksOfColumn(after[1]).size(), 1);
  }

  void removeColumnMergesIntoNeighbor() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);
    model.createColumnsLayout(0, 3);

    const QStringList columns = model.columnIdsOf(layoutId);
    const QString firstChild = model.blocksOfColumn(columns[0])[0];
    model.updateBlockText(firstChild, QStringLiteral("moved text"));

    QVERIFY(model.removeColumn(columns[0]));
    QCOMPARE(model.columnIdsOf(layoutId).size(), 2);
    QVERIFY(!model.document().findBlock(columns[0]));
    QCOMPARE(model.columnIdOfBlock(firstChild), columns[1]);
    QCOMPARE(model.document().findBlock(firstChild)->text,
             QStringLiteral("moved text"));

    QVERIFY(model.undo());
    QCOMPARE(model.columnIdsOf(layoutId).size(), 3);
    QCOMPARE(model.columnIdOfBlock(firstChild), columns[0]);
  }

  void removeLastColumnCollapsesToParagraph() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);
    const int countBefore = model.count();
    const QString focusId = model.createColumnsLayout(0, 2);
    Q_UNUSED(focusId)

    const QStringList columns = model.columnIdsOf(layoutId);
    const QString child = model.blocksOfColumn(columns.last())[0];
    model.updateBlockText(child, QStringLiteral("kept text"));

    QVERIFY(model.removeColumn(columns.first()));

    const Block *collapsed = model.document().blockAt(0);
    QCOMPARE(collapsed->type, BlockType::Paragraph);
    QCOMPARE(collapsed->id, layoutId);
    QVERIFY(collapsed->parentId.isEmpty());
    QVERIFY(model.columnIdsOf(layoutId).isEmpty());
    QCOMPARE(model.count(), countBefore + 2);

    QCOMPARE(model.columnIdOfBlock(child), QString());
    QCOMPARE(model.document().findBlock(child)->text,
             QStringLiteral("kept text"));
    // The merged child from the removed column precedes the surviving one.
    QCOMPARE(model.visibleRowOf(child), 2);

    QVERIFY(model.undo());
    QCOMPARE(model.document().blockAt(0)->type, BlockType::Columns);
    QCOMPARE(model.columnIdsOf(layoutId).size(), 2);
    QCOMPARE(model.columnIdOfBlock(child), columns.last());
  }

  void setColumnWidthOnlyChangesOneColumn() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);
    model.createColumnsLayout(0, 3);
    const QStringList columns = model.columnIdsOf(layoutId);

    const double otherWidth = model.columnWidth(columns[1]);

    QVERIFY(model.setColumnWidth(columns.first(), 460));
    QVERIFY(qFuzzyCompare(model.columnWidth(columns.first()), 460.0));
    QVERIFY(qFuzzyCompare(model.columnWidth(columns[1]), otherWidth));
    QVERIFY(qFuzzyCompare(model.columnWidth(columns[2]), otherWidth));

    QVERIFY(model.setColumnWidth(columns.first(), 10));
    QVERIFY(model.columnWidth(columns.first()) >= 80.0);

  }

  void moveBlockBetweenColumns() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);
    model.createColumnsLayout(0, 2);
    const QStringList columns = model.columnIdsOf(layoutId);
    const QString child = model.blocksOfColumn(columns.first())[0];

    QVERIFY(model.moveBlockToColumn(child, columns.last(), 0));
    QCOMPARE(model.columnIdOfBlock(child), columns.last());
    QVERIFY(model.blocksOfColumn(columns.first()).isEmpty());
    QCOMPARE(model.blocksOfColumn(columns.last()).size(), 2);
    QCOMPARE(model.blocksOfColumn(columns.last()).first(), child);

    QVERIFY(model.undo());
    QCOMPARE(model.columnIdOfBlock(child), columns.first());
  }

  void extractColumnToEditorPromotesChildren() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);
    model.createColumnsLayout(0, 3);

    const QStringList columns = model.columnIdsOf(layoutId);
    const QString middle = model.blocksOfColumn(columns[1])[0];
    model.updateBlockText(middle, QStringLiteral("extracted"));

    // Drop the middle column before the editor block following the layout.
    QVERIFY(model.extractColumnToEditor(columns[1], 1));

    QCOMPARE(model.columnIdsOf(layoutId).size(), 2);
    QVERIFY(!model.columnIdsOf(layoutId).contains(columns[1]));
    QVERIFY(!model.document().findBlock(columns[1]));
    QVERIFY(model.columnIdOfBlock(middle).isEmpty());
    QCOMPARE(model.document().findBlock(middle)->text,
             QStringLiteral("extracted"));
    QCOMPARE(model.blockIdAtVisibleRow(1), middle);

    QVERIFY(model.undo());
    QCOMPARE(model.columnIdsOf(layoutId).size(), 3);
    QCOMPARE(model.columnIdOfBlock(middle), columns[1]);
  }

  void extractLastColumnCollapsesLayout() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);
    model.createColumnsLayout(0, 2);

    // Leave a single-column layout by moving one column to a second layout.
    const int otherRow = model.count() - 1;
    const QString otherLayout = model.blockIdAt(otherRow);
    model.createColumnsLayout(otherRow, 2);
    const QStringList columns = model.columnIdsOf(layoutId);
    QVERIFY(model.moveColumnToLayout(columns.last(), otherLayout, 0));
    QCOMPARE(model.columnIdsOf(layoutId).size(), 1);

    const QString child = model.blocksOfColumn(columns.first())[0];
    model.updateBlockText(child, QStringLiteral("freed"));

    QVERIFY(model.extractColumnToEditor(columns.first(), 1));

    QCOMPARE(model.document().blockAt(0)->type, BlockType::Paragraph);
    QCOMPARE(model.document().blockAt(0)->id, layoutId);
    QVERIFY(model.columnIdsOf(layoutId).isEmpty());
    QVERIFY(model.columnIdOfBlock(child).isEmpty());
    QCOMPARE(model.document().findBlock(child)->text,
             QStringLiteral("freed"));

    QVERIFY(model.undo());
    QCOMPARE(model.document().blockAt(0)->type, BlockType::Columns);
    QCOMPARE(model.columnIdsOf(layoutId).size(), 1);
    QCOMPARE(model.columnIdOfBlock(child), columns.first());
  }

  void moveColumnToDifferentLayout() {
    BlockModel model;
    const QString firstLayout = model.blockIdAt(0);
    model.createColumnsLayout(0, 2);

    const int otherRow = model.count() - 1;
    const QString secondLayout = model.blockIdAt(otherRow);
    model.createColumnsLayout(otherRow, 2);

    const QString moved = model.columnIdsOf(firstLayout).last();
    const QStringList firstBefore = model.columnIdsOf(firstLayout);

    QVERIFY(model.moveColumnToLayout(moved, secondLayout, 0));
    QCOMPARE(model.document().findBlock(moved)->parentId, secondLayout);
    QCOMPARE(model.columnIdsOf(firstLayout),
             QStringList{firstBefore.first()});
    QCOMPARE(model.columnIdsOf(secondLayout).size(), 3);
    QCOMPARE(model.columnIdsOf(secondLayout).first(), moved);

    QVERIFY(model.undo());
    QCOMPARE(model.columnIdsOf(firstLayout), firstBefore);
    QCOMPARE(model.columnIdsOf(secondLayout).size(), 2);
  }

  void moveTopLevelBlockIntoColumn() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);
    model.createColumnsLayout(0, 2);

    const QStringList columns = model.columnIdsOf(layoutId);
    const QString topLevel = model.blockIdAt(model.count() - 1);
    QVERIFY(model.columnIdOfBlock(topLevel).isEmpty());

    QVERIFY(model.moveBlockToColumn(topLevel, columns.last(), 0));
    QCOMPARE(model.columnIdOfBlock(topLevel), columns.last());
    QCOMPARE(model.blocksOfColumn(columns.last()).first(), topLevel);

    QVERIFY(model.undo());
    QVERIFY(model.columnIdOfBlock(topLevel).isEmpty());
  }

  void columnActions() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);
    model.createColumnsLayout(0, 3);
    const QStringList original = model.columnIdsOf(layoutId);

    QVERIFY(model.moveColumn(original[0], 1));
    QCOMPARE(model.columnIdsOf(layoutId)[0], original[1]);
    QCOMPARE(model.columnIdsOf(layoutId)[1], original[0]);

    QVERIFY(model.equalizeColumns(layoutId));
    for (const QString &id : model.columnIdsOf(layoutId))
      QVERIFY(qFuzzyCompare(model.columnWidth(id), 800.0 / 3));

    const QString child = model.blocksOfColumn(original[0])[0];
    QVERIFY(!model.appendBlockToColumn(original[0]).isEmpty());
    QCOMPARE(model.blocksOfColumn(original[0]).size(), 2);

    const QString copyId = model.duplicateBlock(child);
    QVERIFY(!copyId.isEmpty());
    QCOMPARE(model.document().findBlock(copyId)->text,
             model.document().findBlock(child)->text);
    QVERIFY(copyId != child);

    const QString firstBefore = model.columnIdsOf(layoutId).first();
    QVERIFY(model.insertColumn(layoutId, QString()));
    QVERIFY(model.columnIdsOf(layoutId).first() != firstBefore);
    QCOMPARE(model.columnIdsOf(layoutId).size(), 4);
  }

  void resizeGestureIsSingleUndoStep() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);
    model.createColumnsLayout(0, 2);
    const QStringList columns = model.columnIdsOf(layoutId);
    const double before = model.columnWidth(columns.first());

    QVERIFY(model.beginColumnResize(layoutId));
    QVERIFY(model.setColumnWidth(columns.first(), 500));
    QVERIFY(model.setColumnWidth(columns.first(), 600));
    QVERIFY(model.setColumnWidth(columns.first(), 700));
    QVERIFY(model.endColumnResize());

    QVERIFY(qFuzzyCompare(model.columnWidth(columns.first()), 700.0));

    QVERIFY(model.undo());
    QVERIFY(qFuzzyCompare(model.columnWidth(columns.first()), before));
  }

  void moveColumnToIndexReorders() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);
    model.createColumnsLayout(0, 3);
    const QStringList original = model.columnIdsOf(layoutId);

    QVERIFY(model.moveColumnToIndex(original.first(), 2));
    QCOMPARE(model.columnIdsOf(layoutId)[2], original.first());

    QVERIFY(model.undo());
    QCOMPARE(model.columnIdsOf(layoutId)[0], original.first());
  }

  void structuralEditDoesNotReindexUnrelatedNumberedRuns() {
    BlockModel model;

    for (int i = 0; i < 10; ++i)
      model.appendBlock(static_cast<int>(BlockType::NumberedList),
                        QStringLiteral("Item %1").arg(i));

    // Duplicating a block at the end uses applyPatch(). The numbered run above
    // is separated from the edit, so no NumberIndexRole update is needed.
    const QString endId = model.appendBlock(0, QStringLiteral("end"));
    QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);

    QVERIFY(!model.duplicateBlock(endId).isEmpty());

    int numberedUpdates = 0;

    for (const QList<QVariant> &args : std::as_const(spy)) {
      const QList<int> roles = args.value(2).value<QList<int>>();

      if (roles.contains(BlockModel::NumberIndexRole))
        ++numberedUpdates;
    }

    // The old implementation rescanned from row 0 and emitted one signal per
    // numbered block in the document.
    QCOMPARE(numberedUpdates, 0);
  }

  void undoRedoBumpContentRevision() {
    BlockModel model;
    const QString id = model.blockIdAt(0);
    QSignalSpy spy(&model, &BlockModel::contentRevisionChanged);

    const int beforeEdit = model.contentRevision();
    QVERIFY(model.updateBlockText(id, QStringLiteral("changed")));
    const int afterEdit = model.contentRevision();
    QVERIFY(afterEdit > beforeEdit);

    QVERIFY(model.undo());
    const int afterUndo = model.contentRevision();
    QVERIFY(afterUndo > afterEdit);

    QVERIFY(model.redo());
    QVERIFY(model.contentRevision() > afterUndo);

    QVERIFY(spy.count() >= 3);
  }

  void movingBlockAboveLayoutKeepsColumns() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(1);
    model.createColumnsLayout(1, 2);
    const QString above = model.blockIdAt(0);

    QCOMPARE(model.columnIdsOf(layoutId).size(), 2);

    // Moving the block above the layout used to break the row-contiguity
    // assumption and make the layout appear empty.
    QVERIFY(model.moveBlock(above, 1));
    QCOMPARE(model.columnIdsOf(layoutId).size(), 2);

    for (const QString &columnId : model.columnIdsOf(layoutId))
      QCOMPARE(model.blocksOfColumn(columnId).size(), 1);
  }

  void movingLayoutKeepsItsColumns() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);
    model.createColumnsLayout(0, 2);

    QCOMPARE(model.columnIdsOf(layoutId).size(), 2);

    // Alt+Down moves only the layout row; the column wrappers used to be left
    // behind and the layout rendered empty.
    const int belowRow = model.sourceRowOfVisibleRow(1);
    QVERIFY(belowRow > 0);
    QVERIFY(model.moveBlock(layoutId, belowRow));
    QCOMPARE(model.columnIdsOf(layoutId).size(), 2);

    for (const QString &columnId : model.columnIdsOf(layoutId))
      QCOMPARE(model.blocksOfColumn(columnId).size(), 1);

    // ...and moving it back up keeps working too.
    QVERIFY(model.moveBlock(layoutId, 0));
    QCOMPARE(model.columnIdsOf(layoutId).size(), 2);
  }

  void duplicateLayoutCopiesSubtree() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);
    model.createColumnsLayout(0, 2);

    const QStringList columns = model.columnIdsOf(layoutId);
    const QString child = model.blocksOfColumn(columns.first()).first();
    QVERIFY(model.updateBlockText(child, QStringLiteral("kept")));

    const int countBefore = model.count();
    const QString copyId = model.duplicateBlock(layoutId);

    QVERIFY(!copyId.isEmpty());
    QVERIFY(copyId != layoutId);
    QCOMPARE(model.count(), countBefore + 5); // layout + 2 columns + 2 children

    const QStringList copiedColumns = model.columnIdsOf(copyId);
    QCOMPARE(copiedColumns.size(), 2);
    QVERIFY(copiedColumns.first() != columns.first());
    QVERIFY(copiedColumns.last() != columns.last());

    const QString copiedChild =
        model.blocksOfColumn(copiedColumns.first()).first();
    QVERIFY(!copiedChild.isEmpty());
    QVERIFY(copiedChild != child);
    QCOMPARE(model.document().findBlock(copiedChild)->text,
             QStringLiteral("kept"));

    // The whole duplicate is one undo step.
    QVERIFY(model.undo());
    QCOMPARE(model.count(), countBefore);
    QVERIFY(!model.document().findBlock(copyId));
  }

  void removeLayoutRemovesWholeSubtree() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);
    model.createColumnsLayout(0, 2);

    const QStringList columns = model.columnIdsOf(layoutId);
    const QString child = model.blocksOfColumn(columns.first()).first();
    const int layoutCount = model.count();

    QVERIFY(model.removeBlock(layoutId));
    QCOMPARE(model.count(), layoutCount - 5);
    QVERIFY(!model.document().findBlock(layoutId));
    QVERIFY(!model.document().findBlock(columns.first()));
    QVERIFY(!model.document().findBlock(child));

    QVERIFY(model.undo());
    QCOMPARE(model.count(), layoutCount);
    QCOMPARE(model.columnIdsOf(layoutId).size(), 2);
    QCOMPARE(model.blocksOfColumn(columns.first()).size(), 1);
  }

  void mergeAtStartOfColumnDoesNotMergeIntoWrapper() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);
    model.createColumnsLayout(0, 2);

    const QStringList columns = model.columnIdsOf(layoutId);
    const QString child = model.blocksOfColumn(columns.first()).first();
    QVERIFY(model.updateBlockText(child, QStringLiteral("Hello")));

    // Backspace at the start of the first paragraph used to move the text onto
    // the Column wrapper and delete the paragraph.
    QVERIFY(!model.mergeWithPrevious(child));

    const Block *childBlock = model.document().findBlock(child);
    QVERIFY(childBlock);
    QCOMPARE(childBlock->text, QStringLiteral("Hello"));
    QCOMPARE(model.document().findBlock(columns.first())->text, QString());
    QCOMPARE(model.blocksOfColumn(columns.first()).size(), 1);
  }

  void insertColumnRepairsEmptyLayout() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);

    // Simulate a document saved by the old duplication bug: a Columns block
    // with no Column children.
    QVERIFY(model.changeBlockType(layoutId, static_cast<int>(BlockType::Columns)));
    QVERIFY(model.columnIdsOf(layoutId).isEmpty());

    QVERIFY(model.insertColumn(layoutId, QString()));
    QCOMPARE(model.columnIdsOf(layoutId).size(), 1);
    QCOMPARE(model.blocksOfColumn(model.columnIdsOf(layoutId).first()).size(), 1);
  }

  void insertBlockAfterColumnChildStaysInColumn() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);
    model.createColumnsLayout(0, 2);

    const QStringList columns = model.columnIdsOf(layoutId);
    const QString child = model.blocksOfColumn(columns.first()).first();

    const QString newId = model.insertBlockAfter(child, 0, QString());

    QVERIFY(!newId.isEmpty());
    QCOMPARE(model.parentBlockId(newId), columns.first());
    QCOMPARE(model.blocksOfColumn(columns.first()).size(), 2);
    QCOMPARE(model.columnIdsOf(layoutId).size(), 2);
    QCOMPARE(model.visibleCount(), 2);
  }

  void movingColumnKeepsItsChildrenContiguous() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);
    model.createColumnsLayout(0, 3);

    const QStringList original = model.columnIdsOf(layoutId);
    const QString moved = original.first();
    const QString child = model.blocksOfColumn(moved).first();

    QVERIFY(model.moveColumnToIndex(moved, 2));
    QCOMPARE(model.columnIdsOf(layoutId)[2], moved);
    QCOMPARE(model.indexOfBlock(child), model.indexOfBlock(moved) + 1);

    QVERIFY(model.undo());
    QCOMPARE(model.columnIdsOf(layoutId)[0], moved);
    QCOMPARE(model.indexOfBlock(child), model.indexOfBlock(moved) + 1);
  }

  void movingColumnToOtherLayoutMovesWholeSubtree() {
    BlockModel model;
    const QString firstLayout = model.blockIdAt(0);
    model.createColumnsLayout(0, 2);

    const int otherRow = model.count() - 1;
    const QString secondLayout = model.blockIdAt(otherRow);
    model.createColumnsLayout(otherRow, 2);

    const QString moved = model.columnIdsOf(firstLayout).last();
    const QString child = model.blocksOfColumn(moved).first();

    QVERIFY(model.moveColumnToLayout(moved, secondLayout, 0));
    QCOMPARE(model.columnIdsOf(secondLayout).first(), moved);
    QCOMPARE(model.indexOfBlock(child), model.indexOfBlock(moved) + 1);

    QVERIFY(model.undo());
    QCOMPARE(model.columnIdsOf(firstLayout).last(), moved);
    QCOMPARE(model.indexOfBlock(child), model.indexOfBlock(moved) + 1);
  }

  void resizeSingleColumnLayoutIsSingleUndoStep() {
    BlockModel model;
    const QString layoutId = model.blockIdAt(0);
    model.createColumnsLayout(0, 2);

    const QStringList columns = model.columnIdsOf(layoutId);
    const int otherRow = model.count() - 1;
    const QString otherLayout = model.blockIdAt(otherRow);
    model.createColumnsLayout(otherRow, 2);
    QVERIFY(model.moveColumnToLayout(columns.last(), otherLayout, 0));
    QCOMPARE(model.columnIdsOf(layoutId).size(), 1);

    const QString single = model.columnIdsOf(layoutId).first();
    const double before = model.columnWidth(single);

    QVERIFY(model.beginColumnResize(layoutId));
    QVERIFY(model.setColumnWidth(single, 420));
    QVERIFY(model.setColumnWidth(single, 480));
    QVERIFY(model.endColumnResize());
    QVERIFY(qFuzzyCompare(model.columnWidth(single), 480.0));

    QVERIFY(model.undo());
    QVERIFY(qFuzzyCompare(model.columnWidth(single), before));
  }

  void columnsSurviveSaveLoad() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    BlockModel model;
    const QString path = directory.filePath(QStringLiteral("columns"));
    model.createColumnsLayout(0, 3);
    QVERIFY(model.saveDocument(path));

    BlockModel loaded;
    QVERIFY(loaded.loadDocument(path + QStringLiteral(".templnote")));

    const QString layoutId = loaded.blockIdAt(0);
    QCOMPARE(loaded.document().blockAt(0)->type, BlockType::Columns);
    QCOMPARE(loaded.columnIdsOf(layoutId).size(), 3);
    for (const QString &id : loaded.columnIdsOf(layoutId))
      QCOMPARE(loaded.blocksOfColumn(id).size(), 1);
    QCOMPARE(loaded.visibleCount(), 2);
  }
};

QTEST_GUILESS_MAIN(BlockModelTests)

#include "tst_blockmodel.moc"
