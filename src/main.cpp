#include <QGuiApplication>
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

#include "ai/AgentService.h"
#include "ai/ProviderRegistry.h"
#include "ai/SecretStore.h"
#include "core/CodeHighlighter.h"
#include "emoji/EmojiDatabase.h"
#include "model/BlockModel.h"
#include "bridge/LiveBridge.h"
#include "engine/ChangeFeed.h"
#include "engine/EditorSession.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginHost.h"
#include "search/SearchIndex.h"
#include "search/SearchResultsModel.h"
#include "storage/AppPaths.h"
#include "storage/DocumentListModel.h"
#include "tracking/MemoryTracker.h"

namespace {

// Plugin QML shares the application's QML engine. Its network calls must not
// become an exfiltration or local-file-reading channel: only in-app resources
// and loopback services are reachable from QML. Provider traffic uses C++
// QNetworkAccessManager directly and is unaffected.
class GuardedNetworkReply : public QNetworkReply {
public:
  GuardedNetworkReply(const QNetworkRequest &request, QObject *parent)
      : QNetworkReply(parent) {
    setRequest(request);
    setUrl(request.url());
    setOperation(QNetworkAccessManager::GetOperation);
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 403);
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    setError(QNetworkReply::ContentAccessDenied,
             QStringLiteral("Blocked by Templnote QML network policy"));
    QTimer::singleShot(0, this, [this] {
      emit errorOccurred(QNetworkReply::ContentAccessDenied);
      emit finished();
    });
  }
  void abort() override {}
  qint64 bytesAvailable() const override { return 0; }
protected:
  qint64 readData(char *, qint64) override { return -1; }
};

class GuardedNetworkAccessManager : public QNetworkAccessManager {
public:
  using QNetworkAccessManager::QNetworkAccessManager;

protected:
  QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                               QIODevice *outgoingData = nullptr) override {
    const QUrl url = request.url();
    const QString scheme = url.scheme().toLower();
    const QString host = url.host().toLower();
    const bool allowed =
        scheme == QStringLiteral("qrc") || scheme == QStringLiteral("data") ||
        ((scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) &&
         (host == QStringLiteral("127.0.0.1") || host == QStringLiteral("localhost") ||
          host == QStringLiteral("::1")));
    if (!allowed)
      return new GuardedNetworkReply(request, this);
    return QNetworkAccessManager::createRequest(operation, request, outgoingData);
  }
};

class GuardedNetworkFactory : public QQmlNetworkAccessManagerFactory {
public:
  QNetworkAccessManager *create(QObject *parent) override {
    return new GuardedNetworkAccessManager(parent);
  }
};

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

  GuardedNetworkFactory networkGuard;
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
  engine.rootContext()->setContextProperty("providerManager", &providerRegistry);
  engine.rootContext()->setContextProperty("blockModel", blockModel);
  engine.rootContext()->setContextProperty("agentService", agentService);

  engine.loadFromModule("Templnote", "Main");

  if (engine.rootObjects().isEmpty())
    return -1;

  return app.exec();
}

#include "main.moc"
