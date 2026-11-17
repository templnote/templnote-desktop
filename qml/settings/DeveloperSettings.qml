import QtQuick
import Templnote

Column {
  id: root

  width: parent ? parent.width : 0
  spacing: 22

  Text {
    text: "Block Inspection"
    color: Theme.text
    font.pixelSize: 15
    font.bold: true
  }

  SettingToggle {
    title: "Show block debug borders"
    description: "Display orange bounding boxes and blue focus outlines with block height indicators."
    checked: Preferences.showBlockDebugBorders
    onToggled: function(val) {
      Preferences.showBlockDebugBorders = val
    }
  }

  SettingToggle {
    title: "Show block type badges"
    description: "Display a badge showing the block index, block ID, and block type."
    checked: Preferences.showBlockTypeBadge
    onToggled: function(val) {
      Preferences.showBlockTypeBadge = val
    }
  }

  SettingToggle {
    title: "Show drag and drop layout bounds"
    description: "Highlight drop indicator positions and target insertion lines during reordering."
    checked: Preferences.showDragDropBounds
    onToggled: function(val) {
      Preferences.showDragDropBounds = val
    }
  }

  Rectangle {
    width: parent.width
    height: 1
    color: Theme.border
  }

  Text {
    text: "Performance & Diagnostics"
    color: Theme.text
    font.pixelSize: 15
    font.bold: true
  }

  SettingToggle {
    title: "Show FPS counter overlay"
    description: "Display a live frames-per-second and render performance overlay."
    checked: Preferences.showFpsOverlay
    onToggled: function(val) {
      Preferences.showFpsOverlay = val
    }
  }

  SettingChoice {
    title: "FPS counter position"
    description: "Screen corner to anchor the FPS overlay."
    options: ["Top-Left", "Top-Right", "Bottom-Left", "Bottom-Right"]
    value: Preferences.fpsOverlayPosition
    onSelected: function(val) {
      Preferences.fpsOverlayPosition = val
    }
  }
}
