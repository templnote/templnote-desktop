#include "DocumentListModel.h"

#include "AppPaths.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QVariantMap>
#include <algorithm>
#include <utility>

namespace Templnote {
DocumentListModel::DocumentListModel(QObject *parent)
    : DocumentListModel(AppPaths::documentsDirectory(), parent) {}

DocumentListModel::DocumentListModel(const QString &directory, QObject *parent)
    : QAbstractListModel(parent),
      m_rootDirectory(QDir::cleanPath(directory)),
      m_directory(QDir::cleanPath(directory)) {
  if (!m_rootDirectory.isEmpty())
    QDir().mkpath(m_rootDirectory);

  m_watcher = new QFileSystemWatcher(this);
  setupWatcher();
  refresh();

  connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
          [this](const QString &) { refresh(); });
}

void DocumentListModel::setupWatcher() {
  const QStringList watched = m_watcher->directories();

  for (const QString &path : watched) {
    if (path != m_directory)
      m_watcher->removePath(path);
  }

  if (!m_directory.isEmpty() && QDir(m_directory).exists() &&
      !m_watcher->directories().contains(m_directory)) {
    m_watcher->addPath(m_directory);
  }
}

bool DocumentListModel::isInsideRoot(const QString &path) const {
  if (m_rootDirectory.isEmpty())
    return false;

  const QString root = QDir::cleanPath(m_rootDirectory);
  const QString candidate =
      QDir::cleanPath(QFileInfo(path).absoluteFilePath());

  return candidate == root ||
         candidate.startsWith(root + QLatin1Char('/'));
}

int DocumentListModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : m_entries.size();
}

QVariant DocumentListModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
    return {};

  const Entry &entry = m_entries[index.row()];

  switch (role) {
  case NameRole:
    return entry.name;
  case FilePathRole:
    return entry.filePath;
  case IsFolderRole:
    return entry.isFolder;
  case ModifiedRole:
    return entry.modified;
  case CurrentRole:
    return !entry.isFolder && !m_currentFilePath.isEmpty() &&
           QFileInfo(entry.filePath).absoluteFilePath() ==
               QFileInfo(m_currentFilePath).absoluteFilePath();
  case Qt::DisplayRole:
    return entry.name;
  default:
    return {};
  }
}

QHash<int, QByteArray> DocumentListModel::roleNames() const {
  return {{NameRole, "name"},
          {FilePathRole, "filePath"},
          {IsFolderRole, "isFolder"},
          {ModifiedRole, "modified"},
          {CurrentRole, "isCurrent"}};
}

int DocumentListModel::count() const { return m_entries.size(); }

QString DocumentListModel::directory() const { return m_directory; }

QString DocumentListModel::relativeDirectory() const {
  if (m_rootDirectory.isEmpty() || m_directory.isEmpty())
    return {};

  const QString relative =
      QDir(m_rootDirectory).relativeFilePath(m_directory);

  if (relative == QLatin1String(".") || relative.isEmpty() ||
      relative.startsWith(QLatin1String("..")) ||
      QDir::isAbsolutePath(relative)) {
    return {};
  }

  return QDir::cleanPath(relative);
}

QVariantList DocumentListModel::breadcrumbs() const {
  QVariantList crumbs;

  if (m_rootDirectory.isEmpty())
    return crumbs;

  const QString relative = relativeDirectory();

  if (!relative.isEmpty()) {
    QString accumulated = m_rootDirectory;
    const QStringList parts = relative.split(QLatin1Char('/'),
                                             Qt::SkipEmptyParts);

    for (const QString &part : parts) {
      accumulated = QDir(accumulated).filePath(part);
      crumbs.append(QVariantMap{{QStringLiteral("name"), part},
                                {QStringLiteral("path"), accumulated}});
    }
  }

  return crumbs;
}

QString DocumentListModel::currentFilePath() const {
  return m_currentFilePath;
}

void DocumentListModel::setCurrentFilePath(const QString &path) {
  const QString normalized =
      path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();

  if (m_currentFilePath == normalized)
    return;

  const bool hadCurrent = !m_currentFilePath.isEmpty();
  m_currentFilePath = normalized;

  if ((hadCurrent || !normalized.isEmpty()) && !m_entries.isEmpty()) {
    emit dataChanged(index(0), index(m_entries.size() - 1), {CurrentRole});
  }

  emit currentFilePathChanged();
}

void DocumentListModel::refresh() {
  setupWatcher();

  QVector<Entry> folders;
  QVector<Entry> files;

  if (!m_directory.isEmpty()) {
    const QFileInfoList entries = QDir(m_directory).entryInfoList(
        QDir::Dirs | QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
        QDir::Name);

    folders.reserve(entries.size());
    files.reserve(entries.size());

    for (const QFileInfo &info : entries) {
      if (info.isDir()) {
        folders.append({info.fileName(), info.absoluteFilePath(),
                        info.lastModified(), true});
      } else if (info.suffix().compare(QStringLiteral("templnote"),
                                      Qt::CaseInsensitive) == 0) {
        files.append({info.completeBaseName(), info.absoluteFilePath(),
                      info.lastModified(), false});
      }
    }

    std::sort(folders.begin(), folders.end(),
              [](const Entry &left, const Entry &right) {
                return left.name.localeAwareCompare(right.name) < 0;
              });

    // Most recently modified documents first, matching the previous behavior.
    std::sort(files.begin(), files.end(),
              [](const Entry &left, const Entry &right) {
                return left.modified > right.modified;
              });
  }

  QVector<Entry> combined = std::move(folders);
  combined.append(files);

  const bool countChangedNow = combined.size() != m_entries.size();

  beginResetModel();
  m_entries = std::move(combined);
  endResetModel();

  if (countChangedNow)
    emit countChanged();
}

bool DocumentListModel::enterFolder(const QString &path) {
  const QFileInfo info(path);

  if (!info.exists() || !info.isDir() || !isInsideRoot(info.absoluteFilePath()))
    return false;

  const QString absolute = QDir::cleanPath(info.absoluteFilePath());

  if (m_directory == absolute)
    return true;

  m_directory = absolute;
  setupWatcher();
  emit directoryChanged();
  refresh();

  return true;
}

bool DocumentListModel::navigateTo(const QString &path) {
  return enterFolder(path);
}

bool DocumentListModel::goUp() {
  if (m_directory.isEmpty() || m_directory == m_rootDirectory)
    return false;

  return enterFolder(QFileInfo(m_directory).absolutePath());
}

QString DocumentListModel::createUntitledPath() const {
  if (m_directory.isEmpty())
    return {};

  return AppPaths::uniqueDocumentPath(m_directory, QStringLiteral("Untitled"));
}

QString DocumentListModel::createUntitledFolder() {
  if (m_directory.isEmpty())
    return {};

  const QString target = AppPaths::uniqueDirectoryPath(
      m_directory, QStringLiteral("Untitled Folder"));

  if (target.isEmpty() || !QDir().mkpath(target))
    return {};

  refresh();
  return target;
}

bool DocumentListModel::renameFolder(const QString &path,
                                     const QString &newName) {
  const QFileInfo info(path);

  if (!info.exists() || !info.isDir() || !isInsideRoot(info.absoluteFilePath()))
    return false;

  QString base = AppPaths::sanitizeFileName(newName);

  if (base.isEmpty())
    base = QStringLiteral("Untitled Folder");

  if (base == info.fileName())
    return true;

  const QString target =
      AppPaths::uniqueDirectoryPath(info.absolutePath(), base);

  if (target.isEmpty())
    return false;

  QDir parent(info.absolutePath());

  if (!parent.rename(info.fileName(), QFileInfo(target).fileName()))
    return false;

  const QString oldPath = QDir::cleanPath(info.absoluteFilePath());
  const QString newPath = QDir::cleanPath(target);

  // If the explorer was inside the renamed folder, follow it.
  if (m_directory == oldPath || m_directory.startsWith(oldPath + '/')) {
    m_directory = newPath + m_directory.mid(oldPath.size());
    setupWatcher();
    emit directoryChanged();
  }

  refresh();
  emit pathRenamed(oldPath, newPath);
  return true;
}

QString DocumentListModel::renameDocument(const QString &path,
                                         const QString &newName) {
  const QFileInfo info(path);

  if (!info.exists() || !info.isFile() || !isInsideRoot(info.absoluteFilePath()))
    return {};

  QString base = newName.trimmed();

  if (base.endsWith(QStringLiteral(".templnote"), Qt::CaseInsensitive))
    base.chop(10);

  base = AppPaths::sanitizeFileName(base);

  if (base.isEmpty())
    base = QStringLiteral("Untitled");

  if (base == info.completeBaseName())
    return info.absoluteFilePath();

  const QString target =
      AppPaths::uniqueDocumentPath(info.absolutePath(), base);

  if (target.isEmpty())
    return {};

  if (!QFile::rename(info.absoluteFilePath(), target))
    return {};

  // Best-effort update of the JSON title field if valid JSON
  QFile file(target);
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QJsonParseError parseError{};
    const QJsonDocument parsed =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error == QJsonParseError::NoError && parsed.isObject()) {
      QJsonObject json = parsed.object();
      json.insert(QStringLiteral("title"),
                  QFileInfo(target).completeBaseName());

      QSaveFile output(target);
      if (output.open(QIODevice::WriteOnly | QIODevice::Text)) {
        output.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
        output.commit();
      }
    }
  }

  const QString oldPath = QDir::cleanPath(info.absoluteFilePath());
  const QString newPath = QDir::cleanPath(target);

  if (m_currentFilePath == oldPath) {
    setCurrentFilePath(newPath);
  }

  refresh();
  emit pathRenamed(oldPath, newPath);
  return newPath;
}

QString DocumentListModel::duplicateDocument(const QString &path) {
  const QFileInfo info(path);

  if (!info.exists() || !info.isFile() || !isInsideRoot(info.absoluteFilePath()))
    return {};

  const QString base = info.completeBaseName();
  const QString target =
      AppPaths::uniqueDocumentPath(info.absolutePath(), base);

  if (target.isEmpty())
    return {};

  if (!QFile::copy(info.absoluteFilePath(), target))
    return {};

  // Best-effort update of the JSON title field in the copied file
  const QString newTitle = QFileInfo(target).completeBaseName();
  QFile file(target);
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QJsonParseError parseError{};
    const QJsonDocument parsed =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error == QJsonParseError::NoError && parsed.isObject()) {
      QJsonObject json = parsed.object();
      json[QStringLiteral("title")] = newTitle;
      QSaveFile output(target);
      if (output.open(QIODevice::WriteOnly | QIODevice::Text)) {
        output.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
        output.commit();
      }
    }
  }

  refresh();
  return QDir::cleanPath(target);
}

bool DocumentListModel::deleteEntry(const QString &path) {
  const QFileInfo info(path);

  if (!info.exists() || !isInsideRoot(info.absoluteFilePath()))
    return false;

  // Root directory itself cannot be deleted.
  if (info.absoluteFilePath() == m_rootDirectory)
    return false;

  const QString absolute = QDir::cleanPath(info.absoluteFilePath());

  if (info.isDir()) {
    // If folder contains files/subdirectories, move them to the root directory
    QDir dir(absolute);
    const QFileInfoList entries =
        dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QFileInfo &child : entries) {
      if (child.isDir()) {
        const QString target =
            AppPaths::uniqueDirectoryPath(m_rootDirectory, child.fileName());
        if (!target.isEmpty()) {
          QDir().rename(child.absoluteFilePath(), target);
        }
      } else {
        const QString base = child.completeBaseName();
        const QString target =
            AppPaths::uniqueDocumentPath(m_rootDirectory, base);
        if (!target.isEmpty()) {
          QFile::rename(child.absoluteFilePath(), target);
        }
      }
    }

    // If the explorer is inside the folder being deleted, jump up to parent or root.
    if (m_directory == absolute || m_directory.startsWith(absolute + '/')) {
      m_directory = QDir::cleanPath(info.absolutePath());
      if (!isInsideRoot(m_directory))
        m_directory = m_rootDirectory;
      setupWatcher();
      emit directoryChanged();
    }

    // Now remove the folder itself
    if (!QDir(absolute).removeRecursively()) {
      if (!QFile::moveToTrash(absolute))
        return false;
    }
  } else {
    // For single files, move to trash (or remove if trash unsupported)
    if (!QFile::moveToTrash(absolute)) {
      if (!QFile::remove(absolute))
        return false;
    }
  }

  refresh();
  emit pathRemoved(absolute);
  return true;
}

QString DocumentListModel::mostRecentDocumentPath() const {
  for (const Entry &entry : m_entries) {
    if (!entry.isFolder)
      return entry.filePath;
  }

  return {};
}

QString DocumentListModel::filePathAt(int row) const {
  if (row < 0 || row >= m_entries.size())
    return {};

  return m_entries[row].filePath;
}

QVariantList DocumentListModel::searchEntries(const QString &query) const {
  const QString needle = query.trimmed();

  if (needle.isEmpty() || m_rootDirectory.isEmpty())
    return {};

  QVector<Entry> folders;
  QVector<Entry> files;
  QHash<QString, QString> containingPaths;

  QDirIterator iterator(
      m_rootDirectory,
      QDir::Dirs | QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
      QDirIterator::Subdirectories);

  while (iterator.hasNext()) {
    iterator.next();

    const QFileInfo info = iterator.fileInfo();
    const bool isFolder = info.isDir();

    if (!isFolder &&
        info.suffix().compare(QStringLiteral("templnote"),
                              Qt::CaseInsensitive) != 0) {
      continue;
    }

    // Display the same name the explorer list uses for documents.
    const QString name =
        isFolder ? info.fileName() : info.completeBaseName();

    if (!name.contains(needle, Qt::CaseInsensitive))
      continue;

    const QString absolutePath = QDir::cleanPath(info.absoluteFilePath());
    QString relativeParent =
        QDir(m_rootDirectory).relativeFilePath(info.absolutePath());

    if (relativeParent == QStringLiteral("."))
      relativeParent.clear();

    containingPaths.insert(absolutePath, relativeParent);

    const Entry entry{name, absolutePath, info.lastModified(), isFolder};

    if (isFolder)
      folders.append(entry);
    else
      files.append(entry);
  }

  const auto lessThan = [](const Entry &left, const Entry &right) {
    const int byName = left.name.localeAwareCompare(right.name);

    if (byName != 0)
      return byName < 0;

    return left.filePath.localeAwareCompare(right.filePath) < 0;
  };

  std::sort(folders.begin(), folders.end(), lessThan);
  std::sort(files.begin(), files.end(), lessThan);

  QVariantList results;
  results.reserve(folders.size() + files.size());

  for (const Entry &entry : std::as_const(folders)) {
    results.append(QVariantMap{
        {QStringLiteral("name"), entry.name},
        {QStringLiteral("filePath"), entry.filePath},
        {QStringLiteral("isFolder"), entry.isFolder},
        {QStringLiteral("relativePath"), containingPaths.value(entry.filePath)},
    });
  }

  for (const Entry &entry : std::as_const(files)) {
    results.append(QVariantMap{
        {QStringLiteral("name"), entry.name},
        {QStringLiteral("filePath"), entry.filePath},
        {QStringLiteral("isFolder"), entry.isFolder},
        {QStringLiteral("relativePath"), containingPaths.value(entry.filePath)},
    });
  }

  return results;
}
} // namespace Templnote