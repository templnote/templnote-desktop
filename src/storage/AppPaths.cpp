#include "AppPaths.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace Templnote {
namespace AppPaths {
namespace {
constexpr auto kDocumentExtension = ".templnote";
constexpr auto kDocumentsFolder = "Templnote";

QString location(QStandardPaths::StandardLocation type) {
  return QStandardPaths::writableLocation(type);
}
} // namespace

QString documentsDirectory() {
  const QString documents = location(QStandardPaths::DocumentsLocation);

  if (documents.isEmpty())
    return {};

  return QDir(documents).filePath(QString::fromLatin1(kDocumentsFolder));
}

QString ensureDocumentsDirectory(QString *error) {
  const QString directory = documentsDirectory();

  if (error)
    error->clear();

  if (directory.isEmpty() || !QDir().mkpath(directory)) {
    if (error)
      *error = QStringLiteral("Could not create the Templnote documents "
                              "folder.");
    return {};
  }

  return directory;
}

QString configDirectory() { return location(QStandardPaths::AppConfigLocation); }

QString cacheDirectory() {
  return location(QStandardPaths::AppLocalDataLocation);
}

QString autosaveDirectory() {
  const QString cache = cacheDirectory();

  return cache.isEmpty() ? QString() : QDir(cache).filePath(QStringLiteral("autosave"));
}

QString attachmentsDirectory() {
  const QString cache = cacheDirectory();
  if (cache.isEmpty())
    return {};
  const QString path = QDir(cache).filePath(QStringLiteral("attachments"));
  QDir().mkpath(path);
  return path;
}

QString sanitizeFileName(const QString &name) {
  QString result = name.trimmed();

  // Drop characters that are invalid on common file systems or that would
  // change the meaning of the path.
  result.replace(QRegularExpression(QStringLiteral(R"([/\\:*?"<>|\x00-\x1f])")),
                 QStringLiteral(" "));

  while (result.contains(QStringLiteral("  ")))
    result.replace(QStringLiteral("  "), QStringLiteral(" "));

  while (result.startsWith(QLatin1Char('.')))
    result.remove(0, 1);

  while (result.endsWith(QLatin1Char(' ')) ||
         result.endsWith(QLatin1Char('.')))
    result.chop(1);

  return result.trimmed();
}

QString uniqueDocumentPath(const QString &directory, const QString &baseName) {
  if (directory.isEmpty())
    return {};

  QString base = sanitizeFileName(baseName);

  if (base.isEmpty())
    base = QStringLiteral("Untitled");

  QDir dir(directory);
  QString candidate =
      dir.filePath(base + QString::fromLatin1(kDocumentExtension));

  int suffix = 2;

  while (QFileInfo::exists(candidate)) {
    candidate = dir.filePath(base + QLatin1Char(' ') +
                             QString::number(suffix) +
                             QString::fromLatin1(kDocumentExtension));
    ++suffix;
  }

  return QDir::cleanPath(candidate);
}

QString uniqueDocumentPath(const QString &baseName) {
  const QString directory = ensureDocumentsDirectory();

  if (directory.isEmpty())
    return {};

  return uniqueDocumentPath(directory, baseName);
}

QString uniqueDirectoryPath(const QString &parentDirectory,
                            const QString &baseName) {
  if (parentDirectory.isEmpty())
    return {};

  QString base = sanitizeFileName(baseName);

  if (base.isEmpty())
    base = QStringLiteral("Untitled Folder");

  QDir dir(parentDirectory);
  QString candidate = dir.filePath(base);

  int suffix = 2;

  while (QFileInfo::exists(candidate)) {
    candidate = dir.filePath(base + QLatin1Char(' ') + QString::number(suffix));
    ++suffix;
  }

  return QDir::cleanPath(candidate);
}
} // namespace AppPaths
} // namespace Templnote