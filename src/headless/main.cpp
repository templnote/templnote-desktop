// templnote-headless: QCoreApplication daemon for agent workflows.
//
// Usage:
//   templnote-headless apply <file> <command-json>
//   templnote-headless script <file> <commands.json>
//   templnote-headless versions <file>
//   templnote-headless serve [--port N] [--journal path] [--sync-port N]

#include "engine/CapabilityGate.h"
#include "engine/ChangeFeed.h"
#include "engine/CrdtDocumentState.h"
#include "engine/EditorSession.h"
#include "engine/Types.h"
#include "store/DocumentStore.h"
#include "adapter/AIEngineAdapters.h"
#include "ai/AgentHttpServer.h"
#include "ai/ModelRouter.h"
#include "ai/ProviderRegistry.h"
#include "ai/SecretStore.h"
#include "plugins/PluginManager.h"
#include "ai/PipelineRunner.h"
#include "sync/SyncManager.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QUuid>

using namespace Templnote;

namespace {
QJsonObject loadDocument(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return QJsonDocument::fromJson(file.readAll()).object();
}

int fail(const QString &message) {
  QTextStream(stderr) << "error: " << message << "\n";
  return 1;
}

int applyCommands(const QString &path, const QJsonArray &commands) {
  Store::DocumentStore store;
  const auto loaded = store.load(path);
  if (!loaded.ok())
    return fail(loaded.error());

  Engine::EditorSession session;
  QString error;
  if (!session.loadJson(loaded.value().document, &error))
    return fail(error);

  Engine::Patch combined;
  combined.actor = Engine::Actor::system();
  for (const QJsonValue &value : commands) {
    QString commandError;
    const Engine::Command cmd =
        Engine::Command::fromJson(value.toObject(), &commandError);
    if (!commandError.isEmpty())
      return fail(commandError);
    auto result = session.apply(cmd);
    if (!result.ok())
      return fail(result.error());
    combined.ops.append(result.value().ops);
  }

  const auto committed =
      store.commit(path, loaded.value().revision, session.toJson(), combined,
                   session.lamport(), QStringLiteral("headless-cli"));
  if (!committed.ok())
    return fail(committed.error());

  QTextStream(stdout) << QJsonDocument(session.toJson())
                           .toJson(QJsonDocument::Compact)
                      << "\n";
  QTextStream(stdout) << "revision=" << committed.value().revision << "\n";
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  const QStringList args = app.arguments();
  if (args.size() < 3)
    return fail("usage: templnote-headless <command> <file> [json]");

  const QString command = args.at(1);
  const QString path = args.at(2);

  if (command == QLatin1String("apply")) {
    if (args.size() < 4)
      return fail("apply requires a command JSON argument");
    QJsonArray commands;
    commands.append(QJsonDocument::fromJson(args.at(3).toUtf8()).object());
    return applyCommands(path, commands);
  }
  if (command == QLatin1String("script")) {
    if (args.size() < 4)
      return fail("script requires a commands file");
    QFile file(args.at(3));
    if (!file.open(QIODevice::ReadOnly))
      return fail(file.errorString());
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError)
      return fail(error.errorString());
    if (!doc.isArray())
      return fail("commands file must contain a JSON array");
    return applyCommands(path, doc.array());
  }
  if (command == QLatin1String("versions")) {
    Store::DocumentStore store;
    const auto versions = store.versions(path);
    QJsonArray array;
    for (const auto &version : versions)
      array.append(version.toJson());
    QTextStream(stdout) << QJsonDocument(array).toJson(QJsonDocument::Indented)
                        << "\n";
    return 0;
  }
  if (command == QLatin1String("serve")) {
    // --port 8787 --sync-port 45210 --file <path> [--host 127.0.0.1]
    // [--token <bearer-token>] [--allow-lan]
    quint16 httpPort = 8787;
    int syncPort = 45210;
    QString filePath;
    QString host = QStringLiteral("127.0.0.1");
    QString token = qEnvironmentVariable("TEMPLNOTE_AGENT_TOKEN");
    for (int i = 2; i + 1 < args.size(); ++i) {
      if (args.at(i) == QLatin1String("--port"))
        httpPort = args.at(i + 1).toUShort();
      else if (args.at(i) == QLatin1String("--sync-port"))
        syncPort = args.at(i + 1).toInt();
      else if (args.at(i) == QLatin1String("--file"))
        filePath = args.at(i + 1);
      else if (args.at(i) == QLatin1String("--host"))
        host = args.at(i + 1);
      else if (args.at(i) == QLatin1String("--token"))
        token = args.at(i + 1);
      else if (args.at(i) == QLatin1String("--allow-lan"))
        host = QStringLiteral("0.0.0.0");
    }
    if (host != QStringLiteral("127.0.0.1") &&
        host != QStringLiteral("::1") &&
        host != QStringLiteral("localhost")) {
      QTextStream(stderr)
          << "warning: control API is listening on " << host
          << "; use a VPN tunnel and a strong token.\n";
    }

    Store::DocumentStore store;
    int committedRevision = 0;
    Engine::EditorSession session;
    Engine::ChangeFeed feed(&session, &app);
    if (!filePath.isEmpty()) {
      const auto loaded = store.load(filePath);
      if (loaded.ok()) {
        session.loadJson(loaded.value().document);
        committedRevision = loaded.value().revision;
      }
    }

    // Use the CRDT backend so remote peers can merge their op sets.
    Engine::CrdtDocumentState crdt;
    crdt.load(session.toJson());
    session.setDocumentState(&crdt);

    // The gate is installed on the session itself: no endpoint, pipeline stage
    // or sync peer can mutate the document without passing it.
    Engine::CapabilityGate gate;
    const QStringList trustedIds{"local-owner", "http-agent", "ai-agent",
                                 "write", "grammar-1", "humanizer",
                                 "grammar-2", "validation"};
    for (const QString &id : trustedIds)
      gate.setRole(id, Engine::CapabilityGate::Role::Owner);
    session.setActor(Engine::Actor::human(QStringLiteral("local-owner")));
    session.setCapabilityGate(&gate);

    if (token.isEmpty())
      token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    Sync::SyncManager sync(syncPort, &app);
    sync.setHost(host);
    sync.setAuthToken(token);
    sync.setState(&crdt);
    if (sync.startServer())
      QTextStream(stdout) << "sync listening on port " << syncPort << "\n";

    AI::SecretStore secrets(&app);
    AI::ProviderRegistry providers(&app, &secrets);
    providers.detectLocalProviders();

    AI::ModelRouter router(&app);
    router.setRegistry(&providers);
    Templnote::PluginManager pluginManager(&app);
    Adapter::SessionDocumentPort document(&session, &app);
    Adapter::FeedChangePort changes(&feed, &app);
    Adapter::PluginManagerToolPort pluginTools(&pluginManager, &app);
    AI::PipelineRunner pipeline(&app);
    pipeline.setDocument(&document);
    pipeline.setRouter(&router);
    pipeline.setPluginTools(&pluginTools);
    pipeline.setChangeFeed(&changes);
    pipeline.setStages(AI::PipelineRunner::defaultStages());

    // Persist every committed patch with compare-and-swap so an AI edit is
    // never lost when the daemon exits, and a concurrent GUI save is detected
    // instead of silently overwritten.
    const auto commitPatch = [&](const Engine::Patch &patch) {
      if (filePath.isEmpty())
        return;
      const auto result =
          store.commit(filePath, committedRevision, session.toJson(), patch,
                       session.lamport(), QStringLiteral("daemon"));
      if (result.ok())
        committedRevision = result.value().revision;
      else
        QTextStream(stderr) << "warning: commit failed: " << result.error()
                            << "\n";
    };
    QObject::connect(&session, &Engine::EditorSession::patchApplied, &app,
                     commitPatch);

    AI::AgentHttpServer server(&app);
    server.setDocumentProvider(&document);
    server.setChangesProvider(&changes);
    server.setPipeline(&pipeline);
    server.setPluginTools(&pluginTools);
    server.setProviderRegistry(&providers);
    server.setAuthToken(token);
    // Pick up saves made by the desktop app between requests.
    QObject::connect(
        &server, &AI::AgentHttpServer::requestReceived, &app,
        [&](const QString &method, const QString &) {
          if (method != QStringLiteral("POST") || filePath.isEmpty() ||
              pipeline.running())
            return;
          const auto loaded = store.load(filePath);
          if (!loaded.ok() || loaded.value().revision == committedRevision)
            return;
          session.loadJson(loaded.value().document);
          committedRevision = loaded.value().revision;
          feed.clear();
        });
    if (!server.listen(httpPort, host))
      return fail(QStringLiteral("Cannot listen on port ") +
                  QString::number(httpPort));

    QTextStream(stdout) << "templnote agent daemon listening on http://" << host
                        << ":" << httpPort << "\n";
    QTextStream(stderr) << "agent token: " << server.authToken() << "\n";
    QTextStream(stdout) << "connected providers: "
                        << (providers.anyConnected() ? "yes" : "no") << "\n";
    return app.exec();
  }
  return fail("unknown command: " + command);
}
