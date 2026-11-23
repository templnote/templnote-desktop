#include "ai/ProviderRegistry.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginStoreClient.h"

#include <QEventLoop>
#include <QFile>
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

private:
  void incomingConnection(qintptr handle) override {
    auto *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(handle)) {
      socket->deleteLater();
      return;
    }
    connect(socket, &QTcpSocket::readyRead, this, [socket]() {
      socket->readAll();
      const QByteArray body = R"({"data":[{"id":"model-a"},{"id":"model-b"}]})";
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
};

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
    ProviderRegistry registry;
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
  void testPluginStoreLoads() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("TEMPLNOTE_CONFIG_DIR", dir.path().toUtf8());

    const QString fixture = QString::fromLatin1(TEMPLNOTE_PLUGIN_STORE_FIXTURE);
    Templnote::PluginManager manager;
    Templnote::Plugins::PluginStoreClient store;
    store.setManager(&manager);
    store.setCatalogUrl(QUrl::fromLocalFile(fixture + QStringLiteral("/catalog.json")));
    QFile keyFile(fixture + QStringLiteral("/public.key"));
    QVERIFY(keyFile.open(QIODevice::ReadOnly));
    store.setPublicKeyPem(keyFile.readAll());
    store.setHostTags({QStringLiteral("qml-desktop"), QStringLiteral("linux")});

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("pluginManager"), &manager);
    engine.rootContext()->setContextProperty(QStringLiteral("pluginStore"), &store);
    QList<QQmlError> errors;
    connect(&engine, &QQmlApplicationEngine::warnings, this,
            [&errors](const QList<QQmlError> &list) { errors.append(list); });

    engine.loadFromModule(QStringLiteral("TemplnoteAISettingsTest"),
                          QStringLiteral("PluginStoreTest"));
    QCOMPARE(engine.rootObjects().size(), 1);
    QTest::qWait(500); // let the local catalog request finish
    if (!errors.isEmpty()) {
      for (const QQmlError &error : errors)
        qWarning() << error.toString();
    }
    QVERIFY2(errors.isEmpty(), "PluginStoreSettings.qml produced QML errors");
    QCOMPARE(store.model()->rowCount(), 1);
  }
};

QTEST_MAIN(AISettingsTests)
#include "tst_aisettings.moc"
