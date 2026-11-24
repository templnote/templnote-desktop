import QtQuick
import QtQuick.Controls
import TemplnoteAISettingsTest

// Smoke harness: instantiate the real AI settings page against the real
// ProviderRegistry so QML errors in bindings/delegates are caught by CTest.
// An ApplicationWindow gives Popups a real Overlay.overlay to attach to.
ApplicationWindow {
  width: 700
  height: 900
  visible: true

  AISettings {
    width: parent.width
  }
}
