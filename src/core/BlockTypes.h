#pragma once

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
} // namespace Templnote
