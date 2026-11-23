import QtQuick
import TemplnoteAISettingsTest

// Smoke harness: instantiate the real AI settings page against the real
// ProviderRegistry so QML errors in bindings/delegates are caught by CTest.
Item {
  width: 700
  height: 900

  AISettings {
    width: parent.width
  }
}
