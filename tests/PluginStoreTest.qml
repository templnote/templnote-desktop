import QtQuick
import TemplnoteAISettingsTest

// Smoke harness: instantiate the real store settings page against a real
// PluginStoreClient so QML binding/delegate errors are caught by CTest.
Item {
  width: 700
  height: 900

  PluginStoreSettings {
    width: parent.width
  }
}
