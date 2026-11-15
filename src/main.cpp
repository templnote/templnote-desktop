#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQml>
#include <qguiapplication.h>
#include <qqmlapplicationengine.h>

#include "model/BlockModel.h"
#include "search/SearchIndex.h"
#include "search/SearchResultsModel.h"
#include "storage/DocumentListModel.h"

int main(int argc, char *argv[]) {
  QGuiApplication app(argc, argv);

  QGuiApplication::setApplicationName("Templnote");
  QGuiApplication::setOrganizationName("Templnote");

  qmlRegisterType<Templnote::BlockModel>("Templnote", 1, 0, "BlockModel");
  qmlRegisterType<Templnote::DocumentListModel>("Templnote", 1, 0,
                                                "DocumentListModel");
  qmlRegisterType<Templnote::SearchIndex>("Templnote", 1, 0, "SearchIndex");
  qmlRegisterType<Templnote::SearchResultsModel>("Templnote", 1, 0,
                                                 "SearchResultsModel");

  QQmlApplicationEngine engine;

  engine.loadFromModule("Templnote", "Main");

  if (engine.rootObjects().isEmpty())
    return -1;

  return app.exec();
}
