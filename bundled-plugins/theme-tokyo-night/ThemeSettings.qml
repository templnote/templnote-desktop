import QtQuick
import QtQuick.Controls
import Templnote

Column {
  id: root

  property var pluginContext: null
  readonly property string pluginId: pluginContext && pluginContext.pluginId ? pluginContext.pluginId : ""

  width: parent ? parent.width : 0
  spacing: 20

  property string selectedMode: "dark"

  readonly property var colorKeys: [
    { key: "background", label: "Background" },
    { key: "surface", label: "Surface" },
    { key: "surfaceAlt", label: "Surface Alt" },
    { key: "surfaceHover", label: "Surface Hover" },
    { key: "border", label: "Border" },
    { key: "text", label: "Text" },
    { key: "textMuted", label: "Text Muted" },
    { key: "textFaint", label: "Text Faint" },
    { key: "accent", label: "Accent" },
    { key: "accentText", label: "Accent Text" },
    { key: "danger", label: "Danger" }
  ]

  // Mode switcher (Dark / Light palette)
  Row {
    spacing: 8

    Button {
      text: "Dark Palette"
      highlighted: root.selectedMode === "dark"
      onClicked: root.selectedMode = "dark"
    }

    Button {
      text: "Light Palette"
      highlighted: root.selectedMode === "light"
      onClicked: root.selectedMode = "light"
    }

    Item { width: 16; height: 1 }

    Button {
      text: "Reset Overrides"
      onClicked: {
        if (root.pluginId.length > 0 && typeof pluginManager !== "undefined") {
          pluginManager.resetThemeOverrides(root.pluginId)
        }
      }
    }
  }

  // Grid of colors
  Grid {
    width: parent.width
    columns: 2
    columnSpacing: 16
    rowSpacing: 12

    Repeater {
      model: root.colorKeys

      delegate: Rectangle {
        id: colorCard
        required property var modelData

        readonly property string key: modelData.key
        readonly property string label: modelData.label

        // True when a per-key override exists for the currently selected mode.
        readonly property bool hasOverride: {
          if (!pluginManager || root.pluginId.length === 0)
            return false
          // themeOverrides() is a method (not reactive); reading the NOTIFY
          // revision property gives QML a dependency that updates after edits.
          const rev = pluginManager.settingsRevision
          if (rev < 0)
            return false
          const modeOverrides = pluginManager.themeOverrides(root.pluginId)[root.selectedMode]
          return !!modeOverrides && modeOverrides[colorCard.key] !== undefined
        }

        // Get current effective color value from activeThemes
        readonly property string currentColor: {
          if (!pluginManager || root.pluginId.length === 0)
            return "#ffffff"
          const themes = pluginManager.activeThemes
          for (let i = 0; i < themes.length; ++i) {
            if (themes[i].pluginId === root.pluginId) {
              const palette = themes[i][root.selectedMode]
              if (palette && palette[key] !== undefined)
                return palette[key].toString()
            }
          }
          return "#ffffff"
        }

        width: (root.width - 16) / 2
        height: 48
        radius: 8
        color: Theme.surfaceAlt
        border.width: 1
        border.color: Theme.border

        Row {
          anchors.fill: parent
          anchors.margins: 8
          spacing: 12

          Rectangle {
            width: 32
            height: 32
            radius: 6
            color: colorCard.currentColor
            border.width: 1
            border.color: Theme.border
          }

          Column {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 52
            spacing: 2

            Text {
              text: colorCard.label
              color: Theme.text
              font.pixelSize: 12
              font.bold: true
            }

            TextInput {
              id: hexInput
              text: colorCard.currentColor
              color: Theme.textMuted
              font.pixelSize: 11
              selectByMouse: true

              onEditingFinished: {
                const val = hexInput.text.trim()
                if (val.length > 0 && root.pluginId.length > 0 && typeof pluginManager !== "undefined") {
                  pluginManager.setThemeOverride(root.pluginId, root.selectedMode, colorCard.key, val)
                }
              }
            }

            // Per-key removal: an empty value tells PluginManager to drop just
            // this override (the global button clears every override).
            Button {
              text: "Remove override"
              visible: colorCard.hasOverride
              onClicked: {
                if (root.pluginId.length > 0 && typeof pluginManager !== "undefined") {
                  pluginManager.setThemeOverride(root.pluginId, root.selectedMode, colorCard.key, "")
                }
              }
            }
          }
        }
      }
    }
  }
}
