import QtQuick
import QtQuick.Controls
import TemplnoteAISettingsTest

// Smoke harness for the standalone Models settings page.
ApplicationWindow {
  width: 700
  height: 900
  visible: true

  AIModelsSettings {
    width: parent.width
  }
}
