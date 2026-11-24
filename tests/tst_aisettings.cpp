#include "ai/AgentRegistry.h"
#include "ai/ProviderRegistry.h"
#include "ai/SecretStore.h"
#include "plugins/PluginManager.h"

#include <QEventLoop>
#include <QFile>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStringList>
#include <QUrl>
#include <QHostAddress>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QTest>
#include <QVariant>

#include <memory>

using namespace Templnote::AI;

namespace {

class MockModelsServer : public QTcpServer {
public:
  QString baseUrl() const {
    return QStringLiteral("http://127.0.0.1:") + QString::number(serverPort());
  }

  void setBody(const QByteArray &body) { m_body = body; }

private:
  void incomingConnection(qintptr handle) override {
    auto *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(handle)) {
      socket->deleteLater();
      return;
    }
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
      socket->readAll();
      const QByteArray body = m_body;
      socket->write(QByteArray("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                               "Content-Length: ") +
                    QByteArray::number(body.size()) +
                    "\r\nConnection: close\r\n\r\n" + body);
      socket->flush();
      socket->disconnectFromHost();
    });
    connect(socket, &QTcpSocket::disconnected, socket,
            &QTcpSocket::deleteLater);
  }

  QByteArray m_body = R"({"data":[{"id":"model-a"},{"id":"model-b"}]})";
};

// Repeater delegates are parented in the visual tree only, so QObject's
// findChildren() cannot see them. Walk childItems() instead.
QList<QObject *> findItems(QQuickItem *parent, const QString &objectName = {}) {
  QList<QObject *> result;
  if (!parent)
    return result;
  for (QQuickItem *child : parent->childItems()) {
    if (objectName.isEmpty() || child->objectName() == objectName)
      result.append(child);
    result.append(findItems(child, objectName));
  }
  return result;
}

QObject *findItem(QQuickItem *parent, const QString &objectName) {
  const QList<QObject *> found = findItems(parent, objectName);
  return found.isEmpty() ? nullptr : found.first();
}

// A registry created without an explicit SecretStore uses the real OS
// keychain. Connecting a mock provider with a test key would then overwrite
// the user's stored API key (for example ai.provider.google.apiKey) and make
// the desktop app send a bogus key to Google later. Always give test
// registries their own isolated store.
std::unique_ptr<SecretStore> testSecrets(const QTemporaryDir &dir) {
  auto secrets = std::make_unique<SecretStore>();
  secrets->setTestMode(true);
  secrets->setFilePath(dir.filePath(QStringLiteral("test-secrets.dat")));
  return secrets;
}

} // namespace

class AISettingsTests : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("TemplnoteAISettingsTest"));
    QCoreApplication::setApplicationName(QStringLiteral("TemplnoteAISettingsTest"));
  }

  void testSettingsPageLoads() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("TEMPLNOTE_CONFIG_DIR", dir.path().toUtf8());

    MockModelsServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    auto secrets = testSecrets(dir);
    ProviderRegistry registry(nullptr, secrets.get());
    registry.setLocalDetectionEnabled(false);
    QEventLoop loop;
    bool connected = false;
    connect(&registry, &ProviderRegistry::connected, &loop,
            [&](const QString &) {
              connected = true;
              loop.quit();
            });
    QTimer::singleShot(4000, &loop, &QEventLoop::quit);
    registry.connectProvider(QStringLiteral("custom"),
                             QStringLiteral("sk-test"), server.baseUrl());
    if (!connected)
      loop.exec();
    QVERIFY(connected);
    registry.setDefaultModelKey(
        registry.enabledModels().first().toMap()
            .value(QStringLiteral("key")).toString());

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("providerManager"),
                                             &registry);
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this,
            [&errors](const QList<QQmlError> &list) { errors.append(list); });

    engine.loadFromModule(QStringLiteral("TemplnoteAISettingsTest"),
                          QStringLiteral("AISettingsTest"));
    QCOMPARE(engine.rootObjects().size(), 1);
    if (!errors.isEmpty()) {
      for (const QQmlError &error : errors)
        qWarning() << error.toString();
    }
    QVERIFY2(errors.isEmpty(), "AISettings.qml produced QML errors");
  }

  void testToggleLabelsRender() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("TEMPLNOTE_CONFIG_DIR", dir.path().toUtf8());

    QQmlApplicationEngine engine;
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this,
            [&errors](const QList<QQmlError> &list) { errors.append(list); });

    engine.loadFromModule(QStringLiteral("TemplnoteAISettingsTest"),
                          QStringLiteral("SettingToggleTest"));
    QCOMPARE(engine.rootObjects().size(), 1);
    if (!errors.isEmpty()) {
      for (const QQmlError &error : errors)
        qWarning() << error.toString();
    }
    QVERIFY2(errors.isEmpty(), "SettingToggle.qml produced QML errors");

    QObject *toggle = engine.rootObjects().first()
                          ->property("toggle").value<QObject *>();
    QVERIFY(toggle);

    const QString title = QStringLiteral("Restore last session");
    const QString description = QStringLiteral(
        "Reopen the documents that were open when Templnote was closed.");
    bool foundTitle = false;
    bool foundDescription = false;

    const auto descendants = toggle->findChildren<QObject *>();
    for (QObject *child : descendants) {
      const QString text = child->property("text").toString();

      if (text != title && text != description)
        continue;

      QVERIFY2(child->property("visible").toBool(),
               qPrintable(text + QStringLiteral(" is not visible")));
      QVERIFY2(child->property("width").toReal() > 0,
               qPrintable(text + QStringLiteral(" collapsed to zero width")));

      if (text == title)
        foundTitle = true;
      else
        foundDescription = true;
    }

    QVERIFY2(foundTitle, "SettingToggle title is missing");
    QVERIFY2(foundDescription, "SettingToggle description is missing");
  }

  void testConnectDialog() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("TEMPLNOTE_CONFIG_DIR", dir.path().toUtf8());

    MockModelsServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    auto secrets = testSecrets(dir);
    ProviderRegistry registry(nullptr, secrets.get());
    registry.setLocalDetectionEnabled(false);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("providerManager"),
                                             &registry);
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this,
            [&errors](const QList<QQmlError> &list) { errors.append(list); });
    engine.loadFromModule(QStringLiteral("TemplnoteAISettingsTest"),
                          QStringLiteral("AISettingsTest"));
    QCOMPARE(engine.rootObjects().size(), 1);
    QObject *root = engine.rootObjects().first();
    auto *window = qobject_cast<QQuickWindow *>(root);
    QVERIFY(window);
    QQuickItem *rootItem = window->contentItem();
    QVERIFY(rootItem);
    const QList<QObject *> allItems = findItems(rootItem);

    QObject *popup = root->findChild<QObject *>(QStringLiteral("connectPopup"));
    QVERIFY(popup);
    QVERIFY(!popup->property("opened").toBool());

    // The inline API-key form is gone: Connect must open the dialog instead.
    for (QObject *child : allItems)
      QVERIFY2(child->property("placeholderText").toString() != QStringLiteral("sk-..."),
               "stale inline API key field is still present");

    QObject *customButton = nullptr;
    for (QObject *child : allItems) {
      if (child->property("accessibleName").toString() ==
          QStringLiteral("Connect Custom endpoint")) {
        customButton = child;
        break;
      }
    }
    QVERIFY(customButton);
    QVERIFY(QMetaObject::invokeMethod(customButton, "clicked"));
    QVERIFY2(popup->property("opened").toBool(), "Connect did not open the dialog");

    QObject *keyField = popup->property("keyField").value<QObject *>();
    QObject *urlField = popup->property("urlField").value<QObject *>();
    QVERIFY(keyField);
    QVERIFY(urlField);
    keyField->setProperty("text", QStringLiteral("sk-test"));
    urlField->setProperty("text", server.baseUrl());

    auto *popupContent = popup->property("contentItem").value<QQuickItem *>();
    QVERIFY(popupContent);
    QObject *continueButton = findItem(popupContent, QStringLiteral("connectContinueButton"));
    QVERIFY(continueButton);
    QVERIFY(continueButton->property("enabled").toBool());

    QEventLoop loop;
    bool connected = false;
    connect(&registry, &ProviderRegistry::connected, &loop,
            [&](const QString &) {
              connected = true;
              loop.quit();
            });
    QTimer::singleShot(4000, &loop, &QEventLoop::quit);
    QVERIFY(QMetaObject::invokeMethod(continueButton, "clicked"));
    if (!connected)
      loop.exec();
    QVERIFY(connected);
    QVERIFY(!popup->property("opened").toBool());

    // Models are not listed on the Providers page any more.
    QVERIFY2(findItem(rootItem, QStringLiteral("modelGroupRow")) == nullptr,
             "Models section must not be rendered in AI Providers");

    for (const QQmlError &error : errors)
      qWarning() << error.toString();
    QVERIFY2(errors.isEmpty(), "AI Providers page produced QML errors");
  }

  void testModelsSection() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("TEMPLNOTE_CONFIG_DIR", dir.path().toUtf8());

    MockModelsServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    auto secrets = testSecrets(dir);
    ProviderRegistry registry(nullptr, secrets.get());
    registry.setLocalDetectionEnabled(false);
    QEventLoop connectLoop;
    bool connected = false;
    connect(&registry, &ProviderRegistry::connected, &connectLoop,
            [&](const QString &) {
              connected = true;
              connectLoop.quit();
            });
    QTimer::singleShot(4000, &connectLoop, &QEventLoop::quit);
    QVERIFY(registry.connectProvider(QStringLiteral("custom"),
                                     QStringLiteral("sk-test"),
                                     server.baseUrl()));
    if (!connected)
      connectLoop.exec();
    QVERIFY(connected);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("providerManager"),
                                             &registry);
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this,
            [&errors](const QList<QQmlError> &list) { errors.append(list); });
    engine.loadFromModule(QStringLiteral("TemplnoteAISettingsTest"),
                          QStringLiteral("AIModelsTest"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    QQuickItem *rootItem = window->contentItem();

    const QList<QObject *> groups =
        findItems(rootItem, QStringLiteral("modelGroupRow"));
    QVERIFY2(!groups.isEmpty(), "connected provider has no row in Models");
    QVERIFY2(qobject_cast<QQuickItem *>(groups.first())->height() > 20,
             "collapsed provider row has no height");
    QVERIFY(findItem(rootItem, QStringLiteral("modelGroupExpander")));

    QObject *modelsRoot =
        engine.rootObjects().first()->findChild<QObject *>(QStringLiteral("aiModelsRoot"));
    QVERIFY(modelsRoot);
    QVERIFY(QMetaObject::invokeMethod(modelsRoot, "toggleProvider",
                                      Q_ARG(QVariant, QStringLiteral("custom"))));
    QTest::qWait(50);
    QVERIFY2(findItem(rootItem, QStringLiteral("modelToggle")),
             "expanding a provider did not reveal its models");

    // Search filters the groups; an unmatched query leaves no group rows.
    QObject *search = findItem(rootItem, QStringLiteral("modelsSearchField"));
    QVERIFY(search);
    search->setProperty("text", QStringLiteral("does-not-exist"));
    QTest::qWait(50);
    QVERIFY(findItems(rootItem, QStringLiteral("modelGroupRow")).isEmpty());
    search->setProperty("text", QString());
    QTest::qWait(50);

    for (const QQmlError &error : errors)
      qWarning() << error.toString();
    QVERIFY2(errors.isEmpty(), "Models page produced QML errors");
  }

  void testAgentsSection() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("TEMPLNOTE_CONFIG_DIR", dir.path().toUtf8());

    AgentRegistry registry;
    registry.load();
    registry.addAgent(QStringLiteral("Writer"),
                      QStringLiteral("Write the section."), QString());

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("agentManager"),
                                             &registry);
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this,
            [&errors](const QList<QQmlError> &list) { errors.append(list); });
    engine.loadFromModule(QStringLiteral("TemplnoteAISettingsTest"),
                          QStringLiteral("AgentsSettingsTest"));
    QCOMPARE(engine.rootObjects().size(), 1);
    QVERIFY(engine.rootObjects().first()->findChild<QObject *>(
        QStringLiteral("agentsRoot")));
    QVERIFY(findItem(qobject_cast<QQuickWindow *>(engine.rootObjects().first())
                         ->contentItem(),
                     QStringLiteral("agentNameField")));

    for (const QQmlError &error : errors)
      qWarning() << error.toString();
    QVERIFY2(errors.isEmpty(), "Agents page produced QML errors");
  }

  void testGoogleProvider() {
    QVERIFY(ProviderRegistry::definitions().contains(QStringLiteral("google")));
    const ProviderDefinition google =
        ProviderRegistry::definitions().value(QStringLiteral("google"));
    QCOMPARE(google.baseUrl,
             QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai"));
    QVERIFY(google.presetModels.contains(QStringLiteral("gemini-4-hydrogen")));
    QVERIFY(!google.presetModels.contains(QStringLiteral("gemini-2.5-flash")));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("TEMPLNOTE_CONFIG_DIR", dir.path().toUtf8());

    MockModelsServer server;
    server.setBody("{\"data\":[{\"id\":\"models/gemini-4-hydrogen\"},"
                   "{\"id\":\"models/gemini-embedding-001\"}]}");
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    auto secrets = testSecrets(dir);
    ProviderRegistry registry(nullptr, secrets.get());
    registry.setLocalDetectionEnabled(false);
    QEventLoop loop;
    bool connected = false;
    connect(&registry, &ProviderRegistry::connected, &loop,
            [&](const QString &) {
              connected = true;
              loop.quit();
            });
    QTimer::singleShot(4000, &loop, &QEventLoop::quit);
    QVERIFY(registry.connectProvider(QStringLiteral("google"),
                                     QStringLiteral("test-key"),
                                     server.baseUrl()));
    if (!connected)
      loop.exec();
    QVERIFY(connected);

    const QVariantList models = registry.modelsFor(QStringLiteral("google"));
    QVERIFY(!models.isEmpty());
    QStringList ids;
    for (const QVariant &model : models)
      ids.append(model.toMap().value(QStringLiteral("modelId")).toString());
    QVERIFY(ids.contains(QStringLiteral("gemini-4-hydrogen")));
    QVERIFY(!ids.contains(QStringLiteral("models/gemini-4-hydrogen")));
    // Non-chat models must not appear in the model picker.
    QVERIFY(!ids.contains(QStringLiteral("gemini-embedding-001")));

    // Per-model thinking levels come from the current Gemini docs.
    QCOMPARE(ProviderRegistry::thinkingLevelsFor(QStringLiteral("google"),
                                                  QStringLiteral("gemini-4-hydrogen")),
             (QStringList{QStringLiteral("low"), QStringLiteral("medium"),
                          QStringLiteral("high"), QStringLiteral("xhigh")}));
    QCOMPARE(ProviderRegistry::thinkingLevelsFor(QStringLiteral("google"),
                                                  QStringLiteral("gemini-4-carbon")),
             (QStringList{QStringLiteral("low"), QStringLiteral("medium"),
                          QStringLiteral("high")}));
    QCOMPARE(ProviderRegistry::thinkingLevelsFor(
                 QStringLiteral("google"), QStringLiteral("gemini-3.5-flash-lite")),
             (QStringList{QStringLiteral("minimal"), QStringLiteral("low"),
                          QStringLiteral("medium"), QStringLiteral("high")}));

    const QString key = QStringLiteral("google|gemini-4-hydrogen");
    QCOMPARE(registry.thinkingLevels(key).size(), 5); // Default + 4 levels
    QVERIFY(registry.modelDetails(key)
                .value(QStringLiteral("context")).toString().contains(QStringLiteral("1M")));
    QVERIFY(registry.setModelThinkingLevel(QStringLiteral("google"),
                                           QStringLiteral("gemini-4-hydrogen"),
                                           QStringLiteral("xhigh")));
    QCOMPARE(registry.modelThinkingLevel(QStringLiteral("google"),
                                         QStringLiteral("gemini-4-hydrogen")),
             QStringLiteral("xhigh"));
    QVERIFY(!registry.setModelThinkingLevel(QStringLiteral("google"),
                                            QStringLiteral("gemini-4-hydrogen"),
                                            QStringLiteral("minimal")));
    QString resolveError;
    QCOMPARE(registry.resolve(ModelRef::fromKey(key), &resolveError).thinkingLevel,
             QStringLiteral("xhigh"));
  }

  void testPluginsSettingsLoads() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("TEMPLNOTE_CONFIG_DIR", dir.path().toUtf8());

    Templnote::PluginManager manager;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("pluginManager"),
                                             &manager);
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this,
            [&errors](const QList<QQmlError> &list) { errors.append(list); });

    engine.loadFromModule(QStringLiteral("TemplnoteAISettingsTest"),
                          QStringLiteral("PluginsSettingsTest"));
    QCOMPARE(engine.rootObjects().size(), 1);
    if (!errors.isEmpty()) {
      for (const QQmlError &error : errors)
        qWarning() << error.toString();
    }
    QVERIFY2(errors.isEmpty(), "PluginsSettings.qml produced QML errors");
  }
};

QTEST_MAIN(AISettingsTests)
#include "tst_aisettings.moc"
