#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QClipboard>
#include <QtQml>
#include <qguiapplication.h>
#include <qqmlapplicationengine.h>

#include "core/CodeHighlighter.h"
#include "model/BlockModel.h"
#include "search/SearchIndex.h"
#include "search/SearchResultsModel.h"
#include "storage/DocumentListModel.h"
#include "tracking/MemoryTracker.h"

namespace {
class ClipboardHelper : public QObject {
  Q_OBJECT
public:
  explicit ClipboardHelper(QObject *parent = nullptr) : QObject(parent) {}
  Q_INVOKABLE void setText(const QString &text) {
    QGuiApplication::clipboard()->setText(text);
  }
};
}

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
  qmlRegisterType<Templnote::CodeHighlighter>("Templnote", 1, 0, "CodeHighlighter");
  qmlRegisterType<Templnote::MemoryTracker>("Templnote", 1, 0, "MemoryTracker");

  QQmlApplicationEngine engine;

  ClipboardHelper clipboardHelper;
  engine.rootContext()->setContextProperty("ClipboardService", &clipboardHelper);

  engine.loadFromModule("Templnote", "Main");

  if (engine.rootObjects().isEmpty())
    return -1;

  return app.exec();
}

#include "main.moc"
