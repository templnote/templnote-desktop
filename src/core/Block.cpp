#include "Block.h"

namespace Templnote {
QJsonObject Block::toJson() const {
  QJsonObject json;

  json.insert(QStringLiteral("id"), id);
  json.insert(QStringLiteral("parentId"), parentId);
  json.insert(QStringLiteral("type"), blockTypeName(type));
  json.insert(QStringLiteral("text"), text);
  json.insert(QStringLiteral("headingLevel"), headingLevel);

  if (!metadata.isEmpty())
    json.insert(QStringLiteral("metadata"), QJsonObject::fromVariantMap(metadata));

  return json;
}

Block Block::fromJson(const QJsonObject &json, bool *ok) {
  const auto fail = [ok]() {
    if (ok)
      *ok = false;
    return Block{};
  };

  const QString id = json.value(QStringLiteral("id")).toString();

  // Block ids are the identity used for focus, undo and (later) nesting. A
  // block without an id cannot be represented safely.
  if (id.isEmpty())
    return fail();

  bool typeKnown = false;
  const BlockType type =
      blockTypeFromName(json.value(QStringLiteral("type")).toString(), &typeKnown);

  // Unknown types (for example from a newer app version) degrade to a
  // paragraph instead of making the whole document unreadable.
  Block block;
  block.id = id;
  block.parentId = json.value(QStringLiteral("parentId")).toString();
  block.type = typeKnown ? type : BlockType::Paragraph;
  block.text = json.value(QStringLiteral("text")).toString();

  if (block.type == BlockType::Heading) {
    int level = json.value(QStringLiteral("headingLevel")).toInt(1);

    if (level < 1 || level > 6)
      level = 1;

    block.headingLevel = level;
  } else {
    block.headingLevel = 0;
  }

  block.metadata =
      json.value(QStringLiteral("metadata")).toObject().toVariantMap();

  if (ok)
    *ok = true;

  return block;
}
} // namespace Templnote