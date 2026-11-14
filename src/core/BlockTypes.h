#pragma once

#include <QString>
#include <QtGlobal>
#include <qtypes.h>

namespace Templnote {
enum class BlockType : qint8 {
  Paragraph = 0,
  Heading = 1,
  Bullet = 2,
  NumberedList = 3,
  Todo = 4,
  Quote = 5,
  Code = 6,
  Divider = 7,
  Image = 8,
  Toggle = 9,
  Callout = 10,
  Table = 11
};

constexpr bool isValidBlockType(int value) {
  return value >= static_cast<int>(BlockType::Paragraph) &&
         value <= static_cast<int>(BlockType::Table);
}

// Block types are stored by name in .templnote files so that reordering or
// extending the enum never corrupts existing documents.
inline QString blockTypeName(BlockType type) {
  switch (type) {
  case BlockType::Paragraph:
    return QStringLiteral("paragraph");
  case BlockType::Heading:
    return QStringLiteral("heading");
  case BlockType::Bullet:
    return QStringLiteral("bullet");
  case BlockType::NumberedList:
    return QStringLiteral("numberedList");
  case BlockType::Todo:
    return QStringLiteral("todo");
  case BlockType::Quote:
    return QStringLiteral("quote");
  case BlockType::Code:
    return QStringLiteral("code");
  case BlockType::Divider:
    return QStringLiteral("divider");
  case BlockType::Image:
    return QStringLiteral("image");
  case BlockType::Toggle:
    return QStringLiteral("toggle");
  case BlockType::Callout:
    return QStringLiteral("callout");
  case BlockType::Table:
    return QStringLiteral("table");
  }

  return QStringLiteral("paragraph");
}

inline BlockType blockTypeFromName(const QString &name, bool *ok = nullptr) {
  if (ok)
    *ok = true;

  if (name == QLatin1String("paragraph"))
    return BlockType::Paragraph;
  if (name == QLatin1String("heading"))
    return BlockType::Heading;
  if (name == QLatin1String("bullet"))
    return BlockType::Bullet;
  if (name == QLatin1String("numberedList"))
    return BlockType::NumberedList;
  if (name == QLatin1String("todo"))
    return BlockType::Todo;
  if (name == QLatin1String("quote"))
    return BlockType::Quote;
  if (name == QLatin1String("code"))
    return BlockType::Code;
  if (name == QLatin1String("divider"))
    return BlockType::Divider;
  if (name == QLatin1String("image"))
    return BlockType::Image;
  if (name == QLatin1String("toggle"))
    return BlockType::Toggle;
  if (name == QLatin1String("callout"))
    return BlockType::Callout;
  if (name == QLatin1String("table"))
    return BlockType::Table;

  if (ok)
    *ok = false;

  return BlockType::Paragraph;
}
} // namespace Templnote
