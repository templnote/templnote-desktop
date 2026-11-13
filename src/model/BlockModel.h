#pragma once

#include <QAbstractListModel>
#include <qhashfunctions.h>
#include <qtmetamacros.h>

#include "../core/Document.h"

namespace Templnote {
class BlockModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
  enum Roles {
    BlockIdRole = Qt::UserRole + 1,
    ParentIdRole,
    BlockTypeRole,
    ContentRole,
    HeadingLevelRole,
    MetadataRole,
    NextBlockTypeRole,
    NumberIndexRole
  };

  Q_ENUM(Roles)

  explicit BlockModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;

  int count() const;

  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;

  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE QString appendBlock(int type, const QString &text = {});

  Q_INVOKABLE bool insertBlock(int row, int type, const QString &text = {});

  Q_INVOKABLE bool removeBlock(const QString &id);

  Q_INVOKABLE bool updateBlockText(const QString &id, const QString &text);

  Q_INVOKABLE bool changeBlockType(const QString &id, int type,
                                   int headdingLevel = 0);

  Q_INVOKABLE bool moveBlock(const QString &id, int destination);

  // splits blocks on pressing enter (onPressedEnter)
  Q_INVOKABLE QString splitBlock(const QString &id, int position);

  Q_INVOKABLE bool mergeWithPrevious(const QString &id);

  const Document &document() const;

signals:
  void countChanged();

private:
  Document m_document;
};
} // namespace Templnote
