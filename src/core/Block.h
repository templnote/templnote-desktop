#pragma once

#include "BlockTypes.h"

#include <QJsonObject>
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

    if (type == BlockType::Table) {
      QVariantList cols;
      cols.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("c1")}, {QStringLiteral("width"), 160}});
      cols.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("c2")}, {QStringLiteral("width"), 160}});
      cols.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("c3")}, {QStringLiteral("width"), 160}});
      QVariantList rows;
      for (int r = 1; r <= 3; ++r) {
        QVariantMap row;
        row[QStringLiteral("id")] = QStringLiteral("r%1").arg(r);
        QVariantMap cells;
        cells[QStringLiteral("c1")] = QStringLiteral("");
        cells[QStringLiteral("c2")] = QStringLiteral("");
        cells[QStringLiteral("c3")] = QStringLiteral("");
        row[QStringLiteral("cells")] = cells;
        rows.append(row);
      }
      QVariantMap tableData;
      tableData[QStringLiteral("columns")] = cols;
      tableData[QStringLiteral("rows")] = rows;
      block.metadata[QStringLiteral("table")] = tableData;
    }

    return block;
  }

  QJsonObject toJson() const;
  static Block fromJson(const QJsonObject &json, bool *ok = nullptr);
};
} // namespace Templnote
