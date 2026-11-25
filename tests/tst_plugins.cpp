#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <limits>
#include <memory>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantMap>

#include "plugins/PluginManager.h"
#include "storage/AppPaths.h"
#include "plugins/PluginManifest.h"
#include "plugins/PluginsModel.h"

using namespace Templnote;

class PluginTests : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void init();
  void cleanup();
  void testManifestParsing();
  void testPluginDiscovery();
  void testEnableDisable();
  void testConfigDirIsolation();
  void testDependencies();
  void testThemeOverrides();
  void testPluginSettings();
  void testSafeComponentUrls();
  void testSettingsIdValidation();
  void testInvalidColorRejection();
  void testDependencyCycles();
  void testStateRevisionOnEnableDisable();
  void testDeterministicActiveOrder();
  void testDuplicateIdPreference();
  void testProvenanceOverridesBundledField();
  void testUninstallFailureIsRetryable();
  void testNewBundledPluginAutoEnabled();
  void testRetiredBundledPluginIsRemoved();
  void testUninstallCascadesDependents();
  void testEmptyColorRejectedInGenericPath();
  void testCorruptSettingsSanitizedOnLoad();
  void testIsValidIdRejectsTrailingNewline();
  void testUninstallRemovesDirectoryBeforeSettings();
  void testBundledProvenancePersistsWithoutSource();
  void testRecopiedBundledPluginStaysDisabled();
  void testSymlinkEscapeRejected();
  void testResetPluginSettingsSanitizesDefaults();
  void testThemeOverridesJsonStringNormalized();
  void testUninstallDuplicateRemovesAllDirectories();
  void testEnableFailureReportsMissingDependencies();
  void testBundledThemeManifestVersions();
  void testBundledPaperManifestVersion();
  void testCreatePlugin();
  void testWritePluginFileAndValidate();
  void testRollbackPlugin();
  void testDeclarativeSettingsAndPermissions();
  void testPathTraversalRejected();
  void testUnknownPermissionsAreDropped();

private:
  void writePlugin(const QString &dirPath, const QJsonObject &manifest);
  QString testConfigDir() const {
    return m_configDir ? m_configDir->path() : QString();
  }

  std::unique_ptr<QTemporaryDir> m_configDir;
  QString m_testBundledDir;
};

void PluginTests::initTestCase() {
  QCoreApplication::setApplicationName("Templnote");
  QCoreApplication::setOrganizationName("Templnote");
  m_testBundledDir = QStringLiteral(TEMPLNOTE_TEST_BUNDLED_PLUGINS_DIR);
}

// Every test gets a fresh config directory. Previously all tests shared one
// QTemporaryDir, so enabled state / the "initialized" flag / settings files
// written by one test silently changed the behaviour of later tests.
void PluginTests::init() {
  m_configDir = std::make_unique<QTemporaryDir>();
  QVERIFY2(m_configDir->isValid(), qPrintable(m_configDir->errorString()));
  qputenv("TEMPLNOTE_CONFIG_DIR", testConfigDir().toUtf8());
  qputenv("TEMPLNOTE_BUNDLED_PLUGINS_DIR", m_testBundledDir.toUtf8());
}

// Drop the environment after each test so an env-switching test can never
// leak into the next one (even if a QVERIFY returns early).
void PluginTests::cleanup() {
  qunsetenv("TEMPLNOTE_CONFIG_DIR");
  qunsetenv("TEMPLNOTE_BUNDLED_PLUGINS_DIR");
  m_configDir.reset();
}


void PluginTests::testCreatePlugin() {
  PluginManager manager;
  QJsonObject spec;
  spec.insert(QStringLiteral("id"), QStringLiteral("frosted-glass"));
  spec.insert(QStringLiteral("name"), QStringLiteral("Frosted Glass"));
  spec.insert(QStringLiteral("description"), QStringLiteral("Frosted editor background"));
  QJsonArray decorations;
  QJsonObject dec;
  dec.insert(QStringLiteral("slot"), QStringLiteral("paper-background"));
  dec.insert(QStringLiteral("component"), QStringLiteral("Frosted.qml"));
  dec.insert(QStringLiteral("zOrder"), 1);
  decorations.append(dec);
  spec.insert(QStringLiteral("decorations"), decorations);

  QVERIFY(manager.createPlugin(spec));
  QVERIFY(!manager.validatePlugin(QStringLiteral("frosted-glass")).isEmpty());
  QVERIFY(manager.writePluginFile(
      QStringLiteral("frosted-glass"), QStringLiteral("Frosted.qml"),
      QStringLiteral("import QtQuick\nItem { anchors.fill: parent }")));
  QVERIFY(manager.validatePlugin(QStringLiteral("frosted-glass")).isEmpty());
  QVERIFY(manager.rescanPlugins());
  QVERIFY(!manager.pluginInfo(QStringLiteral("frosted-glass")).isEmpty());
}

void PluginTests::testWritePluginFileAndValidate() {
  PluginManager manager;
  QJsonObject spec;
  spec.insert(QStringLiteral("id"), QStringLiteral("daily-verse"));
  spec.insert(QStringLiteral("name"), QStringLiteral("Daily Verse"));
  QVERIFY(manager.createPlugin(spec));

  QVERIFY(manager.writePluginFile(
      QStringLiteral("daily-verse"), QStringLiteral("Verse.qml"),
      QStringLiteral("import QtQuick\nText { text: \"Verse\" }")));
  const QString path = QDir(AppPaths::pluginsDirectory())
                           .filePath(QStringLiteral("daily-verse/Verse.qml"));
  QVERIFY(QFileInfo::exists(path));
  QCOMPARE(manager.lastPluginError(), QString());
}

void PluginTests::testRollbackPlugin() {
  PluginManager manager;
  QJsonObject spec;
  spec.insert(QStringLiteral("id"), QStringLiteral("rollback-test"));
  spec.insert(QStringLiteral("name"), QStringLiteral("Rollback"));
  QVERIFY(manager.createPlugin(spec));
  QVERIFY(manager.writePluginFile(
      QStringLiteral("rollback-test"), QStringLiteral("Widget.qml"),
      QStringLiteral("import QtQuick\nItem {}")));
  const QString path = QDir(AppPaths::pluginsDirectory())
                           .filePath(QStringLiteral("rollback-test/Widget.qml"));
  QVERIFY(QFileInfo::exists(path));

  QVERIFY(manager.writePluginFile(
      QStringLiteral("rollback-test"), QStringLiteral("Widget.qml"),
      QStringLiteral("broken qml")));
  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  QCOMPARE(QString::fromUtf8(f.readAll()), QStringLiteral("broken qml"));

  QVERIFY(manager.rollbackPlugin(QStringLiteral("rollback-test")));
  QFile f2(path);
  QVERIFY(f2.open(QIODevice::ReadOnly));
  QCOMPARE(QString::fromUtf8(f2.readAll()),
           QStringLiteral("import QtQuick\nItem {}"));
}

void PluginTests::testDeclarativeSettingsAndPermissions() {
  PluginManager manager;
  QJsonObject spec;
  spec.insert(QStringLiteral("id"), QStringLiteral("clock-widget"));
  spec.insert(QStringLiteral("name"), QStringLiteral("Clock"));

  QJsonArray fields;
  QJsonObject field;
  field.insert(QStringLiteral("key"), QStringLiteral("showSeconds"));
  field.insert(QStringLiteral("type"), QStringLiteral("boolean"));
  field.insert(QStringLiteral("label"), QStringLiteral("Show seconds"));
  field.insert(QStringLiteral("default"), true);
  fields.append(field);
  spec.insert(QStringLiteral("settingsFields"), fields);
  spec.insert(QStringLiteral("permissions"),
              QJsonArray{QStringLiteral("network")});

  QVERIFY(manager.createPlugin(spec));
  const QJsonObject settings = manager.declarativeSettings(QStringLiteral("clock-widget"));
  QVERIFY(settings.contains(QStringLiteral("fields")));
  QCOMPARE(settings.value(QStringLiteral("fields")).toArray().size(), 1);
  QCOMPARE(manager.pluginPermissions(QStringLiteral("clock-widget")),
           QStringList{QStringLiteral("network")});
}

void PluginTests::testUnknownPermissionsAreDropped() {
  PluginManager manager;
  QJsonObject spec;
  spec.insert(QStringLiteral("id"), QStringLiteral("perm-test"));
  spec.insert(QStringLiteral("name"), QStringLiteral("Perms"));
  spec.insert(QStringLiteral("permissions"),
              QJsonArray{QStringLiteral("network"), QStringLiteral("camera"),
                         QStringLiteral("filesystem")});
  QVERIFY(manager.createPlugin(spec));
  QCOMPARE(manager.pluginPermissions(QStringLiteral("perm-test")),
           QStringList{QStringLiteral("network")});

  QJsonObject raw;
  raw.insert(QStringLiteral("id"), QStringLiteral("raw-perms"));
  raw.insert(QStringLiteral("permissions"),
             QJsonArray{QStringLiteral("clipboard"), QStringLiteral("network")});
  bool ok = false;
  const PluginManifest manifest =
      PluginManifest::fromJson(raw, QDir::currentPath(), &ok);
  QVERIFY(ok);
  QCOMPARE(manifest.permissions(), QStringList{QStringLiteral("network")});
}

void PluginTests::testPathTraversalRejected() {
  PluginManager manager;
  QJsonObject spec;
  spec.insert(QStringLiteral("id"), QStringLiteral("safe-plugin"));
  spec.insert(QStringLiteral("name"), QStringLiteral("Safe"));
  QVERIFY(manager.createPlugin(spec));
  QVERIFY(!manager.writePluginFile(
      QStringLiteral("safe-plugin"), QStringLiteral("../escape.qml"),
      QStringLiteral("bad")));
  QVERIFY(!manager.writePluginFile(
      QStringLiteral("safe-plugin"), QStringLiteral("evil.exe"),
      QStringLiteral("bad")));
  QVERIFY(!manager.lastPluginError().isEmpty());
}

void PluginTests::writePlugin(const QString &dirPath,
                              const QJsonObject &manifest) {
  QVERIFY(QDir().mkpath(dirPath));
  QFile file(QDir(dirPath).filePath(QStringLiteral("manifest.json")));
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(QJsonDocument(manifest).toJson());
  file.close();
}

void PluginTests::testManifestParsing() {
  const QJsonObject json = {
    {"id", "my-test-plugin"},
    {"name", "My Test Plugin"},
    {"version", "1.2.3"},
    {"description", "Test description"},
    {"author", "Test Author"},
    {"bundled", true},
    {"category", "decoration"},
    {"dependencies", QJsonArray{"dep1", "dep2"}},
    {"capabilities", QJsonObject{
      {"decorations", QJsonArray{
        QJsonObject{
          {"slot", "paper-background"},
          {"component", "TestDecoration.qml"},
          {"zOrder", 10}
        }
      }}
    }},
    {"settings", QJsonObject{
      {"component", "TestSettings.qml"},
      {"defaults", QJsonObject{{"foo", "bar"}}}
    }}
  };

  bool ok = false;
  QString error;
  const PluginManifest m = PluginManifest::fromJson(json, "/tmp", &ok, &error);
  QVERIFY2(ok, qPrintable(error));
  QCOMPARE(m.id(), QString("my-test-plugin"));
  QCOMPARE(m.name(), QString("My Test Plugin"));
  QCOMPARE(m.version(), QString("1.2.3"));
  QCOMPARE(m.author(), QString("Test Author"));
  QCOMPARE(m.isBundled(), true);
  QCOMPARE(m.category(), QString("decoration"));
  QCOMPARE(m.dependencies().size(), 2);
  QCOMPARE(m.decorations().size(), 1);
  QCOMPARE(m.decorations().at(0).slot, QString("paper-background"));
  QCOMPARE(m.decorations().at(0).zOrder, 10);
  QCOMPARE(m.hasSettings(), true);
  QCOMPARE(m.settingsComponent(), QString("TestSettings.qml"));
}

void PluginTests::testPluginDiscovery() {
  PluginManager pm;
  QVERIFY(pm.pluginsModel()->rowCount() >= 6);

  // Check known bundled plugins
  QVERIFY(pm.isBundled("theme-tokyo-night"));
  QVERIFY(pm.isBundled("theme-gruvbox"));
  QVERIFY(pm.isBundled("theme-solarized-osaka"));
  QVERIFY(pm.isBundled("theme-github"));
  QVERIFY(pm.isBundled("paper-background"));

  // Check themes are active
  const QVariantList themes = pm.activeThemes();
  QVERIFY(themes.size() >= 4);

  // Check decorations for paper-background
  const QVariantList paperDecs = pm.decorationsForSlot("paper-background");
  QVERIFY(paperDecs.size() >= 1);
}

void PluginTests::testEnableDisable() {
  PluginManager pm;
  QVERIFY(pm.isEnabled("theme-github"));

  QSignalSpy disabledSpy(&pm, &PluginManager::pluginDisabled);
  QSignalSpy themesChangedSpy(&pm, &PluginManager::activeThemesChanged);

  pm.disablePlugin("theme-github");
  QCOMPARE(pm.isEnabled("theme-github"), false);
  QCOMPARE(disabledSpy.count(), 1);
  QCOMPARE(themesChangedSpy.count(), 1);

  // Check that theme-github is no longer in activeThemes
  bool found = false;
  for (const auto &t : pm.activeThemes()) {
    if (t.toMap().value("pluginId").toString() == "theme-github") {
      found = true;
      break;
    }
  }
  QVERIFY(!found);

  // Re-enable
  pm.enablePlugin("theme-github");
  QCOMPARE(pm.isEnabled("theme-github"), true);
}

void PluginTests::testConfigDirIsolation() {
  PluginManager pm;

  QSignalSpy revisionSpy(&pm, &PluginManager::settingsRevisionChanged);
  QSignalSpy settingSpy(&pm, &PluginManager::pluginSettingChanged);
  pm.setPluginSetting("paper-background", "testKey", QStringLiteral("value"));
  QCOMPARE(revisionSpy.count(), 1);
  QCOMPARE(settingSpy.count(), 1);
  QCOMPARE(pm.pluginSetting("paper-background", "testKey").toString(),
           QStringLiteral("value"));

  // Enabled state must be stored under TEMPLNOTE_CONFIG_DIR, not the OS
  // default location that ignores the environment override.
  const QString configDir = QString::fromUtf8(qgetenv("TEMPLNOTE_CONFIG_DIR"));
  QVERIFY(!configDir.isEmpty());
  QVERIFY(QFile::exists(QDir(configDir).filePath(QStringLiteral("Templnote.conf"))));
}

void PluginTests::testDependencies() {
  // The paper plugins used to provide a dependency chain; they are now a
  // single plugin, so build a synthetic chain to exercise the same logic.
  writePlugin(QDir(testConfigDir()).filePath(QStringLiteral("plugins/dep-base")),
              {{"id", "dep-base"},
               {"name", "Dep Base"},
               {"version", "1.0.0"},
               {"bundled", false}});
  writePlugin(QDir(testConfigDir()).filePath(QStringLiteral("plugins/dep-child")),
              {{"id", "dep-child"},
               {"name", "Dep Child"},
               {"version", "1.0.0"},
               {"bundled", false},
               {"dependencies", QJsonArray{"dep-base"}}});

  PluginManager pm;
  QVERIFY(pm.enablePlugin("dep-base"));
  QVERIFY(pm.enablePlugin("dep-child"));
  const QStringList dependents = pm.dependentsOf("dep-base");
  QVERIFY(dependents.contains("dep-child"));

  // Disabling the base cascades to its dependents.
  QSignalSpy breakSpy(&pm, &PluginManager::dependentsWillBreak);
  QVERIFY(pm.disablePlugin("dep-base"));
  QVERIFY(breakSpy.count() >= 1);
  QCOMPARE(pm.isEnabled("dep-child"), false);

  // Re-enabling the child auto-enables its prerequisite.
  QVERIFY(pm.enablePlugin("dep-child"));
  QCOMPARE(pm.isEnabled("dep-base"), true);
  QCOMPARE(pm.isEnabled("dep-child"), true);

  // An unsatisfiable dependency is refused without pretending dependencies
  // were auto-enabled (issue #4).
  writePlugin(QDir(testConfigDir()).filePath(QStringLiteral("plugins/broken-dep")),
              {{"id", "broken-dep"},
               {"name", "Broken Dep"},
               {"version", "1.0.0"},
               {"bundled", false},
               {"dependencies", QJsonArray{"no-such-plugin"}}});
  PluginManager pm2;
  QSignalSpy requiredSpy(&pm2, &PluginManager::dependenciesRequired);
  QVERIFY(!pm2.enablePlugin(QStringLiteral("broken-dep")));
  QCOMPARE(requiredSpy.count(), 0);
}

void PluginTests::testThemeOverrides() {
  PluginManager pm;
  pm.enablePlugin("theme-tokyo-night");

  // Set an accent override
  pm.setThemeOverride("theme-tokyo-night", "dark", "accent", "#ff007f");

  // Find in activeThemes
  bool verified = false;
  for (const auto &t : pm.activeThemes()) {
    const QVariantMap map = t.toMap();
    if (map.value("pluginId").toString() == "theme-tokyo-night") {
      const QVariantMap dark = map.value("dark").toMap();
      QCOMPARE(dark.value("accent").toString(), QString("#ff007f"));
      verified = true;
      break;
    }
  }
  QVERIFY(verified);

  // Reset override
  pm.resetThemeOverrides("theme-tokyo-night");
  for (const auto &t : pm.activeThemes()) {
    const QVariantMap map = t.toMap();
    if (map.value("pluginId").toString() == "theme-tokyo-night") {
      const QVariantMap dark = map.value("dark").toMap();
      QCOMPARE(dark.value("accent").toString(), QString("#7aa2f7"));
      break;
    }
  }
}

void PluginTests::testPluginSettings() {
  PluginManager pm;
  pm.setPluginSetting("paper-background", "paperGrain", 0.065);
  QCOMPARE(pm.pluginSetting("paper-background", "paperGrain").toDouble(), 0.065);

  pm.resetPluginSettings("paper-background");
  QCOMPARE(pm.pluginSetting("paper-background", "paperGrain").toDouble(), 0.035);
}

void PluginTests::testSafeComponentUrls() {
  const QString dir = QStringLiteral("/tmp/my-plugin");

  auto decorationUrl = [&](const QString &component) -> QUrl {
    const QJsonObject json = {
      {"id", "url-test"},
      {"capabilities", QJsonObject{
        {"decorations", QJsonArray{
          QJsonObject{{"slot", "paper-background"}, {"component", component}}
        }}
      }}
    };
    bool ok = false;
    const PluginManifest manifest = PluginManifest::fromJson(json, dir, &ok);
    if (!ok || manifest.decorations().isEmpty())
      return {};
    return manifest.decorations().first().componentUrl;
  };

  const QUrl relative = decorationUrl(QStringLiteral("Test.qml"));
  QVERIFY(relative.isLocalFile());
  QCOMPARE(relative.toLocalFile(), QStringLiteral("/tmp/my-plugin/Test.qml"));

  // qrc: and :/ resources are allowed and must not be resolved inside the
  // plugin directory.
  const QUrl qrc = decorationUrl(QStringLiteral("qrc:/qt/qml/Templnote/foo.qml"));
  QCOMPARE(qrc.scheme(), QStringLiteral("qrc"));
  QCOMPARE(qrc.toString(), QStringLiteral("qrc:/qt/qml/Templnote/foo.qml"));

  const QUrl colon = decorationUrl(QStringLiteral(":/assets/icon.svg"));
  QCOMPARE(colon.toString(), QStringLiteral("qrc:/assets/icon.svg"));

  const QUrl triple = decorationUrl(QStringLiteral("qrc:///assets/icon.svg"));
  QCOMPARE(triple.toString(), QStringLiteral("qrc:/assets/icon.svg"));

  // Traversal and absolute paths outside the plugin directory are rejected.
  QVERIFY(decorationUrl(QStringLiteral("../../evil.qml")).isEmpty());
  QVERIFY(decorationUrl(QStringLiteral("sub/../../../evil.qml")).isEmpty());
  QVERIFY(decorationUrl(QStringLiteral("/etc/passwd")).isEmpty());

  // An absolute path that stays inside the plugin directory is fine.
  const QUrl inside = decorationUrl(QStringLiteral("/tmp/my-plugin/Sub/Inside.qml"));
  QVERIFY(inside.isLocalFile());
  QCOMPARE(inside.toLocalFile(),
           QStringLiteral("/tmp/my-plugin/Sub/Inside.qml"));

  // The settings component goes through the same resolver.
  const QJsonObject settingsJson = {
    {"id", "url-test"},
    {"settings", QJsonObject{{"component", QStringLiteral("qrc:/qt/qml/Templnote/Settings.qml")}}}
  };
  bool ok = false;
  const PluginManifest settingsManifest =
      PluginManifest::fromJson(settingsJson, dir, &ok);
  QVERIFY(ok);
  QCOMPARE(settingsManifest.settingsComponentUrl().toString(),
           QStringLiteral("qrc:/qt/qml/Templnote/Settings.qml"));
}

void PluginTests::testSettingsIdValidation() {
  PluginManager pm;
  QSignalSpy revisionSpy(&pm, &PluginManager::settingsRevisionChanged);
  QSignalSpy settingSpy(&pm, &PluginManager::pluginSettingChanged);

  QCOMPARE(pm.pluginSetting(QStringLiteral("../escape"), QStringLiteral("k")),
           QVariant());
  QCOMPARE(pm.pluginSetting(QStringLiteral("bad/id"), QStringLiteral("k")),
           QVariant());
  QCOMPARE(pm.pluginSetting(QStringLiteral("not-installed"), QStringLiteral("k")),
           QVariant());
  QVERIFY(pm.pluginSettings(QStringLiteral("../escape")).isEmpty());
  QVERIFY(pm.pluginSettings(QStringLiteral("not-installed")).isEmpty());
  QVERIFY(pm.themeOverrides(QStringLiteral("not-installed")).isEmpty());

  pm.setPluginSetting(QStringLiteral("../escape"), QStringLiteral("k"),
                      QStringLiteral("v"));
  pm.setPluginSetting(QStringLiteral("not-installed"), QStringLiteral("k"),
                      QStringLiteral("v"));
  pm.resetPluginSettings(QStringLiteral("not-installed"));
  pm.setThemeOverride(QStringLiteral("not-installed"), QStringLiteral("dark"),
                      QStringLiteral("accent"), QStringLiteral("#ff0000"));
  pm.resetThemeOverrides(QStringLiteral("not-installed"));

  QCOMPARE(revisionSpy.count(), 0);
  QCOMPARE(settingSpy.count(), 0);

  const QString orphan = QDir(testConfigDir())
                             .filePath(QStringLiteral("plugin-settings/not-installed.json"));
  QVERIFY(!QFile::exists(orphan));
}

void PluginTests::testInvalidColorRejection() {
  PluginManager pm;
  pm.enablePlugin(QStringLiteral("theme-tokyo-night"));
  QSignalSpy revisionSpy(&pm, &PluginManager::settingsRevisionChanged);

  pm.setThemeOverride(QStringLiteral("theme-tokyo-night"), QStringLiteral("dark"),
                      QStringLiteral("accent"), QStringLiteral("not-a-color"));
  QCOMPARE(revisionSpy.count(), 0);

  // setPluginSetting() must not bypass the QColor validation.
  QVariantMap badMode;
  badMode.insert(QStringLiteral("accent"), QStringLiteral("also-not-a-color"));
  QVariantMap badOverrides;
  badOverrides.insert(QStringLiteral("dark"), badMode);
  pm.setPluginSetting(QStringLiteral("theme-tokyo-night"),
                      QStringLiteral("themeOverrides"), badOverrides);
  QCOMPARE(revisionSpy.count(), 0);

  // A valid override still works and reaches the active theme.
  pm.setThemeOverride(QStringLiteral("theme-tokyo-night"), QStringLiteral("dark"),
                      QStringLiteral("accent"), QStringLiteral("#ff007f"));
  QCOMPARE(revisionSpy.count(), 1);
  bool verified = false;
  for (const auto &t : pm.activeThemes()) {
    const QVariantMap map = t.toMap();
    if (map.value(QStringLiteral("pluginId")).toString() ==
        QStringLiteral("theme-tokyo-night")) {
      QCOMPARE(map.value(QStringLiteral("dark")).toMap()
                   .value(QStringLiteral("accent")).toString(),
               QStringLiteral("#ff007f"));
      verified = true;
      break;
    }
  }
  QVERIFY(verified);

  // A valid generic themeOverrides write is accepted too.
  QVariantMap goodMode;
  goodMode.insert(QStringLiteral("accent"), QStringLiteral("#00ff00"));
  QVariantMap goodOverrides;
  goodOverrides.insert(QStringLiteral("dark"), goodMode);
  pm.setPluginSetting(QStringLiteral("theme-tokyo-night"),
                      QStringLiteral("themeOverrides"), goodOverrides);
  QCOMPARE(revisionSpy.count(), 2);

  pm.resetThemeOverrides(QStringLiteral("theme-tokyo-night"));
}

void PluginTests::testDependencyCycles() {
  const QString dirA = QDir(testConfigDir()).filePath(QStringLiteral("plugins/cycle-a"));
  const QString dirB = QDir(testConfigDir()).filePath(QStringLiteral("plugins/cycle-b"));
  writePlugin(dirA, {{"id", "cycle-a"},
                     {"name", "Cycle A"},
                     {"version", "1.0.0"},
                     {"bundled", false},
                     {"dependencies", QJsonArray{"cycle-b"}}});
  writePlugin(dirB, {{"id", "cycle-b"},
                     {"name", "Cycle B"},
                     {"version", "1.0.0"},
                     {"bundled", false},
                     {"dependencies", QJsonArray{"cycle-a"}}});

  PluginManager pm;
  QVERIFY(!pm.isBundled(QStringLiteral("cycle-a")));
  QVERIFY(pm.canUninstall(QStringLiteral("cycle-a")));

  // Enabling must terminate and bring both members of the cycle online.
  QVERIFY(pm.enablePlugin(QStringLiteral("cycle-a")));
  QVERIFY(pm.isEnabled(QStringLiteral("cycle-a")));
  QVERIFY(pm.isEnabled(QStringLiteral("cycle-b")));
  QVERIFY(pm.missingDependencies(QStringLiteral("cycle-a")).isEmpty());
  QVERIFY(pm.dependentsOf(QStringLiteral("cycle-a")).contains(QStringLiteral("cycle-b")));
  QVERIFY(pm.dependentsOf(QStringLiteral("cycle-b")).contains(QStringLiteral("cycle-a")));

  // Disabling must terminate and cascade to the other member.
  QVERIFY(pm.disablePlugin(QStringLiteral("cycle-a")));
  QVERIFY(!pm.isEnabled(QStringLiteral("cycle-a")));
  QVERIFY(!pm.isEnabled(QStringLiteral("cycle-b")));

  pm.uninstallPlugin(QStringLiteral("cycle-a"));
  pm.uninstallPlugin(QStringLiteral("cycle-b"));
}

void PluginTests::testStateRevisionOnEnableDisable() {
  PluginManager pm;
  QSignalSpy spy(&pm, &PluginManager::stateRevisionChanged);

  QVERIFY(pm.disablePlugin(QStringLiteral("paper-background")));
  QCOMPARE(spy.count(), 1);
  QVERIFY(pm.enablePlugin(QStringLiteral("paper-background")));
  QCOMPARE(spy.count(), 2);

  QVERIFY(pm.disablePlugin(QStringLiteral("paper-background")));
  QCOMPARE(spy.count(), 3);
  QVERIFY(pm.enablePlugin(QStringLiteral("paper-background")));
  QCOMPARE(spy.count(), 4);
}

void PluginTests::testDeterministicActiveOrder() {
  PluginManager pm;

  QStringList themeIds;
  for (const auto &t : pm.activeThemes())
    themeIds.append(t.toMap().value(QStringLiteral("pluginId")).toString());
  QStringList sorted = themeIds;
  sorted.sort();
  QCOMPARE(themeIds, sorted);

  // Decorations are ordered by zOrder, then by plugin id for ties.
  const QVariantList decorations = pm.decorationsForSlot(QStringLiteral("paper-background"));
  int previousZ = std::numeric_limits<int>::min();
  QString previousId;
  for (const auto &d : decorations) {
    const QVariantMap map = d.toMap();
    const int z = map.value(QStringLiteral("zOrder")).toInt();
    const QString id = map.value(QStringLiteral("pluginId")).toString();
    QVERIFY(z > previousZ || (z == previousZ && id >= previousId));
    previousZ = z;
    previousId = id;
  }
}

void PluginTests::testDuplicateIdPreference() {
  const QString fake = QDir(testConfigDir()).filePath(QStringLiteral("plugins/fake-github"));
  // A user plugin claims an existing id and even self-asserts "bundled".
  writePlugin(fake, {{"id", "theme-github"},
                     {"name", "Fake GitHub"},
                     {"version", "9.0.0"},
                     {"bundled", true}});

  PluginManager pm;
  QVERIFY(pm.isBundled(QStringLiteral("theme-github")));
  const QVariantMap info = pm.pluginInfo(QStringLiteral("theme-github"));
  QCOMPARE(info.value(QStringLiteral("name")).toString(), QStringLiteral("GitHub"));
  QVERIFY(!pm.canUninstall(QStringLiteral("theme-github")));

  int cards = 0;
  for (int row = 0; row < pm.pluginsModel()->rowCount(); ++row) {
    const QVariant id = pm.pluginsModel()->data(
        pm.pluginsModel()->index(row), PluginsModel::IdRole);
    if (id.toString() == QStringLiteral("theme-github"))
      ++cards;
  }
  QCOMPARE(cards, 1);

  QDir(fake).removeRecursively();
}

void PluginTests::testProvenanceOverridesBundledField() {
  const QString dir = QDir(testConfigDir()).filePath(QStringLiteral("plugins/spoofed-bundled"));
  writePlugin(dir, {{"id", "spoofed-bundled"},
                    {"name", "Spoofed Bundled"},
                    {"version", "1.0.0"},
                    {"bundled", true}});

  PluginManager pm;
  QVERIFY(!pm.isBundled(QStringLiteral("spoofed-bundled")));
  QVERIFY(pm.canUninstall(QStringLiteral("spoofed-bundled")));
  QVERIFY(!pm.isEnabled(QStringLiteral("spoofed-bundled")));

  QDir(dir).removeRecursively();
}

void PluginTests::testUninstallFailureIsRetryable() {
  const QString dir = QDir(testConfigDir()).filePath(QStringLiteral("plugins/uninstall-test"));
  writePlugin(dir, {{"id", "uninstall-test"},
                    {"name", "Uninstall Test"},
                    {"version", "1.0.0"},
                    {"bundled", false}});

  PluginManager pm;
  QVERIFY(pm.enablePlugin(QStringLiteral("uninstall-test")));
  pm.setPluginSetting(QStringLiteral("uninstall-test"), QStringLiteral("keptKey"),
                      QStringLiteral("keptValue"));
  const QString settingsFile =
      QDir(testConfigDir()).filePath(QStringLiteral("plugin-settings/uninstall-test.json"));
  QVERIFY(QFile::exists(settingsFile));

  // Make the plugin directory unwritable so QDir::removeRecursively() cannot
  // delete its manifest. If the platform ignores this (e.g. running as root),
  // skip instead of reporting a false failure.
  QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
  const bool removalFailed = !pm.uninstallPlugin(QStringLiteral("uninstall-test"));
  if (!removalFailed) {
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                   QFileDevice::ExeOwner);
    QSKIP("Platform allowed removal of a read-only plugin directory; cannot "
          "simulate a directory-removal failure.");
  }

  // With the directory still present the whole operation must be rolled back:
  // enabled state, model and - most importantly - settings survive (issue #7).
  QVERIFY(QDir(dir).exists());
  QVERIFY(QFile::exists(settingsFile));
  QVERIFY(pm.isEnabled(QStringLiteral("uninstall-test")));
  QVERIFY(pm.pluginInfo(QStringLiteral("uninstall-test")).contains(QStringLiteral("id")));
  QCOMPARE(pm.pluginSetting(QStringLiteral("uninstall-test"), QStringLiteral("keptKey")).toString(),
           QStringLiteral("keptValue"));

  // Retry after restoring write access.
  QVERIFY(QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                         QFileDevice::ExeOwner));
  QVERIFY(pm.uninstallPlugin(QStringLiteral("uninstall-test")));
  QVERIFY(!QDir(dir).exists());
  QVERIFY(!QFile::exists(settingsFile));
}

void PluginTests::testNewBundledPluginAutoEnabled() {
  QTemporaryDir source;
  QVERIFY(source.isValid());
  qputenv("TEMPLNOTE_BUNDLED_PLUGINS_DIR", source.path().toUtf8());

  const QString dirOne = QDir(source.path()).filePath(QStringLiteral("plugin-one"));
  writePlugin(dirOne, {{"id", "plugin-one"},
                       {"name", "Plugin One"},
                       {"version", "1.0.0"},
                       {"bundled", true}});

  {
    PluginManager first;
    QVERIFY(first.isBundled(QStringLiteral("plugin-one")));
    QVERIFY(first.isEnabled(QStringLiteral("plugin-one")));
    // The user explicitly disables this bundled plugin.
    QVERIFY(first.disablePlugin(QStringLiteral("plugin-one")));
  }

  const QString dirTwo = QDir(source.path()).filePath(QStringLiteral("plugin-two"));
  writePlugin(dirTwo, {{"id", "plugin-two"},
                       {"name", "Plugin Two"},
                       {"version", "1.0.0"},
                       {"bundled", true}});

  {
    PluginManager second;
    QVERIFY(second.isBundled(QStringLiteral("plugin-two")));
    // Genuinely new bundled plugin: enabled even though "initialized" is set.
    QVERIFY(second.isEnabled(QStringLiteral("plugin-two")));
    // Explicit user choice is preserved.
    QVERIFY(!second.isEnabled(QStringLiteral("plugin-one")));
  }
}

void PluginTests::testRetiredBundledPluginIsRemoved() {
  // Simulate a user config that still has an installed copy of a bundled
  // plugin which is no longer shipped. It must not appear in the plugin list.
  const QString dir =
      QDir(testConfigDir()).filePath(QStringLiteral("plugins/retired-panel"));
  QVERIFY(QDir().mkpath(dir));
  QJsonObject manifest;
  manifest.insert(QStringLiteral("id"), QStringLiteral("retired-panel"));
  manifest.insert(QStringLiteral("name"), QStringLiteral("Retired Panel"));
  manifest.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
  manifest.insert(QStringLiteral("bundled"), true);
  QFile manifestFile(QDir(dir).filePath(QStringLiteral("manifest.json")));
  QVERIFY(manifestFile.open(QIODevice::WriteOnly));
  manifestFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
  manifestFile.close();
  QFile marker(QDir(dir).filePath(QStringLiteral(".templnote-bundled")));
  QVERIFY(marker.open(QIODevice::WriteOnly));
  marker.write("retired-panel\n");
  marker.close();

  PluginManager manager;
  QVERIFY(!manager.pluginInfo(QStringLiteral("retired-panel"))
               .contains(QStringLiteral("id")));
  QVERIFY(!QFile::exists(QDir(dir).filePath(QStringLiteral("manifest.json"))));
}

void PluginTests::testUninstallCascadesDependents() {
  const QString base = QDir(testConfigDir()).filePath(QStringLiteral("plugins/break-base"));
  const QString child = QDir(testConfigDir()).filePath(QStringLiteral("plugins/break-child"));
  const QString grand = QDir(testConfigDir()).filePath(QStringLiteral("plugins/break-grandchild"));
  writePlugin(base, {{"id", "break-base"},
                     {"name", "Break Base"},
                     {"version", "1.0.0"},
                     {"bundled", false}});
  writePlugin(child, {{"id", "break-child"},
                      {"name", "Break Child"},
                      {"version", "1.0.0"},
                      {"bundled", false},
                      {"dependencies", QJsonArray{"break-base"}}});
  writePlugin(grand, {{"id", "break-grandchild"},
                      {"name", "Break Grandchild"},
                      {"version", "1.0.0"},
                      {"bundled", false},
                      {"dependencies", QJsonArray{"break-child"}}});

  PluginManager pm;
  QVERIFY(pm.enablePlugin(QStringLiteral("break-grandchild")));
  QVERIFY(pm.isEnabled(QStringLiteral("break-child")));
  QVERIFY(pm.isEnabled(QStringLiteral("break-base")));

  QSignalSpy breakSpy(&pm, &PluginManager::dependentsWillBreak);
  QVERIFY(pm.uninstallPlugin(QStringLiteral("break-base")));
  QCOMPARE(breakSpy.count(), 1);
  const QVariantList args = breakSpy.takeFirst();
  QCOMPARE(args.at(0).toString(), QStringLiteral("break-base"));
  const QStringList reported = args.at(1).toStringList();
  QVERIFY(reported.contains(QStringLiteral("break-child")));
  QVERIFY(reported.contains(QStringLiteral("break-grandchild")));
  QVERIFY(!pm.isEnabled(QStringLiteral("break-child")));
  QVERIFY(!pm.isEnabled(QStringLiteral("break-grandchild")));
  QVERIFY(!pm.pluginInfo(QStringLiteral("break-base")).contains(QStringLiteral("id")));
}

void PluginTests::testEmptyColorRejectedInGenericPath() {
  PluginManager pm;
  QSignalSpy revisionSpy(&pm, &PluginManager::settingsRevisionChanged);

  // An empty string is a removal signal ONLY inside setThemeOverride(); the
  // generic setPluginSetting() path must not write a blank colour (issue #3).
  QVariantMap emptyMode;
  emptyMode.insert(QStringLiteral("accent"), QString());
  QVariantMap emptyOverrides;
  emptyOverrides.insert(QStringLiteral("dark"), emptyMode);
  pm.setPluginSetting(QStringLiteral("theme-tokyo-night"),
                      QStringLiteral("themeOverrides"), emptyOverrides);
  QCOMPARE(revisionSpy.count(), 0);
  QVERIFY(pm.themeOverrides(QStringLiteral("theme-tokyo-night")).isEmpty());

  // The dedicated removal API still accepts it.
  pm.setThemeOverride(QStringLiteral("theme-tokyo-night"), QStringLiteral("dark"),
                      QStringLiteral("accent"), QStringLiteral("#ff0000"));
  QCOMPARE(revisionSpy.count(), 1);
  pm.setThemeOverride(QStringLiteral("theme-tokyo-night"), QStringLiteral("dark"),
                      QStringLiteral("accent"), QString());
  QCOMPARE(revisionSpy.count(), 2);
  QVERIFY(pm.themeOverrides(QStringLiteral("theme-tokyo-night"))
              .value(QStringLiteral("dark")).toMap().isEmpty());
}

void PluginTests::testCorruptSettingsSanitizedOnLoad() {
  const QString settingsDir = QDir(testConfigDir()).filePath(QStringLiteral("plugin-settings"));
  QVERIFY(QDir().mkpath(settingsDir));
  const QString settingsFile = QDir(settingsDir).filePath(QStringLiteral("theme-tokyo-night.json"));

  QVariantMap mode;
  mode.insert(QStringLiteral("accent"), QStringLiteral("#123456"));
  mode.insert(QStringLiteral("background"), QStringLiteral("not-a-color"));
  mode.insert(QStringLiteral("foreground"), QString());
  QVariantMap overrides;
  overrides.insert(QStringLiteral("dark"), mode);
  QVariantMap root;
  root.insert(QStringLiteral("themeOverrides"), overrides);
  QFile file(settingsFile);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(QJsonDocument(QJsonObject::fromVariantMap(root)).toJson());
  file.close();

  PluginManager pm;
  const QVariantMap dark = pm.themeOverrides(QStringLiteral("theme-tokyo-night"))
                               .value(QStringLiteral("dark")).toMap();
  QCOMPARE(dark.value(QStringLiteral("accent")).toString(), QStringLiteral("#123456"));
  QVERIFY(!dark.contains(QStringLiteral("background")));
  QVERIFY(!dark.contains(QStringLiteral("foreground")));

  // The sanitised palette is also what reaches the active theme.
  bool verified = false;
  for (const auto &theme : pm.activeThemes()) {
    const QVariantMap map = theme.toMap();
    if (map.value(QStringLiteral("pluginId")).toString() != QStringLiteral("theme-tokyo-night"))
      continue;
    const QVariantMap activeDark = map.value(QStringLiteral("dark")).toMap();
    QCOMPARE(activeDark.value(QStringLiteral("accent")).toString(), QStringLiteral("#123456"));
    // The bogus "background" override was dropped, so the manifest default
    // remains; "foreground" is not a palette key at all.
    QCOMPARE(activeDark.value(QStringLiteral("background")).toString(),
             QStringLiteral("#1a1b26"));
    QVERIFY(!activeDark.contains(QStringLiteral("foreground")));
    verified = true;
    break;
  }
  QVERIFY(verified);
}

void PluginTests::testIsValidIdRejectsTrailingNewline() {
  QVERIFY(PluginManifest::isValidId(QStringLiteral("plugin-a")));
  QVERIFY(PluginManifest::isValidId(QStringLiteral("a_b-9")));
  // PCRE `$` matches before a final newline; `\z` must not (issue #6).
  QVERIFY(!PluginManifest::isValidId(QStringLiteral("plugin-a\n")));
  QVERIFY(!PluginManifest::isValidId(QStringLiteral("plugin-a\r")));
  QVERIFY(!PluginManifest::isValidId(QStringLiteral("plugin-a ")));
  QVERIFY(!PluginManifest::isValidId(QString()));
}

void PluginTests::testUninstallRemovesDirectoryBeforeSettings() {
  const QString dir = QDir(testConfigDir()).filePath(QStringLiteral("plugins/uninstall-order"));
  writePlugin(dir, {{"id", "uninstall-order"},
                    {"name", "Uninstall Order"},
                    {"version", "1.0.0"},
                    {"bundled", false}});

  PluginManager pm;
  QVERIFY(pm.enablePlugin(QStringLiteral("uninstall-order")));
  pm.setPluginSetting(QStringLiteral("uninstall-order"), QStringLiteral("key"),
                      QStringLiteral("value"));
  const QString settingsFile =
      QDir(testConfigDir()).filePath(QStringLiteral("plugin-settings/uninstall-order.json"));
  QVERIFY(QFile::exists(settingsFile));

  // A directory at the settings path makes settings removal fail. Because the
  // directory is removed FIRST, uninstall still completes with a consistent
  // state instead of losing settings while keeping the plugin installed.
  QVERIFY(QFile::remove(settingsFile));
  QVERIFY(QDir().mkpath(settingsFile));
  QVERIFY(pm.uninstallPlugin(QStringLiteral("uninstall-order")));
  QVERIFY(!QDir(dir).exists());
  QVERIFY(QFile::exists(settingsFile)); // harmless orphan, guarded by knownPlugin()
  QVERIFY(!pm.isEnabled(QStringLiteral("uninstall-order")));
  QVERIFY(!pm.pluginInfo(QStringLiteral("uninstall-order")).contains(QStringLiteral("id")));
  QDir(settingsFile).removeRecursively();
}

void PluginTests::testBundledProvenancePersistsWithoutSource() {
  QTemporaryDir source;
  QVERIFY(source.isValid());
  qputenv("TEMPLNOTE_BUNDLED_PLUGINS_DIR", source.path().toUtf8());
  const QString srcDir = QDir(source.path()).filePath(QStringLiteral("prov-plugin"));
  writePlugin(srcDir, {{"id", "prov-plugin"},
                       {"name", "Provenance Plugin"},
                       {"version", "1.0.0"},
                       {"bundled", true}});

  {
    PluginManager first;
    QVERIFY(first.isBundled(QStringLiteral("prov-plugin")));
  }

  const QString installed = QDir(testConfigDir()).filePath(QStringLiteral("plugins/prov-plugin"));
  const QString marker = QDir(installed).filePath(QStringLiteral(".templnote-bundled"));
  QVERIFY(QFile::exists(marker));
  QFile markerFile(marker);
  QVERIFY(markerFile.open(QIODevice::ReadOnly));
  QCOMPARE(QString::fromUtf8(markerFile.readAll()).trimmed(),
           QStringLiteral("prov-plugin"));
  markerFile.close();

  // No bundled source this run: discovery must use the persisted marker.
  QTemporaryDir emptySource;
  QVERIFY(emptySource.isValid());
  qputenv("TEMPLNOTE_BUNDLED_PLUGINS_DIR", emptySource.path().toUtf8());
  {
    PluginManager second;
    QVERIFY(second.isBundled(QStringLiteral("prov-plugin")));
    QVERIFY(!second.canUninstall(QStringLiteral("prov-plugin")));
  }

  // Copying the marker onto a user plugin with a different id is still refused.
  const QString spoof = QDir(testConfigDir()).filePath(QStringLiteral("plugins/prov-spoof"));
  writePlugin(spoof, {{"id", "prov-spoof"},
                      {"name", "Spoof"},
                      {"version", "1.0.0"},
                      {"bundled", true}});
  QVERIFY(QFile::copy(marker, QDir(spoof).filePath(QStringLiteral(".templnote-bundled"))));
  PluginManager third;
  QVERIFY(!third.isBundled(QStringLiteral("prov-spoof")));
  QVERIFY(third.canUninstall(QStringLiteral("prov-spoof")));
}

void PluginTests::testRecopiedBundledPluginStaysDisabled() {
  QTemporaryDir source;
  QVERIFY(source.isValid());
  qputenv("TEMPLNOTE_BUNDLED_PLUGINS_DIR", source.path().toUtf8());
  const QString one = QDir(source.path()).filePath(QStringLiteral("recopy-one"));
  writePlugin(one, {{"id", "recopy-one"},
                    {"name", "Recopy One"},
                    {"version", "1.0.0"},
                    {"bundled", true}});

  {
    PluginManager first;
    QVERIFY(first.isEnabled(QStringLiteral("recopy-one")));
    QVERIFY(first.disablePlugin(QStringLiteral("recopy-one")));
  }

  // The user deletes the installed directory; the installer copies it back.
  const QString installedOne = QDir(testConfigDir()).filePath(QStringLiteral("plugins/recopy-one"));
  QVERIFY(QDir(installedOne).removeRecursively());

  // A genuinely new bundled plugin is added at the same time.
  const QString two = QDir(source.path()).filePath(QStringLiteral("recopy-two"));
  writePlugin(two, {{"id", "recopy-two"},
                    {"name", "Recopy Two"},
                    {"version", "1.0.0"},
                    {"bundled", true}});

  {
    PluginManager second;
    QVERIFY(second.isBundled(QStringLiteral("recopy-one")));
    // Explicit user choice survives the re-copy (issue #10).
    QVERIFY(!second.isEnabled(QStringLiteral("recopy-one")));
    QVERIFY(second.isEnabled(QStringLiteral("recopy-two")));
  }
}

void PluginTests::testSymlinkEscapeRejected() {
  const QString base = QDir(testConfigDir()).filePath(QStringLiteral("symlink-plugin"));
  const QString pluginDir = QDir(base).filePath(QStringLiteral("plugin"));
  const QString outsideDir = QDir(base).filePath(QStringLiteral("outside"));
  QVERIFY(QDir().mkpath(pluginDir));
  QVERIFY(QDir().mkpath(outsideDir));

  QFile outsideFile(QDir(outsideDir).filePath(QStringLiteral("Evil.qml")));
  QVERIFY(outsideFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
  outsideFile.write("x");
  outsideFile.close();

  const QString link = QDir(pluginDir).filePath(QStringLiteral("escape"));
  if (!QFile::link(outsideDir, link))
    QSKIP("Symlinks are not supported on this platform.");

  auto urlFor = [&](const QString &component) -> QUrl {
    const QJsonObject json = {
      {"id", "symlink-test"},
      {"capabilities", QJsonObject{
        {"decorations", QJsonArray{
          QJsonObject{{"slot", "paper-background"}, {"component", component}}
        }}
      }}
    };
    bool ok = false;
    const PluginManifest manifest = PluginManifest::fromJson(json, pluginDir, &ok);
    if (!ok || manifest.decorations().isEmpty())
      return {};
    return manifest.decorations().first().componentUrl;
  };

  // Lexically inside the plugin dir, but canonically outside it (issue #11).
  QVERIFY(urlFor(QStringLiteral("escape/Evil.qml")).isEmpty());
  // A plain relative path still resolves normally.
  QVERIFY(!urlFor(QStringLiteral("Evil.qml")).isEmpty());
}

void PluginTests::testResetPluginSettingsSanitizesDefaults() {
  const QString dir =
      QDir(testConfigDir()).filePath(QStringLiteral("plugins/reset-sanitize"));
  const QJsonObject manifest = {
    {"id", "reset-sanitize"},
    {"name", "Reset Sanitize"},
    {"version", "1.0.0"},
    {"bundled", false},
    {"category", "theme"},
    {"capabilities", QJsonObject{
      {"theme", QJsonObject{
        {"schemeKey", "rs"},
        {"name", QJsonObject{{"dark", "RS Dark"}, {"light", "RS Light"}}},
        {"dark", QJsonObject{{"background", "#111111"}, {"accent", "#222222"}}},
        {"light", QJsonObject{{"background", "#eeeeee"}, {"accent", "#dddddd"}}}
      }}
    }},
    {"settings", QJsonObject{
      {"component", "ThemeSettings.qml"},
      {"defaults", QJsonObject{
        {"themeOverrides", QJsonObject{
          {"dark", QJsonObject{
            {"accent", "#ff007f"},
            {"background", "not-a-color"} // corrupt default: must be dropped
          }}
        }}
      }}
    }}
  };
  writePlugin(dir, manifest);

  PluginManager pm;
  QVERIFY(pm.enablePlugin(QStringLiteral("reset-sanitize")));

  // Change a valid key, then reset. resetPluginSettings() must sanitise a copy
  // of the raw manifest defaults before caching/writing them; otherwise the
  // invalid "background" colour is injected straight back into the palette.
  pm.setThemeOverride(QStringLiteral("reset-sanitize"), QStringLiteral("dark"),
                      QStringLiteral("accent"), QStringLiteral("#00ff00"));
  pm.resetPluginSettings(QStringLiteral("reset-sanitize"));

  const QVariantMap overrides = pm.pluginSetting(QStringLiteral("reset-sanitize"),
                                                  QStringLiteral("themeOverrides")).toMap();
  const QVariantMap dark = overrides.value(QStringLiteral("dark")).toMap();
  QCOMPARE(dark.value(QStringLiteral("accent")).toString(), QStringLiteral("#ff007f"));
  QVERIFY(!dark.contains(QStringLiteral("background")));

  bool verified = false;
  for (const auto &theme : pm.activeThemes()) {
    if (theme.toMap().value(QStringLiteral("pluginId")).toString() !=
        QStringLiteral("reset-sanitize"))
      continue;
    const QVariantMap activeDark = theme.toMap().value(QStringLiteral("dark")).toMap();
    QCOMPARE(activeDark.value(QStringLiteral("accent")).toString(),
             QStringLiteral("#ff007f"));
    QCOMPARE(activeDark.value(QStringLiteral("background")).toString(),
             QStringLiteral("#111111"));
    verified = true;
    break;
  }
  QVERIFY(verified);
}

void PluginTests::testThemeOverridesJsonStringNormalized() {
  PluginManager pm;
  QVERIFY(pm.enablePlugin(QStringLiteral("theme-tokyo-night")));
  QSignalSpy settingSpy(&pm, &PluginManager::pluginSettingChanged);
  QSignalSpy revisionSpy(&pm, &PluginManager::settingsRevisionChanged);

  // A valid JSON object passed as a QString used to be validated but persisted
  // as a JSON *string*, which rebuildActiveState() ignored. It must now be
  // normalised to a real object before insertion so the signal is not a no-op.
  pm.setPluginSetting(QStringLiteral("theme-tokyo-night"),
                      QStringLiteral("themeOverrides"),
                      QStringLiteral("{\"dark\":{\"accent\":\"#00ff00\"}}"));
  QCOMPARE(settingSpy.count(), 1);
  QCOMPARE(revisionSpy.count(), 1);

  const QVariant stored = pm.pluginSetting(QStringLiteral("theme-tokyo-night"),
                                            QStringLiteral("themeOverrides"));
  QVERIFY(stored.metaType().id() != QMetaType::QString);
  const QVariantMap overrides = stored.toMap();
  QCOMPARE(overrides.value(QStringLiteral("dark")).toMap()
               .value(QStringLiteral("accent")).toString(),
           QStringLiteral("#00ff00"));

  bool verified = false;
  for (const auto &theme : pm.activeThemes()) {
    if (theme.toMap().value(QStringLiteral("pluginId")).toString() !=
        QStringLiteral("theme-tokyo-night"))
      continue;
    QCOMPARE(theme.toMap().value(QStringLiteral("dark")).toMap()
                 .value(QStringLiteral("accent")).toString(),
             QStringLiteral("#00ff00"));
    verified = true;
    break;
  }
  QVERIFY(verified);

  // A malformed JSON string is still rejected without emitting a change.
  pm.setPluginSetting(QStringLiteral("theme-tokyo-night"),
                      QStringLiteral("themeOverrides"),
                      QStringLiteral("{\"dark\":"));
  QCOMPARE(settingSpy.count(), 1);
  QCOMPARE(revisionSpy.count(), 1);
}

void PluginTests::testUninstallDuplicateRemovesAllDirectories() {
  const QString dirA =
      QDir(testConfigDir()).filePath(QStringLiteral("plugins/dup-a"));
  const QString dirB =
      QDir(testConfigDir()).filePath(QStringLiteral("plugins/dup-b"));
  writePlugin(dirA, {{"id", "dup-plugin"},
                     {"name", "Dup A"},
                     {"version", "1.0.0"},
                     {"bundled", false}});
  writePlugin(dirB, {{"id", "dup-plugin"},
                     {"name", "Dup B"},
                     {"version", "1.0.0"},
                     {"bundled", false}});

  PluginManager pm;
  QVERIFY(pm.canUninstall(QStringLiteral("dup-plugin")));
  QVERIFY(pm.enablePlugin(QStringLiteral("dup-plugin")));
  QVERIFY(pm.uninstallPlugin(QStringLiteral("dup-plugin")));

  // Both copies must be gone, otherwise the rescan inside uninstallPlugin()
  // rediscovers the leftover one and the card returns.
  QVERIFY(!QDir(dirA).exists());
  QVERIFY(!QDir(dirB).exists());
  QVERIFY(!pm.pluginInfo(QStringLiteral("dup-plugin")).contains(QStringLiteral("id")));

  int cards = 0;
  for (int row = 0; row < pm.pluginsModel()->rowCount(); ++row) {
    if (pm.pluginsModel()->data(pm.pluginsModel()->index(row),
                                PluginsModel::IdRole).toString() ==
        QStringLiteral("dup-plugin"))
      ++cards;
  }
  QCOMPARE(cards, 0);
}

void PluginTests::testEnableFailureReportsMissingDependencies() {
  const QString dir =
      QDir(testConfigDir()).filePath(QStringLiteral("plugins/needs-missing"));
  writePlugin(dir, {{"id", "needs-missing"},
                    {"name", "Needs Missing"},
                    {"version", "1.0.0"},
                    {"bundled", false},
                    {"dependencies", QJsonArray{"absent-dep"}}});

  PluginManager pm;
  QVERIFY(pm.missingDependencies(QStringLiteral("needs-missing"))
              .contains(QStringLiteral("absent-dep")));

  // A failed enable emits no dependenciesRequired signal, so the QML notice
  // is built directly from missingDependencies(). Verify that data source
  // remains accurate after the failed enable.
  QSignalSpy requiredSpy(&pm, &PluginManager::dependenciesRequired);
  QVERIFY(!pm.enablePlugin(QStringLiteral("needs-missing")));
  QCOMPARE(requiredSpy.count(), 0);
  QVERIFY(pm.missingDependencies(QStringLiteral("needs-missing"))
              .contains(QStringLiteral("absent-dep")));
}

void PluginTests::testBundledThemeManifestVersions() {
  // The four bundled themes ship an updated ThemeSettings.qml (remove-override
  // button). Their manifest version must be past 1.0.0 so the installer's
  // version comparison refreshes an existing copy instead of leaving the old
  // settings component in place.
  const QStringList themes = {
    QStringLiteral("theme-github"),
    QStringLiteral("theme-gruvbox"),
    QStringLiteral("theme-solarized-osaka"),
    QStringLiteral("theme-tokyo-night")
  };

  PluginManager pm;
  for (const QString &id : themes) {
    const QString path =
        QDir(m_testBundledDir).filePath(id + QStringLiteral("/manifest.json"));
    QVERIFY2(QFile::exists(path), qPrintable(path));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    QVERIFY(doc.isObject());

    const QJsonObject obj = doc.object();
    QCOMPARE(obj.value(QStringLiteral("id")).toString(), id);
    QCOMPARE(obj.value(QStringLiteral("version")).toString(),
             QStringLiteral("1.0.1"));

    // The same bumped version is what the manager exposes to QML/cards.
    QCOMPARE(pm.pluginInfo(id).value(QStringLiteral("version")).toString(),
             QStringLiteral("1.0.1"));
  }
}

void PluginTests::testBundledPaperManifestVersion() {
  // The installed plugin is refreshed only when the manifest version grows.
  // Bump this whenever its QML changes, otherwise users keep running an old
  // copy whose settings bindings are broken.
  const QString path = QDir(m_testBundledDir)
                           .filePath(QStringLiteral("paper-background/manifest.json"));
  QVERIFY2(QFile::exists(path), qPrintable(path));

  QFile file(path);
  QVERIFY(file.open(QIODevice::ReadOnly));
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  file.close();
  QVERIFY(doc.isObject());
  QCOMPARE(doc.object().value(QStringLiteral("version")).toString(),
           QStringLiteral("2.0.1"));
}

QTEST_MAIN(PluginTests)
#include "tst_plugins.moc"
