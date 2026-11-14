#pragma once

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QVector>
#include <qcontainerfwd.h>
#include <qproperty.h>
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

  Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)

  Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)

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
  Q_INVOKABLE QString splitBlock(const QString &id, int position,
                                 const QString &editorText = {});

  Q_INVOKABLE bool mergeWithPrevious(const QString &id,
                                     const QString &editorText = {});

  const Document &document() const;

  Q_INVOKABLE int indexOfBlock(const QString &id) const;

  // Block that should receive focus after the last undo/redo.
  Q_INVOKABLE QString lastHistoryBlockId() const;

  // undo redo
  Q_INVOKABLE bool undo();
  bool canUndo() const;

  Q_INVOKABLE bool redo();
  bool canRedo() const;

signals:
  void countChanged();
  void historyChanged();

private:
  struct HistoryEntry {
    enum class Kind { TextEdit, DocumentEdit };

    Kind kind = Kind::DocumentEdit;
    QString blockId;
    QString textBefore;
    QString textAfter;
    QString focusBeforeId;
    QString focusAfterId;
    QVector<Block> documentBefore;
    QVector<Block> documentAfter;
  };

  void saveHistory(const QVector<Block> &before,
                   const QString &focusBeforeId = {},
                   const QString &focusAfterId = {});
  void saveTextHistory(const QString &id, const QString &before,
                       const QString &after);
  void closeTextGroup();
  void applyHistoryEntry(const HistoryEntry &entry, bool undo);

  Document m_document;
  QVector<HistoryEntry> m_undoStack;
  QVector<HistoryEntry> m_redoStack;
  QString m_lastHistoryBlockId;

  // Consecutive single-character edits in the same block are merged into one
  // history entry until a word boundary or a short pause is reached.
  QString m_textGroupBlockId;
  QElapsedTimer m_textGroupTimer;
  bool m_textGroupOpen = false;
};
} // namespace Templnote
