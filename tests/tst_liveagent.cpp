// LIVE end-to-end agent test (not part of CTest: requires a local Ollama).
//
// Drives the real PipelineRunner + ModelRouter + editor ports against
// qwen3.5:4b and prints the resulting document structure for inspection.

#include "adapter/AIEngineAdapters.h"
#include "ai/AIPaths.h"
#include "ai/ModelRouter.h"
#include "ai/PipelineRunner.h"
#include "ai/SkillCatalog.h"
#include "ai/SkillFile.h"
#include "ai/ToolSchema.h"
#include "engine/EditorSession.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <QTemporaryDir>
#include <QTime>
#include <QVector>

using namespace Templnote;
using namespace Templnote::AI;

namespace {

QJsonObject block(const QString &id, const QString &type, const QString &text) {
  QJsonObject b;
  b.insert(QStringLiteral("id"), id);
  b.insert(QStringLiteral("type"), type);
  b.insert(QStringLiteral("text"), text);
  return b;
}

QJsonObject sampleDocument() {
  QJsonArray blocks;
  blocks.append(block(QStringLiteral("b-title"), QStringLiteral("paragraph"),
                      QStringLiteral("Project Notes")));
  blocks.append(block(QStringLiteral("b-intro"), QStringLiteral("paragraph"),
                      QStringLiteral("This project explores local-first document editing with AI assistance.")));
  blocks.append(block(QStringLiteral("b-old"), QStringLiteral("paragraph"),
                      QStringLiteral("Old conclusion that should be replaced.")));
  QJsonObject doc;
  doc.insert(QStringLiteral("format"), QStringLiteral("templnote"));
  doc.insert(QStringLiteral("version"), 1);
  doc.insert(QStringLiteral("blocks"), blocks);
  return doc;
}

QString describe(const QJsonObject &doc) {
  QStringList lines;
  const QJsonArray blocks = doc.value(QStringLiteral("blocks")).toArray();
  lines.append(QStringLiteral("blocks=%1").arg(blocks.size()));
  for (const QJsonValue &value : blocks) {
    const QJsonObject b = value.toObject();
    lines.append(QStringLiteral("  %1 [%2] %3")
                     .arg(b.value(QStringLiteral("id")).toString(),
                          b.value(QStringLiteral("type")).toString(),
                          b.value(QStringLiteral("text")).toString().left(70)));
  }
  return lines.join(QLatin1Char('\n'));
}

class LiveAgent : public QObject {
  Q_OBJECT
public:
  LiveAgent(ModelRouter *router, Engine::EditorSession *session,
            Adapter::SessionDocumentPort *port, Adapter::LiveRetrievalPort *retrieval)
      : m_router(router), m_session(session), m_port(port), m_retrieval(retrieval),
        m_runner(this) {
    m_runner.setRouter(router);
    m_runner.setDocument(port);
    m_runner.setRetrieval(retrieval);
    m_runner.setSkillCatalog(&m_skills);
    AgentRun run;
    run.id = QStringLiteral("build");
    m_runner.setAgentRuns({run});
    connect(&m_runner, &PipelineRunner::agentApplied, this,
            [this](const QString &agent, const QJsonObject &patch) {
              const QJsonArray ops = patch.value(QStringLiteral("ops")).toArray();
              for (const QJsonValue &op : ops)
                m_applied.append(op.toObject().value(QStringLiteral("type")).toString());
            });
  }

  PipelineResult run(const QString &prompt) {
    m_applied.clear();
    PipelineResult result;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(&m_runner, &PipelineRunner::finished, this,
            [&](const PipelineResult &value) { result = value; loop.quit(); },
            Qt::SingleShotConnection);
    timeout.start(2 * 60 * 1000);
    QTextStream(stdout) << "\n=== PROMPT: " << prompt << "\n";
    const QTime started = QTime::currentTime();
    // Start on the next event-loop turn so an immediate failure signal is
    // observed by the loop instead of arriving before exec().
    QTimer::singleShot(0, [this, prompt] { m_runner.start(prompt); });
    loop.exec();
    QTextStream(stdout) << "--- " << started.msecsTo(QTime::currentTime())
                        << " ms, ok=" << result.ok
                        << ", rounds=" << result.toolRounds
                        << ", tokens=" << result.promptTokens << "/"
                        << result.completionTokens
                        << ", ops=" << m_applied.join(QStringLiteral(",")) << "\n";
    if (!result.error.isEmpty())
      QTextStream(stderr) << "ERROR: " << result.error << "\n";
    if (!result.answer().isEmpty())
      QTextStream(stdout) << "ANSWER: " << result.answer().left(300) << "\n";
    for (const ChatMessage &message : m_runner.session().messages) {
      if (message.role != QLatin1String("tool"))
        continue;
      QTextStream(stdout) << "TOOL " << message.name << " -> "
                          << message.content.left(180).replace(u'\n', u' ') << "\n";
    }
    QTextStream(stdout) << describe(m_session->toJson()) << "\n";
    return result;
  }

private:
  ModelRouter *m_router;
  Engine::EditorSession *m_session;
  Adapter::SessionDocumentPort *m_port;
  Adapter::LiveRetrievalPort *m_retrieval;
  PipelineRunner m_runner;
  SkillCatalog m_skills;
  QStringList m_applied;
};

} // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir dir;
  AIPaths::setConfigDirectory(dir.path());
  QCoreApplication::setOrganizationName("TemplnoteLiveAgent");
  QCoreApplication::setApplicationName("TemplnoteLiveAgent");

  ModelRouter router;
  router.setLocalEndpoint(QStringLiteral("http://127.0.0.1:11434"));
  router.setLocalModel(QStringLiteral("qwen3.5:4b"));
  router.setMaxRetries(0);
  router.detectLocalModels();

  QEventLoop wait;
  QTimer poll;
  int attempts = 0;
  QObject::connect(&poll, &QTimer::timeout, [&] {
    if (router.localAvailable() || ++attempts > 50)
      wait.quit();
  });
  poll.start(100);
  wait.exec();
  if (!router.localAvailable()) {
    QTextStream(stderr) << "Ollama not reachable at 127.0.0.1:11434\n";
    return 2;
  }

  Engine::EditorSession session;
  session.loadJson(sampleDocument());
  session.setActor(Engine::Actor::human(QStringLiteral("local-owner")));
  Adapter::SessionDocumentPort port(&session);
  Adapter::LiveRetrievalPort retrieval(
      [&session] {
        return QString::fromUtf8(QJsonDocument(session.toJson())
                                     .toJson(QJsonDocument::Compact));
      });
  LiveAgent agent(&router, &session, &port, &retrieval);

  // Preflight: check the router/provider path on its own.
  {
    ChatRequest request;
    request.messages.append({QStringLiteral("user"),
                             QStringLiteral("Reply with exactly: OK")});
    request.stream = false;
    ChatResponse response;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&router, &ModelRouter::finished, &router,
                     [&](const ChatResponse &r) {
      response = r;
      loop.quit();
    }, Qt::SingleShotConnection);
    timer.start(30000);
    QTimer::singleShot(0, [&] { router.chat(request); });
    loop.exec();
    QTextStream(stdout) << "PREFLIGHT ok=" << response.ok
                        << " error=" << response.error
                        << " text=" << response.text.left(80)
                        << " calls=" << response.toolCalls.size()
                        << " tokens=" << response.promptTokens << "/"
                        << response.completionTokens << "\n";
  }

  QTextStream(stdout) << "INITIAL:\n" << describe(session.toJson()) << "\n";

  // Four progressively complex prompts covering the main document types.
  agent.run(QString(
      "Use tools only. Change block b-title into a heading level 1, "
      "replace b-old with a paragraph saying 'Outcome: shipped successfully', "
      "then insert a divider block after it. Do not write markdown syntax."));
  agent.run(QString(
      "Use tools only. Insert a todo block with text 'Verify the release "
      "checklist' after the divider, mark it checked, and add bold formatting "
      "to the first 12 characters of block b-intro."));
  agent.run(QString(
      "Use tools only. Insert a callout block with text 'Deadline moved to Friday' "
      "and calloutType warning, then insert a code block with text 'const x = 1;' "
      "and language javascript. Use setCodeLanguage and setCalloutType as needed."));
  agent.run(QString(
      "Use tools only. Build a 2-column layout with one paragraph inside each "
      "column types 12 for columns and 13 for column. If the single-block tools cannot "
      "express nesting, use applyJson with a complete valid .templnote document "
      "that preserves the existing blocks and adds the columns."));

  return 0;
}

#include "tst_liveagent.moc"
