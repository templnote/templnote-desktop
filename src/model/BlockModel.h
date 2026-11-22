#pragma once

#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QByteArray>
#include <QDateTime>
#include <QStringList>
#include <deque>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <qcontainerfwd.h>
#include <qproperty.h>
#include <qtmetamacros.h>

#include "../core/Document.h"

namespace Templnote {
class VisibleBlocksProxy;

class BlockModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int count READ count NOTIFY countChanged)
  Q_PROPERTY(QString currentFilePath READ currentFilePath NOTIFY
                 currentFilePathChanged)
  Q_PROPERTY(
      QString documentTitle READ documentTitle NOTIFY documentTitleChanged)
  Q_PROPERTY(bool isModified READ isModified NOTIFY modifiedChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
  Q_PROPERTY(
      QDateTime lastModified READ lastModified NOTIFY lastModifiedChanged)
  Q_PROPERTY(
      QString documentFolder READ documentFolder NOTIFY currentFilePathChanged)
  Q_PROPERTY(bool autosaveEnabled READ autosaveEnabled WRITE setAutosaveEnabled
                 NOTIFY autosaveEnabledChanged)
  Q_PROPERTY(bool fullWidth READ isFullWidth WRITE setFullWidth
                 NOTIFY fullWidthChanged)
  Q_PROPERTY(bool hasFullWidthSet READ hasFullWidthSet NOTIFY fullWidthChanged)
  // Increments on every content change; lets the QML find service reuse its
  // lowercased text cache until the document actually changes.
  Q_PROPERTY(int contentRevision READ contentRevision NOTIFY contentRevisionChanged)
  // A view of the document with nested column children filtered out. The
  // editor's ListView uses this so each layout occupies a single visible row;
  // columns render their own children via blocksOfColumn().
  Q_PROPERTY(QAbstractItemModel *visibleModel READ visibleModel CONSTANT)
public:
  enum Roles {
    BlockIdRole = Qt::UserRole + 1,
    ParentIdRole,
    BlockTypeRole,
    ContentRole,
    HeadingLevelRole,
    MetadataRole,
    AlignmentRole,
    CheckedRole,
    CalloutTypeRole,
    CodeLanguageRole,
    ImageSourceRole,
    ImageWidthRatioRole,
    NextBlockTypeRole,
    NumberIndexRole
  };

  Q_ENUM(Roles)

  // Upper bound for the document-wide undo stack.
  static constexpr int kMaxHistoryEntries = 1000;

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

  // Inserts a block directly after `afterId` and inherits its parentId. This is
  // what keeps a paragraph inserted inside a column inside that column instead
  // of silently promoting it to the top level.
  Q_INVOKABLE QString insertBlockAfter(const QString &afterId, int type,
                                       const QString &text);

  Q_INVOKABLE bool removeBlock(const QString &id);
  Q_INVOKABLE QString duplicateBlock(const QString &id);

  Q_INVOKABLE bool updateBlockText(const QString &id, const QString &text);

  Q_INVOKABLE bool changeBlockType(const QString &id, int type,
                                   int headdingLevel = 0);

  Q_INVOKABLE bool setBlockAlignment(const QString &id, const QString &align);

  Q_INVOKABLE bool setBlockChecked(const QString &id, bool checked);

  Q_INVOKABLE bool setBlockCalloutType(const QString &id, const QString &calloutType);

  Q_INVOKABLE bool setBlockCodeLanguage(const QString &id, const QString &language);

  Q_INVOKABLE bool setBlockImageSource(const QString &id, const QString &source);

  Q_INVOKABLE bool setBlockImageWidthRatio(const QString &id, double ratio);

  Q_INVOKABLE bool setBlockMetadata(const QString &id, const QVariantMap &metadata);

  // Column layouts. Every operation below is one atomic undo step.
  // Returns the id of the first content block, which should receive focus.
  // availableWidth is the total width the new columns should share (already
  // net of spacing); 0 keeps the legacy fixed reference-sheet width.
  Q_INVOKABLE QString createColumnsLayout(int row, int count,
                                          double availableWidth = 0.0);
  Q_INVOKABLE bool insertColumn(const QString &containerId,
                                const QString &afterColumnId);
  Q_INVOKABLE bool removeColumn(const QString &columnId);
  // Drag a column out to the editor. Its children become top-level blocks in
  // document order; if it was the layout's last column, the layout collapses
  // back to the paragraph it replaced. beforeVisibleRow < 0 appends at the end.
  Q_INVOKABLE bool extractColumnToEditor(const QString &columnId,
                                         int beforeVisibleRow = -1);
  // Move a column into another layout at the given column index.
  Q_INVOKABLE bool moveColumnToLayout(const QString &columnId,
                                      const QString &targetLayoutId,
                                      int targetIndex);
  Q_INVOKABLE bool moveBlockToColumn(const QString &blockId,
                                     const QString &targetColumnId, int row);
  Q_INVOKABLE bool setColumnWidth(const QString &columnId, double width);
  // Resize gestures call begin/end around a series of setColumnWidth() calls so
  // the whole drag becomes one undo step instead of one step per mouse move.
  Q_INVOKABLE bool beginColumnResize(const QString &containerId);
  Q_INVOKABLE bool endColumnResize();
  Q_INVOKABLE double columnWidth(const QString &columnId) const;
  Q_INVOKABLE QString appendBlockToColumn(const QString &columnId, int type = 0,
                                          const QString &text = {});
  Q_INVOKABLE bool moveColumn(const QString &columnId, int direction);
  Q_INVOKABLE bool moveColumnToIndex(const QString &columnId, int targetIndex);
  Q_INVOKABLE bool equalizeColumns(const QString &containerId);

  Q_INVOKABLE QStringList columnIdsOf(const QString &containerId) const;
  Q_INVOKABLE QStringList blocksOfColumn(const QString &columnId) const;
  Q_INVOKABLE QString columnIdOfBlock(const QString &blockId) const;
  Q_INVOKABLE QString parentBlockId(const QString &blockId) const;
  Q_INVOKABLE QString topLevelAncestorId(const QString &blockId) const;

  QAbstractItemModel *visibleModel() const;
  Q_INVOKABLE int visibleCount() const;
  Q_INVOKABLE int visibleRowOf(const QString &blockId) const;
  Q_INVOKABLE QString blockIdAtVisibleRow(int row) const;
  Q_INVOKABLE int sourceRowOfVisibleRow(int row) const;
  Q_INVOKABLE int sourceRowOfBlock(const QString &blockId) const;
  // Stable proxy exposing the blocks inside a single column, with the same
  // roles as the main model so the shared QML delegate can be reused.
  Q_INVOKABLE QAbstractItemModel *blocksModelForColumn(const QString &columnId);

  Q_INVOKABLE QString importAttachment(const QUrl &fileUrl);

  Q_INVOKABLE bool moveBlock(const QString &id, int destination);

  // splits blocks on pressing enter (onPressedEnter)
  Q_INVOKABLE QString splitBlock(const QString &id, int position,
                                 const QString &editorText = {});

  Q_INVOKABLE bool mergeWithPrevious(const QString &id,
                                     const QString &editorText = {});

  const Document &document() const;

  Q_INVOKABLE int indexOfBlock(const QString &id) const;

  // Id of the block at `row`, or an empty string when out of range.
  Q_INVOKABLE QString blockIdAt(int row) const;
  Q_INVOKABLE int blockTypeAtRow(int row) const;

  // Writes the document to disk shortly after the last edit. Disabled by
  // default; the application enables it in Main.qml.
  bool autosaveEnabled() const;
  void setAutosaveEnabled(bool enabled);

  // Block that should receive focus after the last undo/redo.
  Q_INVOKABLE QString lastHistoryBlockId() const;

  // Caret position to restore after the last undo/redo. -1 means "end of
  // text", which is also used for structural history steps.
  Q_INVOKABLE int lastHistoryCursorPosition() const;

  QString currentFilePath() const;
  QString documentTitle() const;
  QString documentFolder() const;
  QDateTime lastModified() const;
  bool isModified() const;
  QString lastError() const;

  // Renames the current file inside its folder. The name is sanitized and a
  // free "Name 2" variant is picked on collision. Only the base name is
  // accepted; a typed .templnote suffix is stripped.
  Q_INVOKABLE bool renameDocument(const QString &newBaseName);

  // Retargets the open document after its file was moved or renamed outside
  // this model (for example when the explorer renames its parent folder).
  // The in-memory document and unsaved edits are preserved.
  Q_INVOKABLE void adoptRenamedPath(const QString &newPath);

  // Writes the document as JSON. A missing .templnote extension is appended
  // and the parent directory is created when necessary. The write is atomic:
  // either the complete document is saved or the previous file is untouched.
  Q_INVOKABLE bool saveDocument(const QString &path);

  // Saves to currentFilePath(); fails when the document has never been saved.
  Q_INVOKABLE bool saveCurrentDocument();

  // Parses and validates the file before replacing the current document. On
  // failure the open document and its undo history are left untouched.
  Q_INVOKABLE bool loadDocument(const QString &path);

  // Replaces the current document with a single empty paragraph and saves it
  // to `path`. Used by FileExplorer for "New document".
  Q_INVOKABLE bool createDocument(const QString &path);

  // Drops the in-memory document and returns to the no-document state. The
  // file on disk is untouched; this is used when the open file is deleted or
  // when the workspace should show the home surface again.
  Q_INVOKABLE bool closeDocument();

  // undo redo
  Q_INVOKABLE bool undo();
  bool canUndo() const;

  Q_INVOKABLE bool redo();
  bool canRedo() const;

  bool isFullWidth() const;
  void setFullWidth(bool fullWidth);
  bool hasFullWidthSet() const;
  int contentRevision() const;

signals:
  void countChanged();
  void historyChanged();
  void currentFilePathChanged();
  void documentTitleChanged();
  void modifiedChanged();
  void lastModifiedChanged();
  void lastErrorChanged();
  void autosaveEnabledChanged();
  void documentLoaded();
  void documentCreated();
  void fullWidthChanged();
  void contentRevisionChanged();
  // Emitted when a column's stored width changes. QML uses it to refresh the
  // pane-width bindings instead of reacting to every dataChanged signal.
  void columnWidthsChanged();

private:
  void markModified();
  // Recomputes the dirty flag from the current history state instead of
  // assuming every undo/redo leaves unsaved changes behind.
  void syncModifiedState();
  bool hasUnsavedChanges() const;
  // Only numbered blocks have a meaningful numberIndex, so avoid notifying
  // every row after them when the numbering changes.
  void emitNumberIndexChanged(int firstRow);
  void setModified(bool modified);
  void scheduleAutosave();
  void autosave();
  void setCurrentFilePath(const QString &path);
  void setLastModified(const QDateTime &modified);
  bool setError(const QString &message);
  bool resetDocument(Document document);
  static QString folderForPath(const QString &path);
  struct PatchOp {
    enum class Type { Insert, Remove, Move, Update };

    Type type = Type::Insert;
    int from = -1;
    int to = -1;
    Block block;
    Block beforeBlock;
  };

  struct HistoryEntry {
    enum class Kind { TextEdit, StructuralEdit };

    Kind kind = Kind::StructuralEdit;
    QString blockId;
    QString textBefore;
    QString textAfter;
    QString focusBeforeId;
    QString focusAfterId;
    // State ids let the model tell whether an undo/redo landed exactly on the
    // state that was last written to disk.
    int stateBeforeId = 0;
    int stateAfterId = 0;
    QVector<PatchOp> patch;
  };

  // Column helpers. A layout is a Columns block whose direct children are
  // Column blocks; the blocks inside those columns carry the column id in
  // their parentId. Column blocks themselves are never nested.
  QString insertBlockInternal(int row, int type, const QString &text,
                              const QString &parentId);
  int layoutRowOf(const QString &blockId) const;
  bool reparentChildren(const QString &fromColumnId,
                        const QString &toColumnId,
                        QVector<PatchOp> &patch) const;
  QVector<PatchOp> widthPatch(const QVector<QString> &columnIds,
                              const QVector<double> &widths) const;
  // A column and its direct children, in document order. Keeping the subtree
  // together is what makes column reorders preserve document order.
  QStringList columnSubtreeIds(const QString &columnId) const;
  int subtreeEndRow(const QString &columnId) const;
  // Reorders the document into `desired` with the minimal set of Move ops.
  QVector<PatchOp> reorderPatch(const QStringList &desired) const;
  // Places `subtree` at `insertRow` (a row in the list without the subtree).
  QVector<PatchOp> moveSubtreePatch(const QStringList &subtree,
                                    int insertRow) const;
  // Drops the per-column proxies whose column no longer exists.
  void pruneColumnModels();

  void saveHistory(const QVector<PatchOp> &patch,
                   const QString &focusBeforeId = {},
                   const QString &focusAfterId = {});
  void saveTextHistory(const QString &id, const QString &before,
                       const QString &after);
  void closeTextGroup();
  void trimHistory();
  void applyHistoryEntry(const HistoryEntry &entry, bool undo);

  void applyPatch(const QVector<PatchOp> &patch, bool undo);
  bool applyPatchOp(const PatchOp &op, bool inverse);

  void invalidateStructureCaches();
  void cleanupOrphanedAttachments();
  void rebuildNumberIndices() const;

  Document m_document;
  VisibleBlocksProxy *m_visibleModel = nullptr;
  QHash<QString, VisibleBlocksProxy *> m_columnModels;
  bool m_columnResizeActive = false;
  QHash<QString, Block> m_columnResizeBefore;
  // A deque keeps history trimming O(1) after kMaxHistoryEntries is reached.
  std::deque<HistoryEntry> m_undoStack;
  std::deque<HistoryEntry> m_redoStack;
  QString m_lastHistoryBlockId;
  int m_lastHistoryCursorPosition = -1;

  // Every edit gets a new state id. Undo/redo restore the id stored on the
  // history entry, so returning to the saved state clears the dirty flag.
  int m_currentStateId = 0;
  int m_savedStateId = 0;
  int m_stateCounter = 0;
  int m_contentRevision = 0;
  bool m_savedFullWidth = false;
  bool m_savedHasFullWidth = false;

  QString m_currentFilePath;
  QString m_createdAt;
  QString m_documentFolder;
  QDateTime m_lastModified;
  bool m_modified = false;
  QString m_lastError;

  // Exact bytes and path of the last successful save. Autosave can skip
  // re-serializing/writing when the document content has not changed since.
  QByteArray m_lastSavedData;
  QString m_lastSavedPath;

  // Numbered-list indices are cached and rebuilt lazily after any structural
  // or block-type change instead of being recomputed on every role lookup.
  mutable QVector<int> m_numberIndices;
  mutable bool m_numberIndicesDirty = true;

  // blocksOfColumn() is called from QML on every dataChanged/rowsMoved while a
  // column is hovered or resized. Keep the child id lists cached and invalidate
  // them together with the rest of the structural caches.
  mutable QHash<QString, QStringList> m_columnChildren;

  // Consecutive single-character edits in the same block are merged into one
  // history entry until a word boundary or a short pause is reached.
  QString m_textGroupBlockId;
  QElapsedTimer m_textGroupTimer;
  bool m_textGroupOpen = false;

  // Debounced autosave. Saves once the user stops typing for a short while,
  // so a crash or a closed window never silently loses the document.
  QTimer m_autosaveTimer;
  bool m_autosaveEnabled = false;
};
} // namespace Templnote
