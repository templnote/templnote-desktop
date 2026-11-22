#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QClipboard>
#include <QSettings>
#include <QtQml>
#include <qguiapplication.h>
#include <qqmlapplicationengine.h>

#include "core/CodeHighlighter.h"
#include "emoji/EmojiDatabase.h"
#include "model/BlockModel.h"
#include "plugins/PluginManager.h"
#include "search/SearchIndex.h"
#include "search/SearchResultsModel.h"
#include "storage/AppPaths.h"
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

  // Honour an explicit TEMPLNOTE_CONFIG_DIR override for every QSettings-based
  // store. When the variable is unset, configDirectory() falls back to
  // AppConfigLocation - which is already Qt's default - so calling setPath()
  // with it would nest the app directory a second time
  // (~/.config/Templnote/Templnote/Templnote/Templnote.conf) and lose existing
  // preferences. Leave Qt's default untouched in that case.
  if (!qEnvironmentVariable("TEMPLNOTE_CONFIG_DIR").isEmpty()) {
    const QString configDir = Templnote::AppPaths::configDirectory();

    // setPath(NativeFormat) is ignored on macOS/Windows/iOS, where Qt always
    // uses the system registry/plist locations. Switching the default format
    // to INI makes every QSettings client - including QML's Settings singleton
    // - honour the override everywhere. Do this before constructing the engine
    // (and therefore before any QSettings is created) so no store is opened
    // against the previous default first.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, configDir);
    // Keep the NativeFormat path too: it is a no-op on the platforms above but
    // still redirects any explicit NativeFormat client on Linux.
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, configDir);
  }

  qmlRegisterType<Templnote::BlockModel>("Templnote", 1, 0, "BlockModel");
  qmlRegisterType<Templnote::DocumentListModel>("Templnote", 1, 0,
                                                "DocumentListModel");
  qmlRegisterType<Templnote::SearchIndex>("Templnote", 1, 0, "SearchIndex");
  qmlRegisterType<Templnote::SearchResultsModel>("Templnote", 1, 0,
                                                 "SearchResultsModel");
  qmlRegisterType<Templnote::CodeHighlighter>("Templnote", 1, 0, "CodeHighlighter");
  qmlRegisterType<Templnote::MemoryTracker>("Templnote", 1, 0, "MemoryTracker");

  // Declare C++ context objects before the engine that references them: Qt
  // destroys in reverse declaration order, so the engine must die first. All
  // objects exposed via setContextProperty must outlive the engine.
  Templnote::PluginManager pluginManager;
  ClipboardHelper clipboardHelper;
  Templnote::EmojiDatabase emojiDatabase;

  QQmlApplicationEngine engine;

  engine.rootContext()->setContextProperty("ClipboardService", &clipboardHelper);
  engine.rootContext()->setContextProperty("EmojiDatabase", &emojiDatabase);
  engine.rootContext()->setContextProperty("pluginManager", &pluginManager);

  engine.loadFromModule("Templnote", "Main");

  if (engine.rootObjects().isEmpty())
    return -1;

  return app.exec();
}

#include "main.moc"
