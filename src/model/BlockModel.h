#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QDateTime>
#include <deque>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <qcontainerfwd.h>
#include <qproperty.h>
#include <qtmetamacros.h>

#include "../core/Document.h"

namespace Templnote {
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

  Q_INVOKABLE bool removeBlock(const QString &id);

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

  void invalidateNumberIndices();
  void rebuildNumberIndices() const;

  Document m_document;
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
