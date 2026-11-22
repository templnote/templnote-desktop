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

// Windows reserves these base names (with any extension) for devices, so a
// file or folder using one of them cannot be created. The check is applied on
// every platform to keep names portable.
bool isWindowsReservedName(const QString &baseName) {
  const QString upper = baseName.trimmed().toUpper();

  if (upper == QLatin1String("CON") || upper == QLatin1String("PRN") ||
      upper == QLatin1String("AUX") || upper == QLatin1String("NUL")) {
    return true;
  }

  const bool numbered =
      upper.startsWith(QLatin1String("COM")) ||
      upper.startsWith(QLatin1String("LPT"));
  return numbered && upper.size() == 4 &&
         upper.at(3) >= QLatin1Char('1') &&
         upper.at(3) <= QLatin1Char('9');
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

QString configDirectory() {
  const QString env = qEnvironmentVariable("TEMPLNOTE_CONFIG_DIR");
  if (!env.isEmpty()) {
    QDir().mkpath(env);
    return env;
  }
  return location(QStandardPaths::AppConfigLocation);
}

QString pluginsDirectory() {
  const QString config = configDirectory();
  if (config.isEmpty())
    return {};
  const QString path = QDir(config).filePath(QStringLiteral("plugins"));
  QDir().mkpath(path);
  return path;
}

QString pluginSettingsDirectory() {
  const QString config = configDirectory();
  if (config.isEmpty())
    return {};
  const QString path = QDir(config).filePath(QStringLiteral("plugin-settings"));
  QDir().mkpath(path);
  return path;
}

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

  // Compare the base name only: "CON.txt" is just as reserved as "CON".
  if (isWindowsReservedName(result.section(QLatin1Char('.'), 0, 0)))
    result.prepend(QLatin1Char('_'));

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