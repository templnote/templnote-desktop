#include "PluginManager.h"

#include <QColor>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaType>
#include <QSettings>
#include <QVersionNumber>
#include <algorithm>

#include "storage/AppPaths.h"

namespace Templnote {

namespace {
constexpr auto kSettingsGroup = "plugins";
constexpr auto kEnabledKey = "enabled";
constexpr auto kFirstRunKey = "initialized";
// Ids we have ever seen, so a bundled plugin copied back after the user
// deleted its directory is not mistaken for a brand-new plugin.
constexpr auto kKnownPluginsKey = "knownPlugins";
// Written into every installed bundled plugin directory. Discovery consults
// it when this run cannot find the bundled source (qrc / env / dev path).
constexpr auto kBundledMarkerFileName = ".templnote-bundled";

// Always use AppPaths::configDirectory() so TEMPLNOTE_CONFIG_DIR is honoured
// even when PluginManager is constructed outside main() (e.g. in tests).
QString appSettingsPath() {
  const QString dir = AppPaths::configDirectory();
  return dir.isEmpty() ? QString()
                       : QDir(dir).filePath(QStringLiteral("Templnote.conf"));
}

// Prefer the canonical path so symlinked plugin directories cannot be used to
// claim they came from the bundled install source. Fall back to a clean
// absolute path before the directory exists (freshly copied plugins).
QString canonicalDirPath(const QString &path) {
  const QString canonical = QFileInfo(path).canonicalFilePath();
  return canonical.isEmpty() ? QDir::cleanPath(QFileInfo(path).absoluteFilePath())
                             : canonical;
}

// setPluginSetting() is a generic API, so a QML component could otherwise use
// it to write "themeOverrides" and bypass the QColor validation performed by
// setThemeOverride(). Validate every colour value in that structure too.
// Empty strings are NOT valid here: removal of a single override is only
// allowed through setThemeOverride(), which has its own explicit empty-value
// handling.
bool isValidColorValue(const QVariant &value) {
  const QString text = value.toString();
  return !text.isEmpty() && QColor::fromString(text).isValid();
}

QString bundledMarkerPath(const QString &pluginDir) {
  return QDir(pluginDir).filePath(QString::fromLatin1(kBundledMarkerFileName));
}

bool markerDeclaresId(const QString &pluginDir, const QString &pluginId) {
  QFile marker(bundledMarkerPath(pluginDir));
  if (!marker.open(QIODevice::ReadOnly))
    return false;
  return QString::fromUtf8(marker.readAll()).trimmed() == pluginId;
}

// Corrupt (or hand-edited) settings files must not be able to inject invalid
// palettes into active themes. Drop every bad colour on load; an empty mode
// or empty override map is removed entirely so defaults can take over.
void sanitizeThemeOverrides(QJsonObject &settings) {
  if (!settings.contains(QStringLiteral("themeOverrides")))
    return;

  const QJsonObject overrides = settings.value(QStringLiteral("themeOverrides")).toObject();
  QJsonObject cleanOverrides;
  for (auto modeIt = overrides.constBegin(); modeIt != overrides.constEnd(); ++modeIt) {
    const QJsonObject mode = modeIt.value().toObject();
    QJsonObject cleanMode;
    for (auto colorIt = mode.constBegin(); colorIt != mode.constEnd(); ++colorIt) {
      if (isValidColorValue(colorIt.value().toVariant()))
        cleanMode.insert(colorIt.key(), colorIt.value());
    }
    if (!cleanMode.isEmpty())
      cleanOverrides.insert(modeIt.key(), cleanMode);
  }

  if (cleanOverrides.isEmpty())
    settings.remove(QStringLiteral("themeOverrides"));
  else
    settings.insert(QStringLiteral("themeOverrides"), cleanOverrides);
}

bool isValidThemeOverrides(const QVariant &value) {
  QVariantMap root;
  if (value.metaType().id() == QMetaType::QJsonObject) {
    root = value.toJsonObject().toVariantMap();
  } else if (value.canConvert<QVariantMap>()) {
    root = value.toMap();
  } else if (value.canConvert<QString>()) {
    QJsonParseError err;
    const QJsonDocument doc =
        QJsonDocument::fromJson(value.toString().toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
      return false;
    root = doc.object().toVariantMap();
  } else {
    return false;
  }

  for (auto it = root.cbegin(); it != root.cend(); ++it) {
    if (!it.value().canConvert<QVariantMap>())
      return false;
    const QVariantMap mode = it.value().toMap();
    for (auto colorIt = mode.cbegin(); colorIt != mode.cend(); ++colorIt) {
      if (!isValidColorValue(colorIt.value()))
        return false;
    }
  }
  return true;
}

QString agentManifestPath(const QString &pluginId) {
  const QString dir = AppPaths::pluginsDirectory();
  if (dir.isEmpty() || !PluginManifest::isValidId(pluginId))
    return {};
  return QDir(dir).filePath(pluginId + QStringLiteral("/manifest.json"));
}

bool isAgentCreated(const QString &manifestPath) {
  if (manifestPath.isEmpty() || !QFileInfo::exists(manifestPath))
    return false;
  QFile f(manifestPath);
  if (!f.open(QIODevice::ReadOnly))
    return false;
  return QJsonDocument::fromJson(f.readAll())
      .object()
      .value(QStringLiteral("agentCreated"))
      .toBool(false);
}

QString approvalFilePath() {
  const QString dir = AppPaths::configDirectory();
  return dir.isEmpty() ? QString()
                       : QDir(dir).filePath(QStringLiteral("plugin-approvals.json"));
}
} // namespace

PluginManager::PluginManager(QObject *parent)
    : QObject(parent), m_model(new PluginsModel(this)) {
  installBundledPlugins();
  discoverPlugins();
  loadEnabledState();
  rebuildActiveState();

  // Load persisted approvals for agent-authored plugins.
  const QString path = approvalFilePath();
  if (!path.isEmpty() && QFileInfo::exists(path)) {
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
      const QJsonObject approvals = QJsonDocument::fromJson(f.readAll()).object();
      for (auto it = approvals.constBegin(); it != approvals.constEnd(); ++it) {
        if (it.value().toBool())
          m_approvedPlugins.insert(it.key());
      }
    }
  }
}

QAbstractListModel *PluginManager::pluginsModel() const {
  return m_model;
}

QVariantList PluginManager::activeThemes() const {
  return m_activeThemes;
}

QVariantMap PluginManager::activeDecorations() const {
  return m_activeDecorations;
}

int PluginManager::stateRevision() const { return m_stateRevision; }

int PluginManager::settingsRevision() const { return m_settingsRevision; }

bool PluginManager::knownPlugin(const QString &id) const {
  return PluginManifest::isValidId(id) && m_manifests.contains(id);
}

bool PluginManager::isBundledPluginDir(const QString &dirPath,
                                       const QString &pluginId) const {
  const QString key = canonicalDirPath(dirPath);
  if (m_bundledPluginDirs.contains(key) &&
      m_bundledDirExpectedId.value(key) == pluginId)
    return true;
  // Persisted provenance. The id must still match, so a user plugin with a
  // copied marker but a different manifest id is rejected.
  return markerDeclaresId(dirPath, pluginId);
}

QString PluginManager::settingsFilePath(const QString &pluginId) const {
  // Never build a path from an unvalidated id: a crafted value such as
  // "../other" must not escape the plugin-settings directory.
  if (!knownPlugin(pluginId))
    return {};
  const QString dir = AppPaths::pluginSettingsDirectory();
  return dir.isEmpty() ? QString()
                       : QDir(dir).filePath(pluginId + QStringLiteral(".json"));
}

void PluginManager::loadPluginSettings(const QString &pluginId) const {
  if (m_settingsCache.contains(pluginId))
    return;

  const QString path = settingsFilePath(pluginId);
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    // Check if manifest has default settings
    QJsonObject defaults = m_manifests.contains(pluginId)
                               ? m_manifests.value(pluginId).settingsDefaults()
                               : QJsonObject();
    sanitizeThemeOverrides(defaults);
    m_settingsCache[pluginId] = defaults;
    return;
  }

  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
  if (doc.isObject()) {
    QJsonObject obj = doc.object();
    // Fill in defaults for missing keys
    if (m_manifests.contains(pluginId)) {
      const QJsonObject defaults = m_manifests.value(pluginId).settingsDefaults();
      for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        if (!obj.contains(it.key()))
          obj.insert(it.key(), it.value());
      }
    }
    // Never trust palette data that came from disk (issue #3).
    sanitizeThemeOverrides(obj);
    m_settingsCache[pluginId] = obj;
  } else {
    // A malformed (or non-object) settings file silently fell back to
    // defaults before; surface the parse error with the plugin id.
    if (err.error != QJsonParseError::NoError) {
      qWarning() << "PluginManager: failed to parse settings for plugin"
                 << pluginId << "from" << path << ":" << err.errorString();
    }
    QJsonObject defaults = m_manifests.contains(pluginId)
                               ? m_manifests.value(pluginId).settingsDefaults()
                               : QJsonObject();
    sanitizeThemeOverrides(defaults);
    m_settingsCache[pluginId] = defaults;
  }
}

void PluginManager::savePluginSettings(const QString &pluginId) {
  if (!m_settingsCache.contains(pluginId))
    return;

  const QString path = settingsFilePath(pluginId);
  QFile file(path);
  if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    const QJsonDocument doc(m_settingsCache.value(pluginId));
    file.write(doc.toJson(QJsonDocument::Indented));
  }
}

bool PluginManager::copyRecursively(const QString &srcFilePath, const QString &tgtFilePath) {
  const QFileInfo srcFileInfo(srcFilePath);
  if (srcFileInfo.isDir()) {
    if (!QDir().mkpath(tgtFilePath))
      return false;
    const QDir sourceDir(srcFilePath);
    const QStringList fileNames = sourceDir.entryList(QDir::Files | QDir::Dirs |
                                                      QDir::NoDotAndDotDot | QDir::Hidden |
                                                      QDir::System);
    for (const QString &fileName : fileNames) {
      const QString newSrcFilePath = sourceDir.filePath(fileName);
      const QString newTgtFilePath = QDir(tgtFilePath).filePath(fileName);
      if (!copyRecursively(newSrcFilePath, newTgtFilePath))
        return false;
    }
  } else {
    QDir().mkpath(QFileInfo(tgtFilePath).path());
    if (QFile::exists(tgtFilePath))
      QFile::remove(tgtFilePath);
    if (!QFile::copy(srcFilePath, tgtFilePath))
      return false;
  }
  return true;
}

void PluginManager::installBundledPlugins() {
  const QString targetBase = AppPaths::pluginsDirectory();
  if (targetBase.isEmpty())
    return;

  // Search for bundled plugins in Qt resources, environment, or dev path
  QString resourceBase = QStringLiteral(":/qt/qml/Templnote/bundled-plugins");
  if (!QDir(resourceBase).exists()) {
    const QString envDir = qEnvironmentVariable("TEMPLNOTE_BUNDLED_PLUGINS_DIR");
    if (!envDir.isEmpty() && QDir(envDir).exists()) {
      resourceBase = envDir;
    } else {
      const QString devDir = QDir::current().filePath(QStringLiteral("bundled-plugins"));
      if (QDir(devDir).exists()) {
        resourceBase = devDir;
      } else {
        const QString appDevDir = QDir(QCoreApplication::applicationDirPath())
                                      .filePath(QStringLiteral("../../bundled-plugins"));
        if (QDir(appDevDir).exists())
          resourceBase = appDevDir;
      }
    }
  }

  QDir resDir(resourceBase);
  if (!resDir.exists())
    return;

  const QStringList pluginDirs = resDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString &dirName : pluginDirs) {
    const QString srcDir = resDir.filePath(dirName);
    const QString destDir = QDir(targetBase).filePath(dirName);

    // Never clear the destination when it is the source itself (e.g. a
    // misconfigured bundling dir pointing at the installed plugins dir).
    const QString srcCanonical = QFileInfo(srcDir).canonicalFilePath();
    const QString destCanonical = QFileInfo(destDir).canonicalFilePath();
    if (!srcCanonical.isEmpty() && srcCanonical == destCanonical)
      continue;

    bool srcOk = false;
    const PluginManifest srcManifest = PluginManifest::fromFile(
        QDir(srcDir).filePath(QStringLiteral("manifest.json")), &srcOk);

    // If destination does not exist, copy entire plugin
    if (!QDir(destDir).exists()) {
      if (copyRecursively(srcDir, destDir)) {
        if (srcOk && srcManifest.isValid())
          m_newBundledIds.insert(srcManifest.id());
      } else {
        qWarning() << "PluginManager: failed to install bundled plugin from"
                   << srcDir << "to" << destDir;
      }
    } else {
      // Check version in manifest
      bool okDest = false;
      const PluginManifest destManifest = PluginManifest::fromFile(
          QDir(destDir).filePath(QStringLiteral("manifest.json")), &okDest);

      // Numeric comparison: lexicographic strings would treat 1.10.0 as
      // older than 1.2.0 and silently skip bundled plugin updates.
      const auto srcVersion = QVersionNumber::fromString(srcManifest.version());
      const auto destVersion = QVersionNumber::fromString(destManifest.version());

      if (srcOk && (!okDest ||
                    QVersionNumber::compare(srcVersion, destVersion) > 0)) {
        // Replace, never merge: copyRecursively only overwrites files that
        // exist in the source, so files removed in the new version must be
        // cleared first or they would linger as stale leftovers.
        if (!QDir(destDir).removeRecursively()) {
          qWarning() << "PluginManager: failed to clear old bundled plugin"
                     << "directory:" << destDir;
          continue;
        }
        if (!copyRecursively(srcDir, destDir)) {
          qWarning() << "PluginManager: failed to install bundled plugin from"
                     << srcDir << "to" << destDir;
        }
      }
    }

    // Record provenance only now that the directory exists: canonicalFilePath
    // can then resolve symlinks the same way discoverPlugins() will.
    if (QDir(destDir).exists()) {
      const QString destKey = canonicalDirPath(destDir);
      m_bundledPluginDirs.insert(destKey);
      if (srcOk && srcManifest.isValid()) {
        // Persist the provenance in the installed directory itself. On a later
        // run the bundled source may be gone (different env / release layout),
        // but discovery must still know this directory came from a bundle.
        QFile marker(bundledMarkerPath(destDir));
        if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
          marker.write(srcManifest.id().toUtf8());
          marker.write("\n");
        } else {
          qWarning() << "PluginManager: failed to write bundled marker in"
                     << destDir;
        }
        m_bundledDirExpectedId.insert(destKey, srcManifest.id());
      }
    }
  }

  // One-time migration: the three old paper plugins were merged into a single
  // "paper-background" plugin. Remove their installed copies so they cannot
  // keep rendering stale layers or show up as duplicate cards. Only the known
  // obsolete ids are touched, and only directories carrying our provenance
  // marker qualify, so user plugins and bundled plugins whose source is simply
  // unavailable this run (see provenance test) are left alone.
  const QStringList obsoletePaperIds{
      QStringLiteral("paper-texture"),
      QStringLiteral("paper-gradients"),
      QStringLiteral("paper-wallpaper")};
  bool unifiedPaperShipped = false;
  for (const QString &dirName : pluginDirs) {
    bool ok = false;
    const PluginManifest manifest = PluginManifest::fromFile(
        QDir(resDir.filePath(dirName)).filePath(QStringLiteral("manifest.json")),
        &ok);
    if (ok && manifest.id() == QStringLiteral("paper-background"))
      unifiedPaperShipped = true;
  }

  if (unifiedPaperShipped) {
    QDir installedDir(targetBase);
    const QStringList installedSubdirs =
        installedDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &subdir : installedSubdirs) {
      const QString dir = installedDir.filePath(subdir);
      QFile marker(bundledMarkerPath(dir));
      if (!marker.open(QIODevice::ReadOnly))
        continue;
      const QString id = QString::fromUtf8(marker.readAll()).trimmed();
      marker.close();
      if (!obsoletePaperIds.contains(id))
        continue;

      const QString key = canonicalDirPath(dir);
      if (!QDir(dir).removeRecursively()) {
        qWarning() << "PluginManager: failed to remove obsolete bundled plugin"
                   << dir;
        continue;
      }
      m_bundledPluginDirs.remove(key);
      m_bundledDirExpectedId.remove(key);
      m_newBundledIds.remove(id);
    }
  }
}

void PluginManager::discoverPlugins() {
  m_manifests.clear();
  m_pluginDirs.clear();
  const QString pluginsDir = AppPaths::pluginsDirectory();
  if (pluginsDir.isEmpty())
    return;

  QDir dir(pluginsDir);
  const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

  // One card / settings page per id. When two directories claim the same id,
  // the bundled copy wins: a manifest's "bundled" field is self-asserted and
  // cannot be trusted for install/uninstall decisions.
  QHash<QString, PluginItem> byId;
  for (const QString &subdir : subdirs) {
    const QString manifestPath = dir.filePath(subdir + QStringLiteral("/manifest.json"));
    if (!QFileInfo::exists(manifestPath))
      continue;

    bool ok = false;
    QString error;
    PluginManifest manifest = PluginManifest::fromFile(manifestPath, &ok, &error);
    if (!ok || !manifest.isValid())
      continue;

    // Decide bundled-ness from installation provenance, never from JSON.
    // The marker file makes this work even when this run has no bundled source
    // (see installBundledPlugins()); the id check still rejects spoofed dirs.
    const bool bundled = isBundledPluginDir(dir.filePath(subdir), manifest.id());
    manifest.setBundled(bundled);

    // Track every directory claiming this id, including duplicate copies that
    // lose the card-selection race below. Uninstall removes all of them so a
    // leftover duplicate cannot resurrect the card on the rescan (issue #8).
    m_pluginDirs[manifest.id()].append(dir.filePath(subdir));

    PluginItem item;
    item.manifest = manifest;
    item.enabled = m_enabledPlugins.contains(manifest.id());

    auto existing = byId.constFind(manifest.id());
    if (existing != byId.constEnd()) {
      if (bundled && !existing->manifest.isBundled()) {
        qWarning() << "PluginManager: duplicate plugin id" << manifest.id()
                   << "in" << subdir << "- preferring bundled copy over user copy";
        byId.insert(manifest.id(), item);
      } else {
        qWarning() << "PluginManager: duplicate plugin id" << manifest.id()
                   << "in" << subdir << "- skipping";
      }
      continue;
    }
    byId.insert(manifest.id(), item);
  }

  QVector<PluginItem> items;
  items.reserve(byId.size());
  for (auto it = byId.constBegin(); it != byId.constEnd(); ++it) {
    m_manifests.insert(it.key(), it.value().manifest);
    items.append(it.value());
  }

  // Sort plugins by name
  std::sort(items.begin(), items.end(), [](const PluginItem &a, const PluginItem &b) {
    return a.manifest.name().compare(b.manifest.name(), Qt::CaseInsensitive) < 0;
  });

  m_model->setPlugins(items);
  emit pluginsChanged();
}

void PluginManager::loadEnabledState() {
  QSettings settings(appSettingsPath(), QSettings::IniFormat);
  settings.beginGroup(QString::fromLatin1(kSettingsGroup));

  const QStringList knownList =
      settings.value(QString::fromLatin1(kKnownPluginsKey)).toStringList();
  QSet<QString> knownIds(knownList.begin(), knownList.end());

  if (!settings.contains(QString::fromLatin1(kFirstRunKey))) {
    // First run: enable all discovered bundled plugins
    for (auto it = m_manifests.constBegin(); it != m_manifests.constEnd(); ++it) {
      if (it.value().isBundled())
        m_enabledPlugins.insert(it.key());
    }
    settings.setValue(QString::fromLatin1(kFirstRunKey), true);
  } else {
    const QStringList enabledList = settings.value(QString::fromLatin1(kEnabledKey)).toStringList();
    m_enabledPlugins = QSet<QString>(enabledList.begin(), enabledList.end());
  }

  // A bundled plugin shipped in a later update is genuinely new (its
  // directory was just copied in), so enable it even though the one-time
  // "initialized" flag is already set. An id we have seen before is NOT new:
  // the user may have disabled it and then deleted its directory, after which
  // the installer copied it back in. Re-enabling that would discard the user's
  // choice (issue #10).
  for (const QString &id : m_newBundledIds) {
    if (!knownIds.contains(id))
      m_enabledPlugins.insert(id);
  }

  // Persist the union of everything we have ever discovered/copied so the
  // decision above survives restarts and config-dir changes.
  for (auto it = m_manifests.constBegin(); it != m_manifests.constEnd(); ++it)
    knownIds.insert(it.key());
  knownIds.unite(m_newBundledIds);
  QStringList knownOut(knownIds.begin(), knownIds.end());
  knownOut.sort();
  settings.setValue(QString::fromLatin1(kKnownPluginsKey), knownOut);
  settings.endGroup();

  saveEnabledState();

  // Update model enabled states
  for (auto it = m_manifests.constBegin(); it != m_manifests.constEnd(); ++it) {
    m_model->updatePluginEnabled(it.key(), m_enabledPlugins.contains(it.key()));
  }
}

void PluginManager::saveEnabledState() {
  QSettings settings(appSettingsPath(), QSettings::IniFormat);
  settings.beginGroup(QString::fromLatin1(kSettingsGroup));
  QStringList list(m_enabledPlugins.begin(), m_enabledPlugins.end());
  list.sort();
  settings.setValue(QString::fromLatin1(kEnabledKey), list);
  settings.endGroup();
}

bool PluginManager::enablePlugin(const QString &id) {
  if (!m_manifests.contains(id))
    return false;

  // Only agent-authored plugins require explicit approval. Ordinary user
  // plugins keep the original opt-in behaviour.
  const QString manifestPath =
      agentManifestPath(id);
  if (!manifestPath.isEmpty() && isAgentCreated(manifestPath) &&
      !isPluginApproved(id)) {
    m_lastPluginError = QStringLiteral("Plugin needs approval before enabling.");
    emit pluginErrorChanged();
    emit pluginNeedsApproval(id);
    return false;
  }

  // Resolve the whole dependency chain, not just the direct dependencies.
  const QStringList missing = missingDependencies(id);

  // A dependency with no manifest can never be enabled. Refuse the whole
  // operation instead of leaving the plugin enabled with a broken graph, and
  // do NOT emit dependenciesRequired: no dependency was auto-enabled here and
  // a notice would be a lie (issue #4).
  for (const QString &depId : missing) {
    if (!m_manifests.contains(depId))
      return false;
  }

  // Enable disabled dependencies deepest-first (post-order) so a dependency's
  // own prerequisites come online before it does. `visited` breaks cycles.
  QStringList toEnable;
  QSet<QString> visited;
  visited.insert(id); // a cycle back to the target needs no action
  const QStringList directDeps = m_manifests.value(id).dependencies();
  for (const QString &depId : directDeps)
    collectDependenciesToEnable(depId, visited, toEnable);

  for (const QString &depId : toEnable) {
    m_enabledPlugins.insert(depId);
    m_model->updatePluginEnabled(depId, true);
    emit pluginEnabled(depId);
  }

  // Only notify when something was actually brought online.
  if (!toEnable.isEmpty())
    emit dependenciesRequired(id, toEnable);

  m_enabledPlugins.insert(id);
  m_model->updatePluginEnabled(id, true);
  saveEnabledState();
  rebuildActiveState();
  ++m_stateRevision;
  emit stateRevisionChanged();
  emit pluginEnabled(id);
  return true;
}

bool PluginManager::disablePlugin(const QString &id) {
  if (!m_enabledPlugins.contains(id))
    return false;

  const QStringList dependents = dependentsOf(id);
  if (!dependents.isEmpty()) {
    emit dependentsWillBreak(id, dependents);
    // Also disable dependents
    for (const QString &depId : dependents) {
      m_enabledPlugins.remove(depId);
      m_model->updatePluginEnabled(depId, false);
      emit pluginDisabled(depId);
    }
  }

  m_enabledPlugins.remove(id);
  m_model->updatePluginEnabled(id, false);
  saveEnabledState();
  rebuildActiveState();
  ++m_stateRevision;
  emit stateRevisionChanged();
  emit pluginDisabled(id);
  return true;
}

bool PluginManager::isEnabled(const QString &id) const {
  return m_enabledPlugins.contains(id);
}

QVariantMap PluginManager::pluginInfo(const QString &id) const {
  if (!m_manifests.contains(id))
    return {};

  const PluginManifest &m = m_manifests.value(id);
  QVariantMap map;
  map[QStringLiteral("id")] = m.id();
  map[QStringLiteral("name")] = m.name();
  map[QStringLiteral("version")] = m.version();
  map[QStringLiteral("description")] = m.description();
  map[QStringLiteral("author")] = m.author();
  map[QStringLiteral("bundled")] = m.isBundled();
  map[QStringLiteral("category")] = m.category();
  map[QStringLiteral("hasSettings")] = m.hasSettings();
  map[QStringLiteral("enabled")] = isEnabled(id);
  map[QStringLiteral("dependencies")] = m.dependencies();
  // Theme settings UI addresses schemes by schemeKey (e.g. "tokyo-night"),
  // not by plugin id ("theme-tokyo-night"). Expose the mapping so consumers
  // do not have to guess it from the id.
  map[QStringLiteral("hasTheme")] = m.hasTheme();
  if (m.hasTheme())
    map[QStringLiteral("schemeKey")] = m.theme().schemeKey;
  return map;
}

bool PluginManager::isBundled(const QString &id) const {
  return m_manifests.contains(id) && m_manifests.value(id).isBundled();
}

bool PluginManager::canUninstall(const QString &id) const {
  return m_manifests.contains(id) && !m_manifests.value(id).isBundled();
}

bool PluginManager::uninstallPlugin(const QString &id) {
  if (!canUninstall(id))
    return false;

  const PluginManifest m = m_manifests.value(id);
  const QString dirPath = m.pluginDirectory();

  // Every directory that declared this id, including duplicate user copies
  // that lost the card-selection race. All of them must go, otherwise the
  // rescan at the end rediscovers a leftover copy and the card reappears.
  QStringList dirsToRemove = m_pluginDirs.value(id);
  if (!dirPath.isEmpty() && !dirsToRemove.contains(dirPath))
    dirsToRemove.prepend(dirPath);

  // Validate before changing any state: previously disablePlugin() ran even
  // when the plugin directory was missing and the call then failed. A partial
  // failure on an earlier attempt leaves the other directories in place, so
  // a retry can finish the job.
  bool anyExists = false;
  for (const QString &targetDir : dirsToRemove) {
    if (!targetDir.isEmpty() && QDir(targetDir).exists()) {
      anyExists = true;
      break;
    }
  }
  if (!anyExists)
    return false;

  // Snapshot the blast radius before touching files or the model: the target's
  // manifest is still in the graph at this point, and collectDependents()
  // cascades transitively.
  const QStringList dependents = dependentsOf(id);

  // Remove the directories FIRST (issue #7). If any removal fails, nothing at
  // all has changed - settings, enabled state, model and caches are intact -
  // so the operation is fully retryable and the user does not lose settings.
  for (const QString &targetDir : dirsToRemove) {
    if (targetDir.isEmpty() || !QDir(targetDir).exists())
      continue; // already removed by a previous, partially failed attempt
    if (!QDir(targetDir).removeRecursively()) {
      qWarning() << "PluginManager: failed to remove plugin directory:"
                 << targetDir;
      return false;
    }
  }

  // The plugin is now uninstalled. A settings file that cannot be removed is
  // harmless (knownPlugin() guards every read/write), but warn about it.
  const QString settingsPath = settingsFilePath(id);
  if (!settingsPath.isEmpty() && QFile::exists(settingsPath) &&
      !QFile::remove(settingsPath)) {
    qWarning() << "PluginManager: failed to remove settings file:" << settingsPath;
  }

  // Cascade-disable the target and every enabled dependent. Without this,
  // dependents stay enabled with a dependency that no longer has a manifest.
  if (!dependents.isEmpty())
    emit dependentsWillBreak(id, dependents);

  QStringList toDisable = dependents;
  toDisable.append(id);
  for (const QString &depId : toDisable) {
    if (!m_enabledPlugins.contains(depId))
      continue;
    m_enabledPlugins.remove(depId);
    m_model->updatePluginEnabled(depId, false);
    emit pluginDisabled(depId);
  }

  m_settingsCache.remove(id);
  saveEnabledState();
  rebuildActiveState();
  ++m_stateRevision;
  emit stateRevisionChanged();

  // Files are gone: rescan the plugin list to drop the card entirely.
  discoverPlugins();
  return true;
}

QStringList PluginManager::missingDependencies(const QString &id) const {
  if (!m_manifests.contains(id))
    return {};

  QStringList missing;
  QSet<QString> visited;
  collectMissingDependencies(id, visited, missing);
  return missing;
}

void PluginManager::collectMissingDependencies(const QString &id,
                                               QSet<QString> &visited,
                                               QStringList &missing) const {
  if (visited.contains(id))
    return;
  visited.insert(id);

  if (!m_manifests.contains(id)) {
    // A dependency with no manifest can never be satisfied.
    if (!missing.contains(id))
      missing.append(id);
    return;
  }

  const QStringList deps = m_manifests.value(id).dependencies();
  for (const QString &dep : deps) {
    // A dependency with no manifest is unsatisfiable even if a stale id
    // happens to remain in the enabled set.
    const bool satisfiable = m_manifests.contains(dep);
    if ((!satisfiable || !m_enabledPlugins.contains(dep)) && !missing.contains(dep))
      missing.append(dep);
    // Recurse so transitive dependencies are reported too. This also walks
    // through already-enabled dependencies to find gaps further down.
    collectMissingDependencies(dep, visited, missing);
  }
}

void PluginManager::collectDependenciesToEnable(const QString &id,
                                                QSet<QString> &visited,
                                                QStringList &toEnable) const {
  if (visited.contains(id))
    return;
  visited.insert(id);

  if (!m_manifests.contains(id))
    return; // unsatisfiable entries are reported by missingDependencies()

  // Post-order: prerequisites are appended before the plugin that needs them.
  const QStringList deps = m_manifests.value(id).dependencies();
  for (const QString &dep : deps)
    collectDependenciesToEnable(dep, visited, toEnable);

  if (!m_enabledPlugins.contains(id) && !toEnable.contains(id))
    toEnable.append(id);
}

QStringList PluginManager::dependentsOf(const QString &id) const {
  QStringList dependents;
  QSet<QString> visited;
  visited.insert(id);
  collectDependents(id, visited, dependents);
  return dependents;
}

void PluginManager::collectDependents(const QString &id, QSet<QString> &visited,
                                      QStringList &dependents) const {
  for (const QString &enabledId : m_enabledPlugins) {
    if (visited.contains(enabledId) || !m_manifests.contains(enabledId))
      continue;
    if (m_manifests.value(enabledId).dependencies().contains(id)) {
      visited.insert(enabledId);
      dependents.append(enabledId);
      // Cascading: dependents of this dependent are affected as well.
      collectDependents(enabledId, visited, dependents);
    }
  }
}

QVariant PluginManager::pluginSetting(const QString &pluginId, const QString &key) const {
  if (!knownPlugin(pluginId))
    return {};
  loadPluginSettings(pluginId);
  return m_settingsCache.value(pluginId).value(key).toVariant();
}

void PluginManager::setPluginSetting(const QString &pluginId, const QString &key, const QVariant &value) {
  if (!knownPlugin(pluginId))
    return;

  // Generic settings writes must not be able to smuggle in invalid colours.
  if (key == QStringLiteral("themeOverrides") && !isValidThemeOverrides(value)) {
    qWarning() << "PluginManager: ignoring invalid themeOverrides value for plugin"
               << pluginId;
    return;
  }

  loadPluginSettings(pluginId);
  QVariant storedValue = value;
  // isValidThemeOverrides() accepts a QString holding a JSON object, but
  // QJsonValue::fromVariant() would persist it as a JSON *string* that
  // rebuildActiveState() then ignores via toObject(). Normalise a valid JSON
  // string to a real object so this API never emits a change signal for a
  // no-op. Invalid strings were already rejected above.
  if (key == QStringLiteral("themeOverrides") &&
      value.metaType().id() == QMetaType::QString) {
    QJsonParseError err;
    const QJsonDocument doc =
        QJsonDocument::fromJson(value.toString().toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && doc.isObject())
      storedValue = QVariant::fromValue(doc.object());
  }
  QJsonObject obj = m_settingsCache.value(pluginId);
  obj.insert(key, QJsonValue::fromVariant(storedValue));
  m_settingsCache[pluginId] = obj;
  savePluginSettings(pluginId);

  // Decorations and themes both embed settings (colors, wallpaper, etc.), so
  // every setting change must refresh active state, not just theme plugins.
  rebuildActiveState();
  ++m_settingsRevision;
  emit settingsRevisionChanged();
  emit pluginSettingChanged(pluginId, key);
}

QVariantMap PluginManager::pluginSettings(const QString &pluginId) const {
  if (!knownPlugin(pluginId))
    return {};
  loadPluginSettings(pluginId);
  return m_settingsCache.value(pluginId).toVariantMap();
}

void PluginManager::resetPluginSettings(const QString &pluginId) {
  if (!knownPlugin(pluginId))
    return;

  // settingsDefaults() are manifest data and may contain corrupt colours.
  // Sanitise a local copy before caching/writing them, just like the load
  // path does, so resetting cannot inject an invalid palette.
  QJsonObject defaults = m_manifests.value(pluginId).settingsDefaults();
  sanitizeThemeOverrides(defaults);
  m_settingsCache[pluginId] = defaults;
  savePluginSettings(pluginId);
  rebuildActiveState();
  ++m_settingsRevision;
  emit settingsRevisionChanged();
  emit pluginSettingChanged(pluginId, {});
}

QVariantMap PluginManager::themeOverrides(const QString &pluginId) const {
  if (!knownPlugin(pluginId))
    return {};
  loadPluginSettings(pluginId);
  const QJsonObject obj = m_settingsCache.value(pluginId);
  return obj.value(QStringLiteral("themeOverrides")).toObject().toVariantMap();
}

void PluginManager::setThemeOverride(const QString &pluginId,
                                     const QString &mode,
                                     const QString &colorKey,
                                     const QString &value) {
  if (!knownPlugin(pluginId))
    return;

  // An empty value is the per-key "remove override" signal from the UI and
  // must stay allowed. Everything else has to be a real color.
  if (!value.isEmpty() && !QColor::fromString(value).isValid()) {
    qWarning() << "PluginManager: ignoring invalid color override" << value
               << "for plugin" << pluginId << "mode" << mode << "key" << colorKey;
    return;
  }

  loadPluginSettings(pluginId);
  QJsonObject rootObj = m_settingsCache.value(pluginId);
  QJsonObject overrides = rootObj.value(QStringLiteral("themeOverrides")).toObject();
  QJsonObject modeObj = overrides.value(mode).toObject();

  if (value.isEmpty())
    modeObj.remove(colorKey);
  else
    modeObj.insert(colorKey, value);

  overrides.insert(mode, modeObj);
  rootObj.insert(QStringLiteral("themeOverrides"), overrides);
  m_settingsCache[pluginId] = rootObj;
  savePluginSettings(pluginId);

  rebuildActiveState();
  ++m_settingsRevision;
  emit settingsRevisionChanged();
  emit pluginSettingChanged(pluginId, QStringLiteral("themeOverrides"));
}

void PluginManager::resetThemeOverrides(const QString &pluginId) {
  if (!knownPlugin(pluginId))
    return;

  loadPluginSettings(pluginId);
  QJsonObject rootObj = m_settingsCache.value(pluginId);
  rootObj.remove(QStringLiteral("themeOverrides"));
  m_settingsCache[pluginId] = rootObj;
  savePluginSettings(pluginId);

  rebuildActiveState();
  ++m_settingsRevision;
  emit settingsRevisionChanged();
  emit pluginSettingChanged(pluginId, QStringLiteral("themeOverrides"));
}

QUrl PluginManager::settingsComponent(const QString &id) const {
  if (!m_manifests.contains(id))
    return {};
  return m_manifests.value(id).settingsComponentUrl();
}

QVariantList PluginManager::decorationsForSlot(const QString &slot) const {
  return m_activeDecorations.value(slot).toList();
}

void PluginManager::rebuildActiveState() {
  // m_enabledPlugins is a QSet: iterate a sorted snapshot so active theme
  // order (and therefore Theme.qml's fallback) is stable across runs.
  QStringList enabledIds(m_enabledPlugins.begin(), m_enabledPlugins.end());
  std::sort(enabledIds.begin(), enabledIds.end());

  // 1. Rebuild themes
  QVariantList newActiveThemes;
  for (const QString &id : enabledIds) {
    if (!m_manifests.contains(id))
      continue;
    const PluginManifest &m = m_manifests.value(id);
    if (!m.hasTheme())
      continue;

    const PluginThemeDef &themeDef = m.theme();
    loadPluginSettings(id);

    // Apply any theme overrides
    const QJsonObject settingsObj = m_settingsCache.value(id);
    const QJsonObject overrides = settingsObj.value(QStringLiteral("themeOverrides")).toObject();
    const QJsonObject darkOverrides = overrides.value(QStringLiteral("dark")).toObject();
    const QJsonObject lightOverrides = overrides.value(QStringLiteral("light")).toObject();

    QJsonObject darkPalette = themeDef.darkPalette;
    for (auto it = darkOverrides.begin(); it != darkOverrides.end(); ++it)
      darkPalette.insert(it.key(), it.value());

    QJsonObject lightPalette = themeDef.lightPalette;
    for (auto it = lightOverrides.begin(); it != lightOverrides.end(); ++it)
      lightPalette.insert(it.key(), it.value());

    QVariantMap themeMap;
    themeMap[QStringLiteral("pluginId")] = m.id();
    themeMap[QStringLiteral("schemeKey")] = themeDef.schemeKey;
    themeMap[QStringLiteral("name")] = themeDef.name.toVariantMap();
    themeMap[QStringLiteral("dark")] = darkPalette.toVariantMap();
    themeMap[QStringLiteral("light")] = lightPalette.toVariantMap();
    newActiveThemes.append(themeMap);
  }

  if (m_activeThemes != newActiveThemes) {
    m_activeThemes = newActiveThemes;
    emit activeThemesChanged();
  }

  // 2. Rebuild decorations
  struct DecSortItem {
    QVariantMap map;
    int zOrder;
    QString pluginId;
  };
  QHash<QString, QVector<DecSortItem>> slotItems;

  for (const QString &id : enabledIds) {
    if (!m_manifests.contains(id))
      continue;
    const PluginManifest &m = m_manifests.value(id);
    for (const PluginDecorationDef &dec : m.decorations()) {
      QVariantMap decMap;
      decMap[QStringLiteral("pluginId")] = m.id();
      decMap[QStringLiteral("slot")] = dec.slot;
      decMap[QStringLiteral("component")] = dec.componentUrl;
      decMap[QStringLiteral("zOrder")] = dec.zOrder;

      slotItems[dec.slot].append({decMap, dec.zOrder, m.id()});
    }
  }

  QVariantMap newActiveDecorations;
  for (auto it = slotItems.begin(); it != slotItems.end(); ++it) {
    auto &list = it.value();
    std::sort(list.begin(), list.end(), [](const DecSortItem &a, const DecSortItem &b) {
      if (a.zOrder != b.zOrder)
        return a.zOrder < b.zOrder;
      // Deterministic tie-break for equal-zOrder decorations.
      return a.pluginId < b.pluginId;
    });
    QVariantList sortedList;
    for (const auto &item : list)
      sortedList.append(item.map);
    newActiveDecorations.insert(it.key(), sortedList);
  }

  if (m_activeDecorations != newActiveDecorations) {
    m_activeDecorations = newActiveDecorations;
    emit activeDecorationsChanged();
  }
}

// ---- agent plugin authoring ------------------------------------------------

bool PluginManager::createPlugin(const QJsonObject &spec) {
  m_lastPluginError.clear();
  const QString id = spec.value(QStringLiteral("id")).toString();
  if (!PluginManifest::isValidId(id)) {
    m_lastPluginError = QStringLiteral("Invalid plugin id.");
    emit pluginErrorChanged();
    return false;
  }
  const QString root = AppPaths::pluginsDirectory();
  if (root.isEmpty()) {
    m_lastPluginError = QStringLiteral("No plugins directory.");
    emit pluginErrorChanged();
    return false;
  }
  const QString dir = QDir(root).filePath(id);
  if (QFileInfo::exists(dir)) {
    m_lastPluginError = QStringLiteral("A plugin with that id already exists.");
    emit pluginErrorChanged();
    return false;
  }
  if (!QDir().mkpath(dir)) {
    m_lastPluginError = QStringLiteral("Cannot create plugin directory.");
    emit pluginErrorChanged();
    return false;
  }

  // Snapshot for rollback before writing anything.
  const QString backup = dir + QStringLiteral(".tnbak");
  if (QFileInfo::exists(backup))
    QDir(backup).removeRecursively();

  QJsonObject manifest;
  manifest.insert(QStringLiteral("id"), id);
  manifest.insert(QStringLiteral("name"),
                  spec.value(QStringLiteral("name")).toString(id));
  manifest.insert(QStringLiteral("version"),
                  spec.value(QStringLiteral("version")).toString(QStringLiteral("1.0.0")));
  manifest.insert(QStringLiteral("description"),
                  spec.value(QStringLiteral("description")).toString());
  manifest.insert(QStringLiteral("author"),
                  spec.value(QStringLiteral("author")).toString(QStringLiteral("Agent")));
  manifest.insert(QStringLiteral("category"),
                  spec.value(QStringLiteral("category")).toString(QStringLiteral("utility")));
  manifest.insert(QStringLiteral("bundled"), false);
  manifest.insert(QStringLiteral("agentCreated"), true);
  manifest.insert(QStringLiteral("dependencies"), QJsonArray());

  QJsonObject capabilities;
  const QJsonArray decorations = spec.value(QStringLiteral("decorations")).toArray();
  if (!decorations.isEmpty())
    capabilities.insert(QStringLiteral("decorations"), decorations);
  manifest.insert(QStringLiteral("capabilities"), capabilities);

  QJsonObject settings;
  const QJsonArray fields = spec.value(QStringLiteral("settingsFields")).toArray();
  if (!fields.isEmpty())
    settings.insert(QStringLiteral("fields"), fields);
  if (!settings.isEmpty())
    manifest.insert(QStringLiteral("settings"), settings);

  if (spec.contains(QStringLiteral("permissions")))
    manifest.insert(QStringLiteral("permissions"),
                    spec.value(QStringLiteral("permissions")));

  QFile file(QDir(dir).filePath(QStringLiteral("manifest.json")));
  if (!file.open(QIODevice::WriteOnly) ||
      file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) < 0) {
    m_lastPluginError = file.errorString();
    emit pluginErrorChanged();
    return false;
  }
  file.close();

  rescanPlugins();
  emit pluginNeedsApproval(id);
  return true;
}

bool PluginManager::writePluginFile(const QString &pluginId,
                                    const QString &relativePath,
                                    const QString &contents) {
  m_lastPluginError.clear();
  if (!PluginManifest::isValidId(pluginId) || !m_pluginDirs.contains(pluginId)) {
    m_lastPluginError = QStringLiteral("Unknown plugin.");
    emit pluginErrorChanged();
    return false;
  }
  // Reject traversal and absolute paths; only files inside the plugin dir.
  if (relativePath.isEmpty() || QDir::isAbsolutePath(relativePath) ||
      relativePath.contains(QStringLiteral(".."))) {
    m_lastPluginError = QStringLiteral("Invalid relative path.");
    emit pluginErrorChanged();
    return false;
  }
  // Only unapproved AI-authored plugins may be modified through the agent
  // tool. Overwriting a bundled or already-enabled plugin would let a model
  // inject arbitrary QML into a component the user has already trusted.
  const QString manifestPath = QDir(m_pluginDirs.value(pluginId).first())
                                   .filePath(QStringLiteral("manifest.json"));
  if (!isAgentCreated(manifestPath) || isPluginApproved(pluginId) ||
      isEnabled(pluginId)) {
    m_lastPluginError =
        QStringLiteral("Only unapproved agent-authored plugins can be written.");
    emit pluginErrorChanged();
    return false;
  }

  const QString dir = m_pluginDirs.value(pluginId).first();
  const QString target = QDir(dir).filePath(relativePath);
  // Reject symlink escapes as well as lexical traversal.
  const QString canonicalDir = QFileInfo(dir).canonicalFilePath();
  const QString canonicalTarget = QFileInfo(target).canonicalFilePath();
  if (canonicalDir.isEmpty() ||
      (!canonicalTarget.isEmpty() &&
       !canonicalTarget.startsWith(canonicalDir + QLatin1Char('/')))) {
    m_lastPluginError = QStringLiteral("Invalid relative path.");
    emit pluginErrorChanged();
    return false;
  }
  // Only QML/JSON/text assets are authorable — no executables and no
  // extension-less files that could be interpreted as scripts.
  const QString suffix = QFileInfo(target).suffix().toLower();
  static const QSet<QString> allowed{"qml", "json", "js", "mjs", "txt", "svg", "frag"};
  if (!allowed.contains(suffix)) {
    m_lastPluginError = QStringLiteral("File type not allowed.");
    emit pluginErrorChanged();
    return false;
  }
  QDir().mkpath(QFileInfo(target).absolutePath());
  QFile file(target);
  // Snapshot the single file before overwriting it.
  const QString backupDir = dir + QStringLiteral(".tnbak");
  const QString backupFile = QDir(backupDir).filePath(relativePath);
  if (QFileInfo::exists(target)) {
    QDir().mkpath(QFileInfo(backupFile).absolutePath());
    QFile::copy(target, backupFile);
  }
  if (!file.open(QIODevice::WriteOnly) ||
      file.write(contents.toUtf8()) < 0) {
    m_lastPluginError = file.errorString();
    emit pluginErrorChanged();
    return false;
  }
  return true;
}

bool PluginManager::rescanPlugins() {
  discoverPlugins();
  ++m_stateRevision;
  emit stateRevisionChanged();
  emit pluginsChanged();
  return true;
}

QString PluginManager::validatePlugin(const QString &pluginId) const {
  QStringList errors;
  if (!m_manifests.contains(pluginId))
    return QStringLiteral("Unknown plugin.");
  const PluginManifest &manifest = m_manifests.value(pluginId);
  if (!manifest.isValid())
    errors.append(QStringLiteral("Invalid manifest."));
  for (const PluginDecorationDef &dec : manifest.decorations()) {
    if (dec.slot.isEmpty())
      errors.append(QStringLiteral("Decoration with empty slot."));
    const QUrl url = QUrl::fromLocalFile(
        QDir(manifest.pluginDirectory()).filePath(dec.component));
    if (!QFileInfo::exists(url.toLocalFile()))
      errors.append(QStringLiteral("Missing decoration QML: ") + dec.component);
  }
  if (manifest.hasSettings()) {
    const QUrl url = manifest.settingsComponentUrl();
    if (!QFileInfo::exists(url.toLocalFile()))
      errors.append(QStringLiteral("Missing settings QML."));
  }
  return errors.isEmpty() ? QString() : errors.join(QStringLiteral("\n"));
}

bool PluginManager::approvePlugin(const QString &pluginId) {
  if (!m_manifests.contains(pluginId))
    return false;
  m_approvedPlugins.insert(pluginId);
  QJsonObject approvals;
  const QString path = approvalFilePath();
  if (!path.isEmpty() && QFileInfo::exists(path)) {
    QFile f(path);
    if (f.open(QIODevice::ReadOnly))
      approvals = QJsonDocument::fromJson(f.readAll()).object();
  }
  approvals.insert(pluginId, true);
  if (!path.isEmpty()) {
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
      f.write(QJsonDocument(approvals).toJson(QJsonDocument::Compact));
  }
  emit pluginApproved(pluginId);
  ++m_stateRevision;
  emit stateRevisionChanged();
  return true;
}

bool PluginManager::isPluginApproved(const QString &pluginId) const {
  return m_approvedPlugins.contains(pluginId) || isBundled(pluginId);
}

bool PluginManager::needsApproval(const QString &pluginId) const {
  if (!m_manifests.contains(pluginId) || isPluginApproved(pluginId))
    return false;
  const QString dir = m_pluginDirs.value(pluginId).value(0);
  if (dir.isEmpty())
    return false;
  return isAgentCreated(QDir(dir).filePath(QStringLiteral("manifest.json")));
}

QStringList PluginManager::pendingApprovalPlugins() const {
  QStringList pending;
  for (auto it = m_manifests.constBegin(); it != m_manifests.constEnd(); ++it) {
    if (needsApproval(it.key()))
      pending.append(it.key());
  }
  return pending;
}

QJsonObject PluginManager::pluginFiles(const QString &pluginId) const {
  QJsonObject files;
  if (!needsApproval(pluginId))
    return files;
  const QString dir = m_pluginDirs.value(pluginId).value(0);
  if (dir.isEmpty())
    return files;
  QDirIterator iterator(dir, QStringList{"*.qml", "*.js", "*.mjs", "*.json"},
                        QDir::Files, QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    const QString path = iterator.next();
    const QString relative = QDir(dir).relativeFilePath(path);
    if (relative == QStringLiteral("manifest.json"))
      continue;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
      continue;
    files.insert(relative,
                 QString::fromUtf8(file.read(64 * 1024)));
  }
  return files;
}

bool PluginManager::rollbackPlugin(const QString &pluginId) {
  if (!m_pluginDirs.contains(pluginId))
    return false;
  const QString dir = m_pluginDirs.value(pluginId).first();
  const QString backup = dir + QStringLiteral(".tnbak");
  if (!QFileInfo::exists(backup))
    return false;
  QDir(dir).removeRecursively();
  if (!QDir().rename(backup, dir))
    return false;
  rescanPlugins();
  return true;
}

QJsonObject PluginManager::declarativeSettings(const QString &pluginId) const {
  if (!m_manifests.contains(pluginId))
    return {};
  // Re-read the manifest to access declarative field descriptors.
  const QString dir = m_pluginDirs.value(pluginId).value(0);
  if (dir.isEmpty())
    return {};
  QFile f(QDir(dir).filePath(QStringLiteral("manifest.json")));
  if (!f.open(QIODevice::ReadOnly))
    return {};
  const QJsonObject manifest = QJsonDocument::fromJson(f.readAll()).object();
  QJsonObject result = manifest.value(QStringLiteral("settings")).toObject();
  return result;
}

QJsonObject PluginManager::applyPluginTool(const QJsonObject &call) {
  QJsonObject result;
  QJsonObject fn = call.value(QStringLiteral("function")).toObject();
  const QString name = call.value(QStringLiteral("name")).toString(
      fn.value(QStringLiteral("name")).toString());
  QJsonObject args = call.value(QStringLiteral("arguments")).toObject();
  if (args.isEmpty()) {
    const QJsonValue raw = call.value(QStringLiteral("arguments"));
    if (raw.isString()) {
      QJsonParseError error;
      const QJsonDocument doc =
          QJsonDocument::fromJson(raw.toString().toUtf8(), &error);
      if (error.error == QJsonParseError::NoError && doc.isObject())
        args = doc.object();
    }
  }

  bool ok = false;
  if (name == QLatin1String("createPlugin")) {
    ok = createPlugin(args);
  } else if (name == QLatin1String("writePluginFile")) {
    ok = writePluginFile(args.value(QStringLiteral("pluginId")).toString(),
                         args.value(QStringLiteral("path")).toString(),
                         args.value(QStringLiteral("contents")).toString());
  } else if (name == QLatin1String("approvePlugin") ||
             name == QLatin1String("enablePlugin")) {
    // Consent is a user action. It is never available as an agent tool or over
    // the control API, regardless of who is asking.
    result.insert(QStringLiteral("error"),
                  QStringLiteral("Plugin approval requires explicit user consent."));
    return result;
  } else if (name == QLatin1String("validatePlugin")) {
    const QString errors =
        validatePlugin(args.value(QStringLiteral("pluginId")).toString());
    result.insert(QStringLiteral("errors"), errors);
    ok = errors.isEmpty();
  } else if (name == QLatin1String("rescanPlugins")) {
    ok = rescanPlugins();
  } else {
    result.insert(QStringLiteral("error"), QStringLiteral("Unknown plugin tool: ") + name);
    return result;
  }

  result.insert(QStringLiteral("ok"), ok);
  if (!ok && !m_lastPluginError.isEmpty())
    result.insert(QStringLiteral("error"), m_lastPluginError);
  return result;
}

QStringList PluginManager::pluginPermissions(const QString &pluginId) const {
  const QString dir = m_pluginDirs.value(pluginId).value(0);
  if (dir.isEmpty())
    return {};
  QFile f(QDir(dir).filePath(QStringLiteral("manifest.json")));
  if (!f.open(QIODevice::ReadOnly))
    return {};
  const QJsonObject manifest = QJsonDocument::fromJson(f.readAll()).object();
  QStringList result;
  for (const QJsonValue &v : manifest.value(QStringLiteral("permissions")).toArray())
    result.append(v.toString());
  return result;
}

} // namespace Templnote
