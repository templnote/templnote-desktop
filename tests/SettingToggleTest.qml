import QtQuick
import TemplnoteAISettingsTest

// Regression harness for SettingToggle: the title/description column used to
// lose its right anchor when the switch was replaced, hiding every label.
Item {
  width: 700
  height: 200

  property alias toggle: toggle

  SettingToggle {
    id: toggle

    width: parent.width
    title: "Restore last session"
    description: "Reopen the documents that were open when Templnote was closed."
    checked: true
  }
}
