#include "PluginManifest.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace Templnote {

namespace {
// Resolve a manifest-relative component/icon path to a URL. Filesystem paths
// must stay inside the plugin directory: relative traversal ("../") and
// absolute paths pointing elsewhere are rejected. Qt resource paths
// (":/..." or "qrc:/...") are always allowed because they are compiled into
// the binary rather than read from the user's filesystem.
QUrl safeComponentUrl(const QString &pluginDir, const QString &path) {
  if (path.isEmpty())
    return {};

  const bool declaredQrc = path.startsWith(QLatin1String("qrc:"));
  QString resourcePath = path;
  if (declaredQrc) {
    // Normalise qrc:/ and qrc:/// to the canonical :/ form.
    resourcePath = resourcePath.mid(3); // keep the leading ':'
    if (resourcePath.startsWith(QLatin1String("://")))
      resourcePath = QStringLiteral(":") + resourcePath.mid(3);
    if (!resourcePath.startsWith(QLatin1String(":/")))
      return {};
  }

  if (resourcePath.startsWith(QLatin1String(":/"))) {
    const QString cleaned = QDir::cleanPath(resourcePath);
    if (!cleaned.startsWith(QLatin1String(":/")))
      return {};
    return QUrl(QStringLiteral("qrc") + cleaned);
  }

  if (pluginDir.isEmpty())
    return {};
  const QString base = QDir::cleanPath(pluginDir);
  const QString resolved = QDir::isAbsolutePath(path)
                               ? QDir::cleanPath(path)
                               : QDir::cleanPath(base + QLatin1Char('/') + path);

  // This also rejects absolute paths outside the plugin dir: the computed
  // relative path then begins with "../" (or is absolute on other drives).
  const QString rel = QDir(base).relativeFilePath(resolved);
  if (rel == QLatin1String("..") || rel.startsWith(QStringLiteral("../")) ||
      QDir::isAbsolutePath(rel))
    return {};

  // The lexical check above is not enough: a symlink *inside* the plugin dir
  // can point at a file outside it while still looking like a relative path.
  // Resolve both sides canonically and reject anything that escapes. Missing
  // files have no canonical path and fall back to the lexical result above.
  const QString canonicalBase = QFileInfo(base).canonicalFilePath();
  const QString canonicalResolved = QFileInfo(resolved).canonicalFilePath();
  if (!canonicalBase.isEmpty() && !canonicalResolved.isEmpty()) {
    if (canonicalResolved != canonicalBase &&
        !canonicalResolved.startsWith(canonicalBase + QStringLiteral("/")))
      return {};
  }

  return QUrl::fromLocalFile(resolved);
}
} // namespace

bool PluginManifest::isValidId(const QString &id) {
  // `$` also matches just before a trailing newline in PCRE, which would let
  // "plugin\n" through and produce a settings file with a newline in its name.
  // `\z` is the true end-of-subject anchor.
  static const QRegularExpression idRegex(QStringLiteral("^[a-zA-Z0-9_-]+\\z"));
  return idRegex.match(id).hasMatch();
}

QUrl PluginManifest::settingsComponentUrl() const {
  return safeComponentUrl(m_pluginDir, m_settingsComponent);
}

QUrl PluginManifest::iconUrl() const {
  return safeComponentUrl(m_pluginDir, m_iconPath);
}

PluginManifest PluginManifest::fromJson(const QJsonObject &json,
                                       const QString &pluginDir,
                                       bool *ok,
                                       QString *error) {
  if (ok)
    *ok = false;
  if (error)
    error->clear();

  PluginManifest manifest;
  manifest.m_pluginDir = pluginDir;

  manifest.m_id = json.value(QStringLiteral("id")).toString().trimmed();
  if (manifest.m_id.isEmpty()) {
    if (error)
      *error = QStringLiteral("Plugin manifest is missing 'id'.");
    return manifest;
  }

  // Validate ID format (alphanumeric, hyphens, underscores)
  if (!isValidId(manifest.m_id)) {
    if (error)
      *error = QStringLiteral("Plugin ID '%1' contains invalid characters.").arg(manifest.m_id);
    return manifest;
  }

  manifest.m_name = json.value(QStringLiteral("name")).toString().trimmed();
  if (manifest.m_name.isEmpty())
    manifest.m_name = manifest.m_id;

  manifest.m_version = json.value(QStringLiteral("version")).toString(QStringLiteral("1.0.0"));
  manifest.m_description = json.value(QStringLiteral("description")).toString();
  manifest.m_author = json.value(QStringLiteral("author")).toString();
  manifest.m_bundled = json.value(QStringLiteral("bundled")).toBool(false);
  manifest.m_category = json.value(QStringLiteral("category")).toString();

  // Dependencies
  const QJsonArray depsArr = json.value(QStringLiteral("dependencies")).toArray();
  for (const QJsonValue &val : depsArr) {
    const QString dep = val.toString().trimmed();
    if (!dep.isEmpty())
      manifest.m_dependencies.append(dep);
  }

  // Capabilities
  const QJsonObject capsObj = json.value(QStringLiteral("capabilities")).toObject();

  // Theme capability
  if (capsObj.contains(QStringLiteral("theme")) && !capsObj.value(QStringLiteral("theme")).isNull()) {
    const QJsonObject themeObj = capsObj.value(QStringLiteral("theme")).toObject();
    manifest.m_hasTheme = true;
    manifest.m_theme.schemeKey = themeObj.value(QStringLiteral("schemeKey")).toString();
    if (manifest.m_theme.schemeKey.isEmpty())
      manifest.m_theme.schemeKey = manifest.m_id;

    manifest.m_theme.name = themeObj.value(QStringLiteral("name")).toObject();
    manifest.m_theme.darkPalette = themeObj.value(QStringLiteral("dark")).toObject();
    manifest.m_theme.lightPalette = themeObj.value(QStringLiteral("light")).toObject();
  }

  // Decorations capability
  if (capsObj.contains(QStringLiteral("decorations"))) {
    const QJsonArray decArr = capsObj.value(QStringLiteral("decorations")).toArray();
    for (const QJsonValue &decVal : decArr) {
      const QJsonObject decObj = decVal.toObject();
      PluginDecorationDef def;
      def.slot = decObj.value(QStringLiteral("slot")).toString();
      def.component = decObj.value(QStringLiteral("component")).toString();
      def.zOrder = decObj.value(QStringLiteral("zOrder")).toInt(0);
      if (!def.slot.isEmpty() && !def.component.isEmpty()) {
        def.componentUrl = safeComponentUrl(pluginDir, def.component);
        if (def.componentUrl.isEmpty()) {
          // Path escaped the plugin directory: drop this invalid entry.
          qWarning() << "PluginManifest: rejecting decoration component path"
                     << def.component << "for plugin" << manifest.m_id;
          continue;
        }
        manifest.m_decorations.append(def);
      }
    }
  }

  // Settings
  if (json.contains(QStringLiteral("settings"))) {
    const QJsonObject settingsObj = json.value(QStringLiteral("settings")).toObject();
    manifest.m_settingsComponent = settingsObj.value(QStringLiteral("component")).toString();
    manifest.m_settingsDefaults = settingsObj.value(QStringLiteral("defaults")).toObject();
  }

  // Icon
  manifest.m_iconPath = json.value(QStringLiteral("icon")).toString();

  // Infer category if empty
  if (manifest.m_category.isEmpty()) {
    if (manifest.m_hasTheme)
      manifest.m_category = QStringLiteral("theme");
    else if (!manifest.m_decorations.isEmpty())
      manifest.m_category = QStringLiteral("decoration");
    else
      manifest.m_category = QStringLiteral("general");
  }

  manifest.m_valid = true;
  if (ok)
    *ok = true;

  return manifest;
}

PluginManifest PluginManifest::fromFile(const QString &manifestPath,
                                       bool *ok,
                                       QString *error) {
  if (ok)
    *ok = false;
  if (error)
    error->clear();

  QFile file(manifestPath);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error)
      *error = QStringLiteral("Failed to open '%1': %2").arg(manifestPath, file.errorString());
    return {};
  }

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (doc.isNull() || !doc.isObject()) {
    if (error)
      *error = QStringLiteral("JSON error in '%1': %2").arg(manifestPath, parseError.errorString());
    return {};
  }

  const QString pluginDir = QFileInfo(manifestPath).absolutePath();
  return fromJson(doc.object(), pluginDir, ok, error);
}

} // namespace Templnote
