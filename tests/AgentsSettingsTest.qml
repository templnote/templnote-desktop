import QtQuick
import QtQuick.Controls
import TemplnoteAISettingsTest

// Smoke harness for the standalone Agents settings page.
ApplicationWindow {
  width: 700
  height: 900
  visible: true

  AgentsSettings {
    width: parent.width
  }
}
