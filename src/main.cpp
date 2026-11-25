#include <QGuiApplication>
#include <QLoggingCategory>
#include <QWindow>
#include <QQmlApplicationEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QClipboard>
#include <QSettings>
#include <QtQml>
#include <qguiapplication.h>
#include <qqmlapplicationengine.h>

#include "core/CodeHighlighter.h"
#include "emoji/EmojiDatabase.h"
#include "model/BlockModel.h"
#include "bridge/LiveBridge.h"
#include "control/ControlServer.h"
#include "engine/ChangeFeed.h"
#include "engine/EditorSession.h"
#include "plugins/NetworkGuard.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginHost.h"
#include "search/SearchIndex.h"
#include "search/SearchResultsModel.h"
#include "storage/AppPaths.h"
#include "storage/DocumentListModel.h"
#include "store/DocumentStore.h"
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

// Exposes QWindow's native move/resize operations to the frameless QML title
// bar so dragging and edge-resizing keep Aero Snap and Windows window behaviour.
class WindowControls : public QObject {
  Q_OBJECT
public:
  explicit WindowControls(QObject *parent = nullptr) : QObject(parent) {}

  Q_INVOKABLE void startMove(QObject *object) {
    if (auto *w = qobject_cast<QWindow *>(object))
      w->startSystemMove();
  }

  Q_INVOKABLE void startResize(QObject *object, int edges) {
    if (auto *w = qobject_cast<QWindow *>(object))
      w->startSystemResize(static_cast<Qt::Edges>(edges));
  }
};
}

int main(int argc, char *argv[]) {
  // The native Windows style does not allow overriding control backgrounds or
  // content items; Fusion does, and keeps the custom theme working.
  if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
    qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");

  // Qt 6.12's QQmlSettings emits a false-positive connect warning on Windows.
  QLoggingCategory::setFilterRules(QStringLiteral("qt.core.qobject.connect=false"));

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
  qmlRegisterType<Templnote::LiveBridge>("Templnote", 1, 0, "LiveBridge");
  qmlRegisterType<Templnote::Engine::EditorSession>("Templnote", 1, 0,
                                                    "EditorSession");
  qmlRegisterType<Templnote::Engine::ChangeFeed>("Templnote", 1, 0,
                                                 "ChangeFeed");
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
  Templnote::PluginHost pluginHost(&pluginManager);
  ClipboardHelper clipboardHelper;
  WindowControls windowControls;
  Templnote::EmojiDatabase emojiDatabase;

  // The live bridge and its store must outlive the QML engine that references
  // them through context properties.
  Templnote::Store::DocumentStore documentStore;
  Templnote::LiveBridge liveBridge;
  Templnote::ControlServer aiControl(&liveBridge,
                                     Templnote::AppPaths::configDirectory());

  Templnote::Plugins::NetworkGuardFactory networkGuard;
  QQmlApplicationEngine engine;
  engine.setNetworkAccessManagerFactory(&networkGuard);

  // The editor model is created in C++ so it can be shared with the engine
  // bridge and outlive the QML engine that references it.
  auto *blockModel = new Templnote::BlockModel(&engine);
  blockModel->setAutosaveEnabled(true);

  liveBridge.setStore(&documentStore);
  liveBridge.setBlockModel(blockModel);

  engine.rootContext()->setContextProperty("ClipboardService", &clipboardHelper);
  engine.rootContext()->setContextProperty("windowControls", &windowControls);
  engine.rootContext()->setContextProperty("EmojiDatabase", &emojiDatabase);
  engine.rootContext()->setContextProperty("pluginManager", &pluginManager);
  engine.rootContext()->setContextProperty("pluginHost", &pluginHost);
  engine.rootContext()->setContextProperty("blockModel", blockModel);
  engine.rootContext()->setContextProperty("liveBridge", &liveBridge);

  engine.loadFromModule("Templnote", "Main");

  if (engine.rootObjects().isEmpty())
    return -1;

  // Harness control is always available: loopback-only and token-protected,
  // with the token written to ai-control.json in the app config directory.
  QString controlError;
  if (!aiControl.start(0, &controlError))
    qWarning() << "AI control endpoint failed to start:" << controlError;
  else
    qInfo() << "AI control endpoint:" << aiControl.port()
            << "(token in ai-control.json)";

  return app.exec();
}

#include "main.moc"
