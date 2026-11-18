#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QVariantList>
#include <QVector>
#include <atomic>
#include <memory>
#include <qtmetamacros.h>

namespace Templnote {
// Lists the folders and .templnote files in one directory below the user
// document root. Navigation is breadcrumb-based: the model exposes the current
// directory and the path segments above it.
class DocumentListModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int count READ count NOTIFY countChanged)
  Q_PROPERTY(QString directory READ directory NOTIFY directoryChanged)
  Q_PROPERTY(QString relativeDirectory READ relativeDirectory NOTIFY
                 directoryChanged)
  Q_PROPERTY(
      QVariantList breadcrumbs READ breadcrumbs NOTIFY directoryChanged)
  Q_PROPERTY(QString currentFilePath READ currentFilePath WRITE
                 setCurrentFilePath NOTIFY currentFilePathChanged)

public:
  enum Roles {
    NameRole = Qt::UserRole + 1,
    FilePathRole,
    IsFolderRole,
    ModifiedRole,
    CurrentRole
  };

  Q_ENUM(Roles)

  explicit DocumentListModel(QObject *parent = nullptr);
  explicit DocumentListModel(const QString &directory,
                             QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  int count() const;
  QString directory() const;
  QString relativeDirectory() const;
  QVariantList breadcrumbs() const;
  QString currentFilePath() const;

  void setCurrentFilePath(const QString &path);

  // Re-scans the current directory.
  Q_INVOKABLE void refresh();

  // Navigation. Paths outside the model root are rejected.
  Q_INVOKABLE bool enterFolder(const QString &path);
  Q_INVOKABLE bool navigateTo(const QString &path);
  Q_INVOKABLE bool goUp();

  // Returns a free document path in the current directory.
  Q_INVOKABLE QString createUntitledPath() const;

  // Creates "Untitled Folder" (or the next free suffix) in the current
  // directory and returns its absolute path.
  Q_INVOKABLE QString createUntitledFolder();

  // Renames a folder inside the root. Collisions are auto-suffixed.
  Q_INVOKABLE bool renameFolder(const QString &path, const QString &newName);

  // Renames an existing file inside the root. Returns the new path or empty on failure.
  Q_INVOKABLE QString renameDocument(const QString &path, const QString &newName);

  // Duplicates an existing document in the same directory with an auto-suffixed name.
  // Returns the new document path or empty on failure.
  Q_INVOKABLE QString duplicateDocument(const QString &path);

  // Deletes a folder or file by moving it to the system trash.
  Q_INVOKABLE bool deleteEntry(const QString &path);

  // Path of the most recently modified document in the current directory.
  // Folders are ignored.
  Q_INVOKABLE QString mostRecentDocumentPath() const;

  // Same, but searches the whole document tree. Used by session restore so a
  // document inside a subfolder is still found when no saved path exists.
  Q_INVOKABLE QString mostRecentDocumentPathRecursive() const;

  // Path of the entry at `row`, or an empty string when out of range.
  Q_INVOKABLE QString filePathAt(int row) const;

  // Recursively searches every folder and .templnote file below the model
  // root by name (case-insensitive substring match). Each result is a map
  // with name, filePath, isFolder and relativePath (the containing folder
  // relative to the root, empty for root-level entries). An empty query
  // returns an empty list.
  Q_INVOKABLE QVariantList searchEntries(const QString &query) const;

  // Runs the recursive scan on a worker thread and emits searchCompleted for
  // the most recent request. The UI can safely call this on every keystroke.
  Q_INVOKABLE void searchEntriesAsync(const QString &query);

signals:
  void countChanged();
  void directoryChanged();
  void currentFilePathChanged();

  // Emitted before a refresh triggered by the filesystem watcher. The view can
  // preserve its scroll position across the model reset that follows.
  void refreshedExternally();

  // Emitted after a file or folder has been renamed on disk. The search index
  // uses this to move every indexed path below the old location.
  void pathRenamed(const QString &oldPath, const QString &newPath);

  // Emitted after a file or folder has been removed. Folders may have moved
  // their children elsewhere, so the index also re-syncs with disk.
  void pathRemoved(const QString &path);

  // `query` is echoed back so callers can discard stale results.
  void searchCompleted(const QString &query, const QVariantList &results);

private:
  static QVariantList searchEntriesInRoot(
      const QString &rootDirectory, const QString &needle,
      const std::atomic_bool *cancelled = nullptr);

  struct Entry {
    QString name;
    QString filePath;
    QDateTime modified;
    bool isFolder = false;
  };

  void setupWatcher();
  bool isInsideRoot(const QString &path) const;

  QString m_rootDirectory;
  QString m_directory;
  QString m_currentFilePath;
  QVector<Entry> m_entries;
  QFileSystemWatcher *m_watcher = nullptr;
  quint64 m_searchGeneration = 0;
  std::shared_ptr<std::atomic_bool> m_searchCancel;
};
} // namespace Templnote