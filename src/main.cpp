#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QClipboard>
#include <QSettings>
#include <QtQml>
#include <QUrl>
#include <qguiapplication.h>
#include <qqmlapplicationengine.h>

#include "adapter/AgentService.h"
#include "adapter/PluginCatalogKey.h"
#include "ai/ProviderRegistry.h"
#include "ai/SecretStore.h"
#include "core/CodeHighlighter.h"
#include "emoji/EmojiDatabase.h"
#include "model/BlockModel.h"
#include "bridge/LiveBridge.h"
#include "engine/ChangeFeed.h"
#include "engine/EditorSession.h"
#include "plugins/NetworkGuard.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginHost.h"
#include "plugins/PluginStoreClient.h"
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
  Templnote::AI::ProviderRegistry providerRegistry;
  providerRegistry.detectLocalProviders();
  ClipboardHelper clipboardHelper;
  Templnote::EmojiDatabase emojiDatabase;

  // The store client uses its own network manager, so it is unaffected by the
  // plugin QML network guard.
  Templnote::Plugins::PluginStoreClient pluginStore;
  pluginStore.setManager(&pluginManager);
  const QString catalogOverride = qEnvironmentVariable("TEMPLNOTE_PLUGIN_CATALOG_URL");
  pluginStore.setCatalogUrl(QUrl(catalogOverride.isEmpty()
      ? QStringLiteral("https://raw.githubusercontent.com/templnote/templnote-plugins/gh-pages/catalog.json")
      : catalogOverride));
  pluginStore.setPublicKeyPem(
      QByteArray(Templnote::Adapter::kPluginCatalogPublicKey));
  pluginStore.setHostTags({QStringLiteral("qml-desktop"), QStringLiteral("linux")});
  pluginStore.setHostVersion(QStringLiteral("0.1.0"));

  Templnote::Plugins::NetworkGuardFactory networkGuard;
  QQmlApplicationEngine engine;
  engine.setNetworkAccessManagerFactory(&networkGuard);

  // One editor model, shared by the UI and the in-process AI agent. Creating it
  // in C++ lets the agent seed its preview session from the live document.
  auto *blockModel = new Templnote::BlockModel(&engine);
  blockModel->setAutosaveEnabled(true);
  auto *agentService = new Templnote::AI::AgentService(
      blockModel, &pluginManager, &providerRegistry, &engine);

  engine.rootContext()->setContextProperty("ClipboardService", &clipboardHelper);
  engine.rootContext()->setContextProperty("EmojiDatabase", &emojiDatabase);
  engine.rootContext()->setContextProperty("pluginManager", &pluginManager);
  engine.rootContext()->setContextProperty("pluginHost", &pluginHost);
  engine.rootContext()->setContextProperty("pluginStore", &pluginStore);
  engine.rootContext()->setContextProperty("providerManager", &providerRegistry);
  engine.rootContext()->setContextProperty("blockModel", blockModel);
  engine.rootContext()->setContextProperty("agentService", agentService);

  engine.loadFromModule("Templnote", "Main");

  if (engine.rootObjects().isEmpty())
    return -1;

  return app.exec();
}

#include "main.moc"
