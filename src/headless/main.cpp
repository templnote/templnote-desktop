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
#include "ai/AgentRegistry.h"
#include "ai/SessionManager.h"
#include "ai/SessionStore.h"
#include "ai/SkillFile.h"
#include "ai/ModelRouter.h"
#include "ai/ProviderRegistry.h"
#include "ai/SecretStore.h"
#include "plugins/PluginManager.h"
#include "ai/PipelineRunner.h"
#include "sync/SyncManager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>
#include <QUuid>

#include <functional>

using namespace Templnote;

namespace {

// Minimal retrieval over the daemon's live document. Keeps session prompts
// proportional to the matching blocks instead of the whole note.
class DaemonRetrieval : public AI::RetrievalPort, public QObject {
public:
  explicit DaemonRetrieval(std::function<QString()> documentJson)
      : m_documentJson(std::move(documentJson)) {}

  QJsonArray relevantBlocks(const QString &query, int limit) const override {
    QJsonArray out;
    if (query.trimmed().isEmpty()) {
      const QJsonDocument all = QJsonDocument::fromJson(m_documentJson().toUtf8());
      for (const QJsonValue &value :
           all.object().value(QStringLiteral("blocks")).toArray()) {
        const QJsonObject block = value.toObject();
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), block.value(QStringLiteral("id")));
        entry.insert(QStringLiteral("type"), block.value(QStringLiteral("type")));
        entry.insert(QStringLiteral("text"),
                     block.value(QStringLiteral("text")).toString().left(80));
        out.append(entry);
        if (out.size() >= limit)
          break;
      }
      return out;
    }
    const QStringList terms = query.split(QRegularExpression(QStringLiteral(
                                    R"([^\p{L}\p{N}_]+)")),
                                Qt::SkipEmptyParts);
    const QJsonDocument doc = QJsonDocument::fromJson(m_documentJson().toUtf8());
    for (const QJsonValue &value :
         doc.object().value(QStringLiteral("blocks")).toArray()) {
      const QJsonObject block = value.toObject();
      const QString text = block.value(QStringLiteral("text")).toString();
      bool matches = false;
      for (const QString &term : terms)
        if (term.size() >= 2 && text.contains(term, Qt::CaseInsensitive))
          matches = true;
      if (!matches)
        continue;
      QJsonObject entry;
      entry.insert(QStringLiteral("id"), block.value(QStringLiteral("id")));
      entry.insert(QStringLiteral("type"), block.value(QStringLiteral("type")));
      entry.insert(QStringLiteral("text"), text.left(600));
      out.append(entry);
      if (out.size() >= limit)
        break;
    }
    return out;
  }

  QJsonObject block(const QString &id) const override {
    const QJsonDocument doc = QJsonDocument::fromJson(m_documentJson().toUtf8());
    for (const QJsonValue &value :
         doc.object().value(QStringLiteral("blocks")).toArray()) {
      const QJsonObject candidate = value.toObject();
      if (candidate.value(QStringLiteral("id")).toString() == id)
        return candidate;
    }
    return {};
  }

private:
  std::function<QString()> m_documentJson;
};

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
    int httpPort = 8787;
    int syncPort = 45210;
    QString filePath;
    QString host = QStringLiteral("127.0.0.1");
    QString token = qEnvironmentVariable("TEMPLNOTE_AGENT_TOKEN");
    bool printToken = false;
    const auto parsePort = [](const QString &value, int &out) {
      bool ok = false;
      const int parsed = value.toInt(&ok);
      if (!ok || parsed < 1 || parsed > 65535)
        return fail(QStringLiteral("Invalid port value: ") + value);
      out = parsed;
      return 0;
    };
    for (int i = 2; i < args.size(); ++i) {
      const QString &arg = args.at(i);
      // Value-taking options: require and consume a following argument.
      const auto nextValue = [&](const char *name, QString &out) {
        if (i + 1 >= args.size()) {
          out.clear();
          return fail(QStringLiteral("Missing value for ") +
                      QLatin1String(name));
        }
        out = args.at(++i);
        return 0;
      };
      if (arg == QLatin1String("--port")) {
        QString value;
        if (const int rc = nextValue("--port", value))
          return rc;
        if (const int rc = parsePort(value, httpPort))
          return rc;
      } else if (arg == QLatin1String("--sync-port")) {
        QString value;
        if (const int rc = nextValue("--sync-port", value))
          return rc;
        if (const int rc = parsePort(value, syncPort))
          return rc;
      } else if (arg == QLatin1String("--file")) {
        if (const int rc = nextValue("--file", filePath))
          return rc;
      } else if (arg == QLatin1String("--host")) {
        if (const int rc = nextValue("--host", host))
          return rc;
      } else if (arg == QLatin1String("--token-file")) {
        QString path;
        if (const int rc = nextValue("--token-file", path))
          return rc;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
          return fail(f.errorString());
        token = QString::fromUtf8(f.readAll().trimmed());
      } else if (arg == QLatin1String("--token")) {
        QTextStream(stderr)
            << "warning: --token exposes the secret in the process list; "
               "prefer --token-file or TEMPLNOTE_AGENT_TOKEN.\n";
        if (const int rc = nextValue("--token", token))
          return rc;
      } else if (arg == QLatin1String("--print-token")) {
        printToken = true;
      } else if (arg == QLatin1String("--allow-lan")) {
        host = QStringLiteral("0.0.0.0");
      } else {
        return fail(QStringLiteral("Unknown option: ") + arg);
      }
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

    // The gate is installed on the session itself: no endpoint, configured
    // agent or sync peer can mutate the document without passing it.
    AI::AgentRegistry agentRegistry(&app);
    agentRegistry.load();
    Engine::CapabilityGate gate;
    const auto syncGate = [&] {
      QStringList trustedIds{"local-owner", "http-agent", "ai-agent"};
      trustedIds.append(agentRegistry.agentIds());
      for (const QString &id : trustedIds)
        gate.setRole(id, Engine::CapabilityGate::Role::Owner);
      session.setTrustedActorIds(trustedIds);
    };
    syncGate();
    // Agents can be created/updated over the HTTP control API at runtime.
    // Their gate roles must be refreshed too, not just the pipeline runs.
    QObject::connect(&agentRegistry, &AI::AgentRegistry::agentsChanged, &app,
                     syncGate);
    QObject::connect(&agentRegistry, &AI::AgentRegistry::activeAgentsChanged,
                     &app, syncGate);
    session.setActor(Engine::Actor::human(QStringLiteral("local-owner")));
    session.setCapabilityGate(&gate);

    const bool generatedToken = token.isEmpty();
    if (generatedToken)
      token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // Use a distinct, one-way derived token for sync. Compromise of the LAN
    // sync channel must never grant access to the HTTP control API.
    const QByteArray syncToken =
        QCryptographicHash::hash(token.toUtf8() +
                                     QByteArrayLiteral("templnote-sync-token"),
                                 QCryptographicHash::Sha256)
            .toHex();
    Sync::SyncManager sync(syncPort, &app);
    sync.setHost(host);
    sync.setAuthToken(QString::fromLatin1(syncToken));
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
    // No built-in pipeline. The user's selected agents form the run; with none
    // selected a single prompt-driven agent handles the request.
    const auto syncAgentRuns = [&] {
      QVector<AI::AgentRun> runs;
      const QString instructions = AI::SkillFile::readOrCreate();
      for (const AI::AgentDefinition &def : agentRegistry.activeAgentDefs()) {
        if (!def.document.trimmed().isEmpty())
          continue;
        AI::AgentRun run;
        run.id = def.id;
        run.instructions = def.instructions;
        run.instructionBlocks = {instructions};
        runs.append(run);
      }
      if (runs.isEmpty()) {
        AI::AgentRun run;
        run.id = QStringLiteral("ai-agent");
        run.instructionBlocks = {instructions};
        runs.append(run);
      }
      pipeline.setAgentRuns(runs);
    };
    syncAgentRuns();
    QObject::connect(&agentRegistry, &AI::AgentRegistry::agentsChanged, &app,
                     syncAgentRuns);
    QObject::connect(&agentRegistry, &AI::AgentRegistry::activeAgentsChanged,
                     &app, syncAgentRuns);

    // Concurrent conversations share the daemon's single CRDT document but
    // keep independent histories and review state in their own sessions.
    AI::SessionStore sessionStore(&app);
    sessionStore.open();
    AI::SessionManager sessionManager(&app);
    auto *daemonRetrieval = new DaemonRetrieval([&session] {
      return QString::fromUtf8(
          QJsonDocument(session.toJson()).toJson(QJsonDocument::Compact));
    });
    sessionManager.configure(
        &router, &sessionStore, {}, AI::BudgetOptions{},
        AI::SessionManager::PortFactory{
            [&](QObject *, const QString &) {
              return AI::SessionManager::SessionPorts{
                  &document, &changes, daemonRetrieval};
            }});
    const auto syncSessionRuns = [&] {
      QVector<AI::AgentRun> runs;
      const QString instructions = AI::SkillFile::readOrCreate();
      for (const AI::AgentDefinition &def : agentRegistry.activeAgentDefs()) {
        if (!def.document.trimmed().isEmpty())
          continue;
        AI::AgentRun run;
        run.id = def.id;
        run.instructions = def.instructions;
        run.instructionBlocks = {instructions};
        runs.append(run);
      }
      if (runs.isEmpty()) {
        AI::AgentRun run;
        run.id = QStringLiteral("ai-agent");
        run.instructionBlocks = {instructions};
        runs.append(run);
      }
      sessionManager.setAgentRuns(runs);
    };
    sessionManager.setPluginTools(&pluginTools);
    syncSessionRuns();
    QObject::connect(&agentRegistry, &AI::AgentRegistry::agentsChanged, &app,
                     syncSessionRuns);
    QObject::connect(&agentRegistry, &AI::AgentRegistry::activeAgentsChanged,
                     &app, syncSessionRuns);

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
    server.setSessionManager(&sessionManager);
    server.setPluginTools(&pluginTools);
    server.setProviderRegistry(&providers);
    server.setAgentRegistry(&agentRegistry);
    server.setAuthToken(token);
    // Pick up saves made by the desktop app between requests.
    QObject::connect(
        &server, &AI::AgentHttpServer::requestReceived, &app,
        [&](const QString &method, const QString &) {
          bool anySessionRunning = false;
          for (const QString &sessionId : sessionManager.sessionIds()) {
            if (AI::PipelineRunner *runner = sessionManager.runner(sessionId))
              anySessionRunning = anySessionRunning || runner->running();
          }
          if (method != QStringLiteral("POST") || filePath.isEmpty() ||
              pipeline.running() || anySessionRunning)
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
                        << ":" << server.port() << "\n";
    if (printToken)
      QTextStream(stderr) << "agent token: " << server.authToken() << "\n";
    else if (generatedToken)
      QTextStream(stderr) << "a random agent token was generated; use "
                             "--token-file, TEMPLNOTE_AGENT_TOKEN, or "
                             "--print-token to obtain it.\n";
    QTextStream(stdout) << "connected providers: "
                        << (providers.anyConnected() ? "yes" : "no") << "\n";
    return app.exec();
  }
  return fail("unknown command: " + command);
}
