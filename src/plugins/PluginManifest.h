#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

namespace Templnote {

struct PluginDecorationDef {
  QString slot;
  QString component; // relative file path
  QUrl componentUrl; // resolved file:// URL
  int zOrder = 0;
};

struct PluginThemeDef {
  QString schemeKey;
  QJsonObject name; // { "dark": "...", "light": "..." }
  QJsonObject darkPalette;
  QJsonObject lightPalette;
  bool isValid() const {
    return !schemeKey.isEmpty() && !darkPalette.isEmpty() && !lightPalette.isEmpty();
  }
};

class PluginManifest {
public:
  PluginManifest() = default;

  // IDs are also used as file names for per-plugin settings, so the format is
  // shared with PluginManager (which must reject unknown/invalid ids before
  // touching the settings directory).
  static bool isValidId(const QString &id);

  bool isValid() const { return m_valid; }
  const QString &id() const { return m_id; }
  const QString &name() const { return m_name; }
  const QString &version() const { return m_version; }
  const QString &description() const { return m_description; }
  const QString &author() const { return m_author; }
  bool isBundled() const { return m_bundled; }
  // Provenance is decided by the installer (PluginManager), never by the
  // self-asserted "bundled" field in a manifest JSON file.
  void setBundled(bool bundled) { m_bundled = bundled; }
  const QString &category() const { return m_category; }
  const QStringList &dependencies() const { return m_dependencies; }
  const QString &pluginDirectory() const { return m_pluginDir; }

  bool hasTheme() const { return m_hasTheme && m_theme.isValid(); }
  const PluginThemeDef &theme() const { return m_theme; }

  const QVector<PluginDecorationDef> &decorations() const { return m_decorations; }

  bool hasSettings() const { return !m_settingsComponent.isEmpty(); }
  const QString &settingsComponent() const { return m_settingsComponent; }
  QUrl settingsComponentUrl() const;
  const QJsonObject &settingsDefaults() const { return m_settingsDefaults; }

  QUrl iconUrl() const;

  static PluginManifest fromJson(const QJsonObject &json,
                                 const QString &pluginDir,
                                 bool *ok = nullptr,
                                 QString *error = nullptr);

  static PluginManifest fromFile(const QString &manifestPath,
                                 bool *ok = nullptr,
                                 QString *error = nullptr);

private:
  bool m_valid = false;
  QString m_id;
  QString m_name;
  QString m_version;
  QString m_description;
  QString m_author;
  bool m_bundled = false;
  QString m_category;
  QStringList m_dependencies;
  QString m_pluginDir;

  bool m_hasTheme = false;
  PluginThemeDef m_theme;

  QVector<PluginDecorationDef> m_decorations;

  QString m_settingsComponent;
  QJsonObject m_settingsDefaults;
  QString m_iconPath;
};

} // namespace Templnote
