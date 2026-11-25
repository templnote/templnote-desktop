// templnote-headless: QCoreApplication CLI for document engine operations.
//
// Usage:
//   templnote-headless apply <file> <command-json>
//   templnote-headless script <file> <commands.json>
//   templnote-headless versions <file>

#include "engine/EditorSession.h"
#include "engine/Types.h"
#include "store/DocumentStore.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

using namespace Templnote;

namespace {

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

  return fail("unknown command: " + command);
}
