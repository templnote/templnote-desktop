#include "AIEngineAdapters.h"

#include "engine/CapabilityGate.h"
#include "engine/EditorSession.h"
#include "engine/Types.h"
#include "plugins/PluginManager.h"

#include <algorithm>

#include <QHash>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>
#include <QVariantMap>

namespace Templnote::Adapter {
namespace {

struct BlockInfo {
  QString id;
  QString text;
  QString type;
};

QVector<BlockInfo> parseBlocks(const QString &json) {
  QVector<BlockInfo> blocks;
  const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
  const QJsonArray array = doc.object().value(QStringLiteral("blocks")).toArray();
  for (const QJsonValue &value : array) {
    const QJsonObject block = value.toObject();
    blocks.append({block.value(QStringLiteral("id")).toString(),
                   block.value(QStringLiteral("text")).toString(),
                   block.value(QStringLiteral("type")).toString()});
  }
  return blocks;
}

QVariantMap changeEntry(const QString &id, const QString &kind,
                        const QString &before, const QString &after,
                        const QString &type) {
  QVariantMap entry;
  entry.insert(QStringLiteral("id"), id);
  entry.insert(QStringLiteral("kind"), kind);
  entry.insert(QStringLiteral("before"), before);
  entry.insert(QStringLiteral("after"), after);
  entry.insert(QStringLiteral("type"), type);
  return entry;
}

} // namespace

SessionDocumentPort::SessionDocumentPort(Engine::EditorSession *session,
                                         QObject *parent)
    : AI::DocumentPort(parent), m_session(session) {}

int SessionDocumentPort::revision() const {
  return m_session ? m_session->revision() : 0;
}

QString SessionDocumentPort::documentJson() const {
  return m_session ? m_session->documentJson() : QStringLiteral("{}");
}

bool SessionDocumentPort::applyCommand(const QJsonObject &command,
                                       const QString &actorId,
                                       QJsonObject *patch, QString *error) {
  if (!m_session) {
    if (error)
      *error = QStringLiteral("No open document.");
    return false;
  }
  QString commandError;
  Engine::Command cmd = Engine::Command::fromJson(command, &commandError);
  if (!commandError.isEmpty()) {
    if (error)
      *error = commandError;
    return false;
  }
  // The actor is stamped by the host, never by the model or HTTP caller.
  cmd.actor = Engine::Actor::pipeline(actorId, actorId);
  const auto result = m_session->apply(cmd);
  if (!result.ok()) {
    if (error)
      *error = result.error();
    return false;
  }
  if (patch)
    *patch = result.value().toJson();
  emit commandApplied(command, actorId, patch ? *patch : QJsonObject{});
  return true;
}

FeedChangePort::FeedChangePort(Engine::ChangeFeed *feed, QObject *parent)
    : QObject(parent), m_feed(feed) {}

QJsonArray FeedChangePort::toJson(const QVector<Engine::ChangeFeed::Event> &events) {
  QJsonArray array;
  for (const auto &event : events)
    array.append(event.patch.toJson());
  return array;
}

QJsonArray FeedChangePort::all() const {
  return m_feed ? toJson(m_feed->events()) : QJsonArray();
}

QJsonArray FeedChangePort::recent(int count) const {
  if (!m_feed)
    return {};
  const QVector<Engine::ChangeFeed::Event> events = m_feed->recent(count);
  QJsonArray array;
  for (const auto &event : events) {
    QJsonObject entry;
    entry.insert(QStringLiteral("actor"), event.patch.actor.id);
    entry.insert(QStringLiteral("stage"), event.patch.actor.displayName);
    entry.insert(QStringLiteral("revision"), event.patch.revision);
    entry.insert(QStringLiteral("ops"),
                 event.patch.toJson().value(QStringLiteral("ops")).toArray());
    array.append(entry);
  }
  return array;
}

LiveRetrievalPort::LiveRetrievalPort(std::function<QString()> documentJson,
                                     QObject *parent)
    : QObject(parent), m_documentJson(std::move(documentJson)) {}

QJsonArray LiveRetrievalPort::relevantBlocks(const QString &query,
                                             int limit) const {
  QJsonArray out;
  if (!m_documentJson)
    return out;
  // An empty query means "give the model an outline of every block".
  if (query.trimmed().isEmpty()) {
    const QJsonDocument all = QJsonDocument::fromJson(m_documentJson().toUtf8());
    for (const QJsonValue &value :
         all.object().value(QStringLiteral("blocks")).toArray()) {
      const QJsonObject block = value.toObject();
      QJsonObject entry;
      entry.insert(QStringLiteral("id"), block.value(QStringLiteral("id")));
      entry.insert(QStringLiteral("type"), block.value(QStringLiteral("type")));
      const QString text = block.value(QStringLiteral("text")).toString();
      entry.insert(QStringLiteral("text"),
                   text.size() > 80 ? text.left(80) : text);
      out.append(entry);
      if (out.size() >= limit)
        break;
    }
    return out;
  }
  QStringList terms;
  for (const QString &word : query.split(QRegularExpression(QStringLiteral(
                                  R"([^\p{L}\p{N}_]+)")),
                              Qt::SkipEmptyParts)) {
    if (word.size() >= 2)
      terms.append(word.toLower());
  }
  if (terms.isEmpty())
    return out;

  const QJsonDocument doc = QJsonDocument::fromJson(m_documentJson().toUtf8());
  struct Scored {
    double score = 0.0;
    QJsonObject block;
  };
  QVector<Scored> scored;
  for (const QJsonValue &value : doc.object().value(QStringLiteral("blocks")).toArray()) {
    const QJsonObject block = value.toObject();
    const QString text = block.value(QStringLiteral("text")).toString().toLower();
    if (text.isEmpty())
      continue;
    double score = 0.0;
    for (const QString &term : terms) {
      if (text.contains(term))
        score += 1.0 + (term.size() >= 4 ? 0.5 : 0.0);
    }
    if (score <= 0.0)
      continue;
    QJsonObject compact;
    compact.insert(QStringLiteral("id"), block.value(QStringLiteral("id")));
    compact.insert(QStringLiteral("type"), block.value(QStringLiteral("type")));
    const QString full = block.value(QStringLiteral("text")).toString();
    compact.insert(QStringLiteral("text"),
                   full.size() > 600 ? full.left(600) + QStringLiteral("...") : full);
    scored.append({score, compact});
  }
  std::sort(scored.begin(), scored.end(),
            [](const Scored &a, const Scored &b) { return a.score > b.score; });
  for (int i = 0; i < qMin(limit, scored.size()); ++i)
    out.append(scored.at(i).block);
  return out;
}

QJsonObject LiveRetrievalPort::block(const QString &id) const {
  if (!m_documentJson || id.isEmpty())
    return {};
  const QJsonDocument doc = QJsonDocument::fromJson(m_documentJson().toUtf8());
  for (const QJsonValue &value : doc.object().value(QStringLiteral("blocks")).toArray()) {
    QJsonObject block = value.toObject();
    if (block.value(QStringLiteral("id")).toString() == id)
      return block;
  }
  return {};
}

PluginManagerToolPort::PluginManagerToolPort(PluginManager *manager,
                                             QObject *parent)
    : QObject(parent), m_manager(manager) {}

QJsonObject PluginManagerToolPort::applyPluginTool(const QJsonObject &call) {
  return m_manager ? m_manager->applyPluginTool(call) : QJsonObject{};
}

ReplayResult replayCommands(const QString &originalJson,
                            const QVector<StagedCommand> &commands,
                            Engine::CapabilityGate *gate) {
  ReplayResult result;
  const QJsonDocument doc = QJsonDocument::fromJson(originalJson.toUtf8());
  if (!doc.isObject()) {
    result.error = QStringLiteral("The original document is not valid JSON.");
    return result;
  }

  Engine::EditorSession session;
  QString loadError;
  if (!session.loadJson(doc.object(), &loadError)) {
    result.error = loadError.isEmpty()
                       ? QStringLiteral("Could not reload the original document.")
                       : loadError;
    return result;
  }
  session.setActor(Engine::Actor::human(QStringLiteral("local-owner")));
  session.setCapabilityGate(gate);

  SessionDocumentPort port(&session);
  QJsonArray ops;
  QJsonObject lastPatch;
  for (const StagedCommand &entry : commands) {
    QJsonObject patch;
    QString error;
    if (!port.applyCommand(entry.command, entry.stageId, &patch, &error)) {
      result.error = QStringLiteral("Stage '%1': %2")
                         .arg(entry.stageId,
                              error.isEmpty() ? QStringLiteral("command rejected")
                                              : error);
      return result;
    }
    const QJsonArray patchOps = patch.value(QStringLiteral("ops")).toArray();
    for (const QJsonValue &op : patchOps)
      ops.append(op);
    lastPatch = patch;
  }

  result.ok = true;
  result.document = session.toJson();
  result.patch.insert(QStringLiteral("ops"), ops);
  result.patch.insert(QStringLiteral("actor"),
                      lastPatch.value(QStringLiteral("actor")));
  result.patch.insert(QStringLiteral("after"),
                      lastPatch.value(QStringLiteral("after")));
  return result;
}

QString agentLabel(const QString &agentId) {
  QStringList words = agentId.split(QLatin1Char('-'), Qt::SkipEmptyParts);
  for (QString &word : words) {
    if (!word.isEmpty())
      word[0] = word[0].toUpper();
  }
  return words.join(QLatin1Char(' '));
}

QVariantList diffDocuments(const QString &beforeJson,
                           const QString &afterJson) {
  const QVector<BlockInfo> before = parseBlocks(beforeJson);
  const QVector<BlockInfo> after = parseBlocks(afterJson);

  QHash<QString, BlockInfo> beforeById;
  QHash<QString, BlockInfo> afterById;
  for (const BlockInfo &block : before)
    beforeById.insert(block.id, block);
  for (const BlockInfo &block : after)
    afterById.insert(block.id, block);

  QVariantList changes;
  // Added and modified blocks, in the document's current order.
  for (const BlockInfo &block : after) {
    const auto it = beforeById.constFind(block.id);
    if (it == beforeById.constEnd()) {
      changes.append(changeEntry(block.id, QStringLiteral("added"), QString(),
                                 block.text, block.type));
    } else if (it->text != block.text || it->type != block.type) {
      changes.append(changeEntry(block.id, QStringLiteral("changed"), it->text,
                                 block.text, block.type));
    }
  }
  // Removed blocks, in their original order.
  for (const BlockInfo &block : before) {
    if (!afterById.contains(block.id)) {
      changes.append(changeEntry(block.id, QStringLiteral("removed"), block.text,
                                 QString(), block.type));
    }
  }
  return changes;
}

} // namespace Templnote::Adapter
