#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "PluginManifest.h"
#include "PluginsModel.h"

namespace Templnote {

class PluginManager : public QObject {
  Q_OBJECT

  Q_PROPERTY(QAbstractListModel *pluginsModel READ pluginsModel CONSTANT)
  Q_PROPERTY(QVariantList activeThemes READ activeThemes NOTIFY activeThemesChanged)
  Q_PROPERTY(QVariantMap activeDecorations READ activeDecorations NOTIFY activeDecorationsChanged)
  // QML bindings that read a Q_INVOKABLE method are not reactive. These
  // revision properties give QML an explicit dependency it can read so
  // enable/disable and per-plugin setting changes refresh the UI at runtime.
  Q_PROPERTY(int stateRevision READ stateRevision NOTIFY stateRevisionChanged)
  Q_PROPERTY(int settingsRevision READ settingsRevision NOTIFY settingsRevisionChanged)

public:
  explicit PluginManager(QObject *parent = nullptr);

  QAbstractListModel *pluginsModel() const;
  QVariantList activeThemes() const;
  QVariantMap activeDecorations() const;
  int stateRevision() const;
  int settingsRevision() const;

  // Discovers plugins in ~/.config/Templnote/plugins/
  void discoverPlugins();

  // Installs or updates bundled plugins from qrc to ~/.config/Templnote/plugins/
  void installBundledPlugins();

  // Enable / disable
  Q_INVOKABLE bool enablePlugin(const QString &id);
  Q_INVOKABLE bool disablePlugin(const QString &id);
  Q_INVOKABLE bool isEnabled(const QString &id) const;

  // Plugin info
  Q_INVOKABLE QVariantMap pluginInfo(const QString &id) const;
  Q_INVOKABLE bool isBundled(const QString &id) const;
  Q_INVOKABLE bool canUninstall(const QString &id) const;
  Q_INVOKABLE bool uninstallPlugin(const QString &id);

  // Dependency resolution
  Q_INVOKABLE QStringList missingDependencies(const QString &id) const;
  Q_INVOKABLE QStringList dependentsOf(const QString &id) const;

  // Per-plugin settings
  Q_INVOKABLE QVariant pluginSetting(const QString &pluginId, const QString &key) const;
  Q_INVOKABLE void setPluginSetting(const QString &pluginId, const QString &key, const QVariant &value);
  Q_INVOKABLE QVariantMap pluginSettings(const QString &pluginId) const;
  Q_INVOKABLE void resetPluginSettings(const QString &pluginId);

  // Theme overrides
  Q_INVOKABLE QVariantMap themeOverrides(const QString &pluginId) const;
  Q_INVOKABLE void setThemeOverride(const QString &pluginId,
                                     const QString &mode,
                                     const QString &colorKey,
                                     const QString &value);
  Q_INVOKABLE void resetThemeOverrides(const QString &pluginId);

  // QML URLs
  Q_INVOKABLE QUrl settingsComponent(const QString &id) const;
  Q_INVOKABLE QVariantList decorationsForSlot(const QString &slot) const;

signals:
  void activeThemesChanged();
  void activeDecorationsChanged();
  void stateRevisionChanged();
  void settingsRevisionChanged();
  void pluginSettingChanged(const QString &pluginId, const QString &key);
  void pluginsChanged();
  void pluginEnabled(const QString &id);
  void pluginDisabled(const QString &id);
  void dependenciesRequired(const QString &id, const QStringList &deps);
  void dependentsWillBreak(const QString &id, const QStringList &dependents);

private:
  void loadPluginSettings(const QString &pluginId) const;
  void savePluginSettings(const QString &pluginId);
  void loadEnabledState();
  void saveEnabledState();
  void rebuildActiveState();

  // Recursive, cycle-safe dependency graph helpers.
  void collectMissingDependencies(const QString &id, QSet<QString> &visited,
                                  QStringList &missing) const;
  void collectDependenciesToEnable(const QString &id, QSet<QString> &visited,
                                   QStringList &toEnable) const;
  void collectDependents(const QString &id, QSet<QString> &visited,
                         QStringList &dependents) const;

  // True only for ids that came from a parsed manifest with a valid format.
  // Guards every settings-facing API so arbitrary ids cannot read or write
  // files under the plugin-settings directory.
  bool knownPlugin(const QString &id) const;

  // Bundled provenance for a discovered directory: either this run's install
  // source recorded it, or the marker file written at install time still
  // declares this exact id (needed on runs where the source is unavailable).
  bool isBundledPluginDir(const QString &dirPath, const QString &pluginId) const;

  QString settingsFilePath(const QString &pluginId) const;
  bool copyRecursively(const QString &srcFilePath, const QString &tgtFilePath);

  PluginsModel *m_model = nullptr;
  QHash<QString, PluginManifest> m_manifests;
  // Every directory that declared a given id during discovery, duplicates
  // included. Uninstall removes all of them so a leftover copy cannot bring
  // the card back on the next rescan.
  QHash<QString, QStringList> m_pluginDirs;
  QSet<QString> m_enabledPlugins;

  // Canonical paths of plugin directories that came from the bundled install
  // source this run. Used to derive "bundled" from provenance instead of
  // trusting the manifest's self-asserted "bundled" field. Runs without the
  // install source fall back to the marker file written at install time.
  QSet<QString> m_bundledPluginDirs;
  // For each bundled slot, the plugin id declared by the bundled source. A
  // directory only counts as bundled if its manifest still declares that id.
  QHash<QString, QString> m_bundledDirExpectedId;
  // Ids of bundled plugins genuinely copied into place during this run. They
  // are auto-enabled even when the one-time "initialized" flag is already set.
  QSet<QString> m_newBundledIds;

  // Cached settings per plugin: id -> QJsonObject
  mutable QHash<QString, QJsonObject> m_settingsCache;

  // Cached active structures
  QVariantList m_activeThemes;
  QVariantMap m_activeDecorations;
  int m_stateRevision = 0;
  int m_settingsRevision = 0;
};

} // namespace Templnote
