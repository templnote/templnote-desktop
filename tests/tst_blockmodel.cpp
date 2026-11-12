
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
};

QTEST_GUILESS_MAIN(BlockModelTests)

#include "tst_blockmodel.moc"
