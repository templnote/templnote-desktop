#pragma once

#include "BlockTypes.h"

#include <QString>
#include <QUuid>
#include <QVariantMap>
#include <quuid.h>

namespace Templnote {
struct Block {
  QString id;
  QString parentId;
  BlockType type = BlockType::Paragraph;
  QString text;

  int headingLevel = 0;

  QVariantMap metadata;

  static Block create(BlockType type = BlockType::Paragraph,
                      const QString &text = {}) {
    Block block;

    block.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    block.type = type;
    block.text = text;

    if (type == BlockType::Heading)
      block.headingLevel = 1;

    return block;
  }
};
} // namespace Templnote
