#include "ai/ModelRouter.h"
#include "ai/ProviderRegistry.h"
#include "ai/SecretStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace Templnote::AI;

Q_DECLARE_METATYPE(ChatResponse)

namespace {

// Tiny HTTP server that speaks just enough OpenAI/Ollama/Anthropic JSON to
// exercise connection validation, model discovery and chat completions.
class MockProviderServer : public QTcpServer {
  Q_OBJECT
public:
  bool failModels = false;
  QString lastPath;
  QString lastAuth;
  QString lastBody;

  QString baseUrl() const {
    return QStringLiteral("http://127.0.0.1:") + QString::number(serverPort());
  }

private:
  QHash<QTcpSocket *, QByteArray> m_buffers;

  void incomingConnection(qintptr handle) override {
    auto *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(handle)) {
      socket->deleteLater();
      return;
    }
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
      QByteArray &buffer = m_buffers[socket];
      buffer.append(socket->readAll());
      const int headerEnd = buffer.indexOf("\r\n\r\n");
      if (headerEnd < 0)
        return;
      int contentLength = 0;
      for (const QByteArray &line : buffer.left(headerEnd).split('\n')) {
        if (line.toLower().startsWith("content-length:"))
          contentLength = line.mid(15).trimmed().toInt();
      }
      if (buffer.size() < headerEnd + 4 + contentLength)
        return;
      const QByteArray request = buffer;
      buffer.clear();
      const QByteArrayList lines = request.split('\n');
      if (lines.isEmpty())
        return;
      const QString line = QString::fromLatin1(lines.first()).trimmed();
      const QStringList parts = line.split(QLatin1Char(' '));
      if (parts.size() < 2)
        return;
      lastPath = parts.at(1);
      lastAuth.clear();
      for (const QByteArray &raw : lines) {
        const QString header = QString::fromLatin1(raw);
        if (header.startsWith(QStringLiteral("Authorization:"),
                              Qt::CaseInsensitive))
          lastAuth = header.mid(header.indexOf(QLatin1Char(':')) + 1).trimmed();
      }
      const int separator = request.indexOf("\r\n\r\n");
      if (separator >= 0)
        lastBody = QString::fromUtf8(request.mid(separator + 4));

      QByteArray body;
      int status = 200;
      if (lastPath.contains(QStringLiteral("/api/tags"))) {
        body = R"({"models":[{"name":"llama3.2"}]})";
      } else if (lastPath.contains(QStringLiteral("/models"))) {
        if (failModels) {
          status = 500;
          body = R"({"error":"boom"})";
        } else {
          body = R"({"data":[{"id":"model-a"},{"id":"model-b"}]})";
        }
      } else if (lastPath.contains(QStringLiteral("/chat/completions")) ||
                 lastPath.contains(QStringLiteral("/messages"))) {
        body = R"({"model":"model-a","choices":[{"message":{"role":"assistant","content":"hello from mock"}}]})";
      } else {
        status = 404;
        body = R"({"error":"not found"})";
      }
      const QByteArray response =
          QByteArray("HTTP/1.1 ") + QByteArray::number(status) + " OK\r\n"
          "Content-Type: application/json\r\nContent-Length: " +
          QByteArray::number(body.size()) +
          "\r\nConnection: close\r\n\r\n" + body;
      socket->write(response);
      socket->flush();
      socket->disconnectFromHost();
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
      m_buffers.remove(socket);
      socket->deleteLater();
    });
  }
};

} // namespace

class ProviderTests : public QObject {
  Q_OBJECT

private:
  std::unique_ptr<QTemporaryDir> m_dir;

  static void waitFor(const QSignalSpy &spy, int count = 1) {
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= count, 4000);
  }

private slots:
  void initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("TemplnoteProviderTest"));
    QCoreApplication::setApplicationName(QStringLiteral("TemplnoteProviderTest"));
  }

  void init() {
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    qputenv("TEMPLNOTE_CONFIG_DIR", m_dir->path().toUtf8());
    // Avoid env leakage between tests.
    qunsetenv("TEMPLNOTE_SECRET_AI_PROVIDER_OPENROUTER_APIKEY");
  }

  void cleanup() { m_dir.reset(); }

  void testDefinitions() {
    const auto &defs = ProviderRegistry::definitions();
    QVERIFY(defs.contains(QStringLiteral("ollama")));
    QVERIFY(defs.contains(QStringLiteral("openrouter")));
    QVERIFY(defs.contains(QStringLiteral("anthropic")));
    QCOMPARE(defs.value(QStringLiteral("openrouter")).kind,
             ProviderKind::OpenAICompatible);
    QCOMPARE(defs.value(QStringLiteral("anthropic")).kind,
             ProviderKind::Anthropic);
    QVERIFY(!defs.value(QStringLiteral("ollama")).requiresKey);
  }

  void testSecretStoreRoundTrip() {
    SecretStore secrets;
    secrets.setTestMode(true);
    secrets.setFilePath(m_dir->filePath(QStringLiteral("secrets.dat")));
    QVERIFY(secrets.set(QStringLiteral("ai.provider.openrouter.apiKey"),
                        QStringLiteral("sk-test-12345")));
    QCOMPARE(secrets.get(QStringLiteral("ai.provider.openrouter.apiKey")),
             QStringLiteral("sk-test-12345"));
    QVERIFY(secrets.has(QStringLiteral("ai.provider.openrouter.apiKey")));

    QFile raw(m_dir->filePath(QStringLiteral("secrets.dat")));
    QVERIFY(raw.open(QIODevice::ReadOnly));
    QVERIFY(!raw.readAll().contains("sk-test-12345"));

    QVERIFY(secrets.remove(QStringLiteral("ai.provider.openrouter.apiKey")));
    QVERIFY(secrets.get(QStringLiteral("ai.provider.openrouter.apiKey")).isEmpty());
  }

  void testSecretEnvironmentOverride() {
    SecretStore secrets;
    secrets.setTestMode(true);
    secrets.setFilePath(m_dir->filePath(QStringLiteral("secrets2.dat")));
    qputenv(SecretStore::environmentName(
                QStringLiteral("ai.provider.openrouter.apiKey")).toUtf8().constData(),
            "sk-from-env");
    QCOMPARE(secrets.get(QStringLiteral("ai.provider.openrouter.apiKey")),
             QStringLiteral("sk-from-env"));
    qunsetenv(SecretStore::environmentName(
        QStringLiteral("ai.provider.openrouter.apiKey")).toUtf8().constData());
  }

  void testConnectDiscoverAndToggle() {
    MockProviderServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    SecretStore secrets;
    secrets.setTestMode(true);
    ProviderRegistry registry(nullptr, &secrets);
    registry.setLocalDetectionEnabled(false);

    QSignalSpy connected(&registry, &ProviderRegistry::connected);
    QVERIFY(registry.connectProvider(QStringLiteral("openrouter"),
                                     QStringLiteral("sk-test-key"),
                                     server.baseUrl()));
    waitFor(connected);
    QVERIFY(registry.isConnected(QStringLiteral("openrouter")));
    QCOMPARE(registry.modelsFor(QStringLiteral("openrouter")).size(), 2);
    QCOMPARE(registry.enabledModels().size(), 2);
    // Newly discovered models are enabled by default.
    for (const QVariant &value : registry.enabledModels())
      QVERIFY(value.toMap().value(QStringLiteral("enabled")).toBool());
    QCOMPARE(server.lastAuth, QStringLiteral("Bearer sk-test-key"));

    QVERIFY(registry.setModelEnabled(QStringLiteral("openrouter"),
                                     QStringLiteral("model-b"), false));
    QCOMPARE(registry.enabledModels().size(), 1);
    QCOMPARE(registry.enabledModels().first().toMap()
                 .value(QStringLiteral("modelId")).toString(),
             QStringLiteral("model-a"));

    // Preferences and connection survive a restart (the key is in SecretStore).
    const QString key = registry.enabledModels().first().toMap()
                            .value(QStringLiteral("key")).toString();
    registry.setDefaultModelKey(key);
    ProviderRegistry reloaded(nullptr, &secrets);
    reloaded.setLocalDetectionEnabled(false);
    QVERIFY(reloaded.isConnected(QStringLiteral("openrouter")));
    QCOMPARE(reloaded.enabledModels().size(), 1);
    QCOMPARE(reloaded.defaultModelKey(), key);
  }

  void testDisconnectClearsModels() {
    MockProviderServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    SecretStore secrets;
    secrets.setTestMode(true);
    ProviderRegistry registry(nullptr, &secrets);
    registry.setLocalDetectionEnabled(false);
    QSignalSpy connected(&registry, &ProviderRegistry::connected);
    QVERIFY(registry.connectProvider(QStringLiteral("openrouter"),
                                     QStringLiteral("sk-test-key"),
                                     server.baseUrl()));
    waitFor(connected);
    registry.setDefaultModelKey(
        registry.enabledModels().first().toMap()
            .value(QStringLiteral("key")).toString());

    QSignalSpy disconnected(&registry, &ProviderRegistry::disconnected);
    registry.disconnectProvider(QStringLiteral("openrouter"));
    QCOMPARE(disconnected.count(), 1);
    QVERIFY(!registry.isConnected(QStringLiteral("openrouter")));
    QVERIFY(registry.enabledModels().isEmpty());
    QVERIFY(registry.defaultModelKey().isEmpty());
    QVERIFY(!secrets.has(QStringLiteral("ai.provider.openrouter.apiKey")));
  }

  void testLocalProviderDetection() {
    MockProviderServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    SecretStore secrets;
    secrets.setTestMode(true);
    ProviderRegistry registry(nullptr, &secrets);
    registry.setLocalEndpoint(QStringLiteral("ollama"), server.baseUrl());
    // Avoid a stray probe to the real LM Studio port hitting a later test's
    // mock server if the OS reuses that port.
    registry.setLocalEndpoint(QStringLiteral("lm-studio"),
                              QStringLiteral("http://127.0.0.1:1"));
    registry.detectLocalProviders();

    QTRY_VERIFY_WITH_TIMEOUT(registry.isConnected(QStringLiteral("ollama")), 4000);
    QCOMPARE(registry.modelsFor(QStringLiteral("ollama")).size(), 1);
    QCOMPARE(registry.enabledModels().first().toMap()
                 .value(QStringLiteral("modelId")).toString(),
             QStringLiteral("llama3.2"));
  }

  void testConnectionFailure() {
    MockProviderServer server;
    server.failModels = true;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    SecretStore secrets;
    secrets.setTestMode(true);
    ProviderRegistry registry(nullptr, &secrets);
    registry.setLocalDetectionEnabled(false);
    QSignalSpy errors(&registry, &ProviderRegistry::error);
    QVERIFY(registry.connectProvider(QStringLiteral("openrouter"),
                                     QStringLiteral("sk-test-key"),
                                     server.baseUrl()));
    QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 4000);
    QVERIFY(!registry.isConnected(QStringLiteral("openrouter")));
    QVERIFY(errors.first().at(0).toString().contains(QStringLiteral("OpenRouter")));
  }

  void testRouterUsesRegistry() {
    MockProviderServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    SecretStore secrets;
    secrets.setTestMode(true);
    ProviderRegistry registry(nullptr, &secrets);
    registry.setLocalDetectionEnabled(false);
    QSignalSpy connected(&registry, &ProviderRegistry::connected);
    QVERIFY(registry.connectProvider(QStringLiteral("custom"),
                                     QStringLiteral("sk-custom"),
                                     server.baseUrl()));
    waitFor(connected);
    const QString chosenModel = registry.enabledModels().first().toMap()
                                    .value(QStringLiteral("modelId")).toString();
    registry.setDefaultModelKey(
        registry.enabledModels().first().toMap()
            .value(QStringLiteral("key")).toString());

    ModelRouter router;
    router.setRegistry(&registry);
    QSignalSpy finished(&router, &ModelRouter::finished);
    ChatRequest request;
    request.messages.append({QStringLiteral("user"), QStringLiteral("hi")});
    router.chat(request);
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 4000);
    const ChatResponse response = finished.first().at(0)
                                      .value<ChatResponse>();
    QVERIFY(response.ok);
    QCOMPARE(response.text, QStringLiteral("hello from mock"));
    QCOMPARE(server.lastAuth, QStringLiteral("Bearer sk-custom"));
    QVERIFY(server.lastBody.contains(chosenModel));

    registry.disconnectProvider(QStringLiteral("custom"));
    QSignalSpy second(&router, &ModelRouter::finished);
    router.chat(request);
    QTRY_VERIFY_WITH_TIMEOUT(!second.isEmpty(), 4000);
    const ChatResponse failure = second.first().at(0)
                                     .value<ChatResponse>();
    QVERIFY(!failure.ok);
    QVERIFY(failure.error.contains(QStringLiteral("Connect")));
  }
};

QTEST_GUILESS_MAIN(ProviderTests)
#include "tst_providers.moc"
