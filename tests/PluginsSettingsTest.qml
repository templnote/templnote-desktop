import QtQuick
import TemplnoteAISettingsTest

// Smoke harness for the Plugins settings page, whose enable switches were
// changed to the shared themed SettingSwitch component.
Item {
  width: 700
  height: 900

  PluginsSettings {
    width: parent.width
  }
}
