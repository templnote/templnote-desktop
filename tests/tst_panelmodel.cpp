#include "adapter/PanelListModel.h"

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTest>
#include <QVariantMap>

using namespace Templnote::AI;

class PanelModelTests : public QObject {
  Q_OBJECT

private slots:
  void countIsNotifiedAndDataUsesRoles() {
    PanelListModel model({QStringLiteral("role"), QStringLiteral("text")});
    QSignalSpy countSpy(&model, &PanelListModel::countChanged);
    QCOMPARE(model.rowCount(), 0);

    QVariantMap first;
    first.insert(QStringLiteral("role"), QStringLiteral("user"));
    first.insert(QStringLiteral("text"), QStringLiteral("hello"));
    model.append(first);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(model.data(model.index(0), Qt::UserRole + 1).toString(),
             QStringLiteral("hello"));

    QVariantMap second;
    second.insert(QStringLiteral("role"), QStringLiteral("assistant"));
    second.insert(QStringLiteral("text"), QStringLiteral("world"));
    model.append(second);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(countSpy.count(), 2);

    QVariantMap update;
    update.insert(QStringLiteral("text"), QStringLiteral("streamed"));
    model.updateRow(1, update);
    QCOMPARE(model.data(model.index(1), Qt::UserRole + 1).toString(),
             QStringLiteral("streamed"));
    QCOMPARE(countSpy.count(), 2); // editing a row does not change count

    model.setRows({first});
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(countSpy.count(), 3);
  }

  void qmlSeesCountAndRows() {
    PanelListModel model({QStringLiteral("id"), QStringLiteral("title")});
    QVariantMap row;
    row.insert(QStringLiteral("id"), QStringLiteral("s1"));
    row.insert(QStringLiteral("title"), QStringLiteral("Chat"));
    model.append(row);

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("listModel"), &model);
    QQmlComponent component(&engine);
    component.setData(
        QByteArrayLiteral(
            "import QtQuick 2.15\n"
            "Item {\n"
            "  property int observedCount: listModel.count\n"
            "  property string firstTitle: listModel.row(0).title\n"
            "}\n"),
        QUrl());
    QObject *object = component.create();
    QVERIFY2(object, qPrintable(component.errorString()));
    QCOMPARE(object->property("observedCount").toInt(), 1);
    QCOMPARE(object->property("firstTitle").toString(), QStringLiteral("Chat"));
    delete object;
  }

  void roleNamesMatchDeclaredRoles() {
    PanelListModel model({QStringLiteral("id"), QStringLiteral("title")});
    const QHash<int, QByteArray> names = model.roleNames();
    QCOMPARE(names.value(Qt::UserRole), QByteArray("id"));
    QCOMPARE(names.value(Qt::UserRole + 1), QByteArray("title"));
    QCOMPARE(model.row(0).value(QStringLiteral("id")).toString(), QString());
  }
};

QTEST_GUILESS_MAIN(PanelModelTests)
#include "tst_panelmodel.moc"
