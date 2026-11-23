#include "Types.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace Templnote::Engine {

namespace {
QString actorKindName(Actor::Kind kind) {
  switch (kind) {
  case Actor::Kind::Human:
    return QStringLiteral("human");
  case Actor::Kind::Agent:
    return QStringLiteral("agent");
  case Actor::Kind::Pipeline:
    return QStringLiteral("pipeline");
  case Actor::Kind::System:
    return QStringLiteral("system");
  case Actor::Kind::Import:
    return QStringLiteral("import");
  }
  return QStringLiteral("unknown");
}

Actor::Kind actorKindFromName(const QString &name) {
  if (name == QLatin1String("agent"))
    return Actor::Kind::Agent;
  if (name == QLatin1String("pipeline"))
    return Actor::Kind::Pipeline;
  if (name == QLatin1String("system"))
    return Actor::Kind::System;
  if (name == QLatin1String("import"))
    return Actor::Kind::Import;
  return Actor::Kind::Human;
}

QString opTypeName(Op::Type type) {
  switch (type) {
  case Op::Type::InsertText:
    return QStringLiteral("insertText");
  case Op::Type::DeleteRange:
    return QStringLiteral("deleteRange");
  case Op::Type::ReplaceRange:
    return QStringLiteral("replaceRange");
  case Op::Type::InsertBlock:
    return QStringLiteral("insertBlock");
  case Op::Type::RemoveBlock:
    return QStringLiteral("removeBlock");
  case Op::Type::MoveBlock:
    return QStringLiteral("moveBlock");
  case Op::Type::SetBlockType:
    return QStringLiteral("setBlockType");
  case Op::Type::SetHeadingLevel:
    return QStringLiteral("setHeadingLevel");
  case Op::Type::SetAlignment:
    return QStringLiteral("setAlignment");
  case Op::Type::SetMetadata:
    return QStringLiteral("setMetadata");
  case Op::Type::SetParent:
    return QStringLiteral("setParent");
  case Op::Type::ApplySnapshot:
    return QStringLiteral("applySnapshot");
  }
  return QStringLiteral("unknown");
}

Op::Type opTypeFromName(const QString &name) {
  if (name == QLatin1String("insertText"))
    return Op::Type::InsertText;
  if (name == QLatin1String("deleteRange"))
    return Op::Type::DeleteRange;
  if (name == QLatin1String("insertBlock"))
    return Op::Type::InsertBlock;
  if (name == QLatin1String("removeBlock"))
    return Op::Type::RemoveBlock;
  if (name == QLatin1String("moveBlock"))
    return Op::Type::MoveBlock;
  if (name == QLatin1String("setBlockType"))
    return Op::Type::SetBlockType;
  if (name == QLatin1String("setHeadingLevel"))
    return Op::Type::SetHeadingLevel;
  if (name == QLatin1String("setAlignment"))
    return Op::Type::SetAlignment;
  if (name == QLatin1String("setMetadata"))
    return Op::Type::SetMetadata;
  if (name == QLatin1String("setParent"))
    return Op::Type::SetParent;
  if (name == QLatin1String("applySnapshot"))
    return Op::Type::ApplySnapshot;
  return Op::Type::ReplaceRange;
}
} // namespace

QString Actor::kindName() const { return actorKindName(kind); }

QJsonObject Actor::toJson() const {
  QJsonObject json;
  json.insert(QStringLiteral("id"), id);
  json.insert(QStringLiteral("kind"), actorKindName(kind));
  json.insert(QStringLiteral("displayName"), displayName);
  return json;
}

Actor Actor::fromJson(const QJsonObject &json) {
  Actor a;
  a.id = json.value(QStringLiteral("id")).toString();
  a.kind = actorKindFromName(json.value(QStringLiteral("kind")).toString());
  a.displayName = json.value(QStringLiteral("displayName")).toString();
  return a;
}

QJsonObject Selection::toJson() const {
  QJsonObject json;
  json.insert(QStringLiteral("blockId"), blockId);
  json.insert(QStringLiteral("anchor"), anchor);
  json.insert(QStringLiteral("head"), head);
  return json;
}

Selection Selection::fromJson(const QJsonObject &json) {
  Selection s;
  s.blockId = json.value(QStringLiteral("blockId")).toString();
  s.anchor = json.value(QStringLiteral("anchor")).toInt();
  s.head = json.value(QStringLiteral("head")).toInt();
  return s;
}

QString Op::typeName() const { return opTypeName(type); }

QJsonObject Op::toJson() const {
  QJsonObject json;
  json.insert(QStringLiteral("type"), opTypeName(type));
  json.insert(QStringLiteral("blockId"), blockId);
  if (!afterId.isEmpty())
    json.insert(QStringLiteral("afterId"), afterId);
  if (position >= 0)
    json.insert(QStringLiteral("position"), position);
  if (length > 0)
    json.insert(QStringLiteral("length"), length);
  if (destination >= 0)
    json.insert(QStringLiteral("destination"), destination);
  if (!text.isEmpty())
    json.insert(QStringLiteral("text"), text);
  if (!fullText.isEmpty())
    json.insert(QStringLiteral("fullText"), fullText);
  if (blockType != 0)
    json.insert(QStringLiteral("blockType"), blockType);
  if (headingLevel != 0)
    json.insert(QStringLiteral("headingLevel"), headingLevel);
  if (!key.isEmpty())
    json.insert(QStringLiteral("key"), key);
  if (!payload.isUndefined())
    json.insert(QStringLiteral("payload"), payload);
  json.insert(QStringLiteral("lamport"),
              static_cast<qint64>(lamport));
  json.insert(QStringLiteral("actor"), actor.toJson());
  return json;
}

Op Op::fromJson(const QJsonObject &json) {
  Op op;
  op.type = opTypeFromName(json.value(QStringLiteral("type")).toString());
  op.blockId = json.value(QStringLiteral("blockId")).toString();
  op.afterId = json.value(QStringLiteral("afterId")).toString();
  op.position = json.value(QStringLiteral("position")).toInt(-1);
  op.length = json.value(QStringLiteral("length")).toInt(0);
  op.destination = json.value(QStringLiteral("destination")).toInt(-1);
  op.text = json.value(QStringLiteral("text")).toString();
  op.fullText = json.value(QStringLiteral("fullText")).toString();
  op.blockType = json.value(QStringLiteral("blockType")).toInt(0);
  op.headingLevel = json.value(QStringLiteral("headingLevel")).toInt(0);
  op.key = json.value(QStringLiteral("key")).toString();
  op.payload = json.value(QStringLiteral("payload"));
  op.lamport = static_cast<quint64>(
      json.value(QStringLiteral("lamport")).toInteger());
  op.actor = Actor::fromJson(json.value(QStringLiteral("actor")).toObject());
  return op;
}

QString Command::typeName() const { return typeName(type); }

QString Command::typeName(Type type) {
  switch (type) {
  case Type::InsertText:
    return QStringLiteral("insertText");
  case Type::DeleteRange:
    return QStringLiteral("deleteRange");
  case Type::ReplaceRange:
    return QStringLiteral("replaceRange");
  case Type::ToggleFormat:
    return QStringLiteral("toggleFormat");
  case Type::SetBlockType:
    return QStringLiteral("setBlockType");
  case Type::SetAlignment:
    return QStringLiteral("setAlignment");
  case Type::SetMetadata:
    return QStringLiteral("setMetadata");
  case Type::SplitBlock:
    return QStringLiteral("splitBlock");
  case Type::MergeWithPrevious:
    return QStringLiteral("mergeWithPrevious");
  case Type::InsertBlock:
    return QStringLiteral("insertBlock");
  case Type::RemoveBlock:
    return QStringLiteral("removeBlock");
  case Type::MoveBlock:
    return QStringLiteral("moveBlock");
  case Type::SetText:
    return QStringLiteral("setText");
  case Type::SetChecked:
    return QStringLiteral("setChecked");
  case Type::SetCalloutType:
    return QStringLiteral("setCalloutType");
  case Type::SetCodeLanguage:
    return QStringLiteral("setCodeLanguage");
  case Type::SetImageSource:
    return QStringLiteral("setImageSource");
  case Type::SetImageWidthRatio:
    return QStringLiteral("setImageWidthRatio");
  case Type::ApplyJson:
    return QStringLiteral("applyJson");
  }
  return QStringLiteral("unknown");
}

Command::Type Command::typeFromName(const QString &name, bool *ok) {
  if (ok)
    *ok = true;
  const Type all[] = {
      Type::InsertText,       Type::DeleteRange,      Type::ReplaceRange,
      Type::ToggleFormat,     Type::SetBlockType,     Type::SetAlignment,
      Type::SetMetadata,      Type::SplitBlock,       Type::MergeWithPrevious,
      Type::InsertBlock,      Type::RemoveBlock,      Type::MoveBlock,
      Type::SetText,          Type::SetChecked,       Type::SetCalloutType,
      Type::SetCodeLanguage,  Type::SetImageSource,   Type::SetImageWidthRatio,
      Type::ApplyJson};
  for (Type t : all) {
    if (name == typeName(t))
      return t;
  }
  if (ok)
    *ok = false;
  return Type::ReplaceRange;
}

QJsonObject Command::toJson() const {
  QJsonObject json;
  json.insert(QStringLiteral("type"), typeName());
  if (!blockId.isEmpty())
    json.insert(QStringLiteral("blockId"), blockId);
  if (!afterId.isEmpty())
    json.insert(QStringLiteral("afterId"), afterId);
  if (position >= 0)
    json.insert(QStringLiteral("position"), position);
  if (start >= 0)
    json.insert(QStringLiteral("start"), start);
  if (end >= 0)
    json.insert(QStringLiteral("end"), end);
  if (!text.isEmpty())
    json.insert(QStringLiteral("text"), text);
  if (!format.isEmpty())
    json.insert(QStringLiteral("format"), format);
  if (!alignment.isEmpty())
    json.insert(QStringLiteral("alignment"), alignment);
  if (!calloutType.isEmpty())
    json.insert(QStringLiteral("calloutType"), calloutType);
  if (!language.isEmpty())
    json.insert(QStringLiteral("language"), language);
  if (!imageSource.isEmpty())
    json.insert(QStringLiteral("imageSource"), imageSource);
  if (imageWidthRatio != 1.0)
    json.insert(QStringLiteral("imageWidthRatio"), imageWidthRatio);
  if (blockType != 0)
    json.insert(QStringLiteral("blockType"), blockType);
  if (headingLevel != 0)
    json.insert(QStringLiteral("headingLevel"), headingLevel);
  if (destination >= 0)
    json.insert(QStringLiteral("destination"), destination);
  if (type == Type::SetChecked)
    json.insert(QStringLiteral("checked"), checked);
  if (!metadata.isEmpty())
    json.insert(QStringLiteral("metadata"),
                QJsonObject::fromVariantMap(metadata));
  if (!actor.id.isEmpty())
    json.insert(QStringLiteral("actor"), actor.toJson());
  if (lamport > 0)
    json.insert(QStringLiteral("lamport"), static_cast<qint64>(lamport));
  if (!resultingBlockId.isEmpty())
    json.insert(QStringLiteral("resultingBlockId"), resultingBlockId);
  return json;
}

Command Command::fromJson(const QJsonObject &json, QString *error) {
  Command c;
  bool typeOk = false;
  c.type = typeFromName(json.value(QStringLiteral("type")).toString(), &typeOk);
  if (!typeOk) {
    if (error)
      *error = QStringLiteral("Unknown command type.");
    return c;
  }
  c.blockId = json.value(QStringLiteral("blockId")).toString();
  c.afterId = json.value(QStringLiteral("afterId")).toString();
  c.position = json.value(QStringLiteral("position")).toInt(-1);
  c.start = json.value(QStringLiteral("start")).toInt(-1);
  c.end = json.value(QStringLiteral("end")).toInt(-1);
  c.text = json.value(QStringLiteral("text")).toString();
  c.format = json.value(QStringLiteral("format")).toString();
  c.alignment = json.value(QStringLiteral("alignment")).toString();
  c.calloutType = json.value(QStringLiteral("calloutType")).toString();
  c.language = json.value(QStringLiteral("language")).toString();
  c.imageSource = json.value(QStringLiteral("imageSource")).toString();
  c.imageWidthRatio = json.value(QStringLiteral("imageWidthRatio")).toDouble(1.0);
  c.blockType = json.value(QStringLiteral("blockType")).toInt(0);
  c.headingLevel = json.value(QStringLiteral("headingLevel")).toInt(0);
  c.destination = json.value(QStringLiteral("destination")).toInt(-1);
  c.checked = json.value(QStringLiteral("checked")).toBool(false);
  c.metadata = json.value(QStringLiteral("metadata")).toObject().toVariantMap();
  if (json.contains(QStringLiteral("actor")))
    c.actor = Actor::fromJson(json.value(QStringLiteral("actor")).toObject());
  c.lamport = static_cast<quint64>(
      json.value(QStringLiteral("lamport")).toInteger());
  c.resultingBlockId = json.value(QStringLiteral("resultingBlockId")).toString();
  return c;
}

QJsonObject Patch::toJson() const {
  QJsonObject json;
  json.insert(QStringLiteral("revision"), revision);
  QJsonArray opsArray;
  for (const Op &op : ops)
    opsArray.append(op.toJson());
  json.insert(QStringLiteral("ops"), opsArray);
  json.insert(QStringLiteral("before"), before.toJson());
  json.insert(QStringLiteral("after"), after.toJson());
  json.insert(QStringLiteral("actor"), actor.toJson());
  json.insert(QStringLiteral("lamport"), static_cast<qint64>(lamport));
  json.insert(QStringLiteral("timestampMs"), timestampMs);
  return json;
}

Patch Patch::fromJson(const QJsonObject &json) {
  Patch p;
  p.revision = json.value(QStringLiteral("revision")).toInt();
  const QJsonArray opsArray = json.value(QStringLiteral("ops")).toArray();
  p.ops.reserve(opsArray.size());
  for (const QJsonValue &v : opsArray)
    p.ops.append(Op::fromJson(v.toObject()));
  p.before = Selection::fromJson(json.value(QStringLiteral("before")).toObject());
  p.after = Selection::fromJson(json.value(QStringLiteral("after")).toObject());
  p.actor = Actor::fromJson(json.value(QStringLiteral("actor")).toObject());
  p.lamport = static_cast<quint64>(
      json.value(QStringLiteral("lamport")).toInteger());
  p.timestampMs = static_cast<qint64>(
      json.value(QStringLiteral("timestampMs")).toInteger());
  return p;
}

QJsonObject SnapshotInfo::toJson() const {
  QJsonObject json;
  json.insert(QStringLiteral("id"), id);
  json.insert(QStringLiteral("revision"), revision);
  json.insert(QStringLiteral("timestampMs"), timestampMs);
  json.insert(QStringLiteral("actor"), actor.toJson());
  json.insert(QStringLiteral("label"), label);
  return json;
}

SnapshotInfo SnapshotInfo::fromJson(const QJsonObject &json) {
  SnapshotInfo s;
  s.id = json.value(QStringLiteral("id")).toString();
  s.revision = json.value(QStringLiteral("revision")).toInt();
  s.timestampMs = static_cast<qint64>(
      json.value(QStringLiteral("timestampMs")).toInteger());
  s.actor = Actor::fromJson(json.value(QStringLiteral("actor")).toObject());
  s.label = json.value(QStringLiteral("label")).toString();
  return s;
}

} // namespace Templnote::Engine
