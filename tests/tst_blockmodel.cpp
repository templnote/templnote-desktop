
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include "../src/model/BlockModel.h"
#include "../src/storage/AppPaths.h"

using namespace Templnote;

namespace {
QJsonObject readDocumentJson(const QString &path) {
  QFile file(path);

  if (!file.open(QIODevice::ReadOnly))
    return {};

  return QJsonDocument::fromJson(file.readAll()).object();
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
};

QTEST_GUILESS_MAIN(BlockModelTests)

#include "tst_blockmodel.moc"
