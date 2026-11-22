import QtQuick

Column {
  id: root

  width: parent ? parent.width : 0
  spacing: 28

  // Theme plugins are managed here rather than in the Plugins page: selecting
  // a colour scheme is the primary action, and enable/disable plus per-colour
  // overrides are exposed on the same card set.
  property string customizingThemeId: ""

  function enableTheme(id) {
    if (typeof pluginManager === "undefined" || !pluginManager) return
    pluginManager.enablePlugin(id)
  }

  function disableTheme(id) {
    if (typeof pluginManager === "undefined" || !pluginManager) return
    pluginManager.disablePlugin(id)
    if (root.customizingThemeId === id)
      root.customizingThemeId = ""
  }

  Column {
    width: parent.width
    spacing: 12

    Text {
      text: "Theme"
      color: Theme.text
      font.pixelSize: 15
      font.bold: true
    }

    Text {
      width: parent.width
      text: "Choose whether Templnote follows your system appearance or always stays light or dark."
      color: Theme.textMuted
      font.pixelSize: 12
      wrapMode: Text.Wrap
    }

    Row {
      spacing: 8

      Repeater {
        model: [
          { key: "system", label: "System" },
          { key: "light", label: "Light" },
          { key: "dark", label: "Dark" }
        ]

        delegate: Rectangle {
          id: modeButton

          required property var modelData

          readonly property bool selected: Theme.mode === modelData.key

          width: 104
          height: 38
          radius: 8
          color: selected ? Theme.tint(Theme.accent, 0.16) : Theme.surfaceAlt
          border.width: selected ? 1 : 0
          border.color: Theme.accent

          Text {
            anchors.centerIn: parent
            text: modeButton.modelData.label
            color: modeButton.selected ? Theme.accent : Theme.textMuted
            font.pixelSize: 13
          }

          MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: Theme.setMode(modeButton.modelData.key)
          }
        }
      }
    }
  }

  Column {
    width: parent.width
    spacing: 12

    Text {
      text: "Colour scheme"
      color: Theme.text
      font.pixelSize: 15
      font.bold: true
    }

    Text {
      width: parent.width
      text: "Colour schemes are provided by theme plugins. Select one here, or enable and customise it without leaving this page."
      color: Theme.textMuted
      font.pixelSize: 12
      wrapMode: Text.Wrap
    }

    // Scheme cards, one per theme plugin (including disabled ones). Using the
    // plugin model rather than Theme.schemeKeys keeps disabled themes visible
    // so they can still be enabled from here.
    Grid {
      visible: root.customizingThemeId.length === 0
      width: parent.width
      columns: 2
      columnSpacing: 12
      rowSpacing: 12

      Repeater {
        model: typeof pluginManager !== "undefined" && pluginManager
               ? pluginManager.pluginsModel : null

        delegate: Rectangle {
          id: themeCard

          required property string id
          required property string name
          required property string description
          required property string category
          required property bool bundled

          readonly property bool pluginEnabled: {
            if (typeof pluginManager === "undefined" || !pluginManager)
              return false
            // isEnabled() has no NOTIFY; the revision read keeps the card in
            // sync when a theme is toggled from this page.
            const revision = pluginManager.stateRevision
            return pluginManager.isEnabled(themeCard.id)
          }
          readonly property var pluginInfo: typeof pluginManager !== "undefined" && pluginManager
                                            ? pluginManager.pluginInfo(themeCard.id) : null
          // Theme.schemes is keyed by schemeKey, not by plugin id.
          readonly property string schemeKey: pluginInfo && pluginInfo.schemeKey
                                              ? pluginInfo.schemeKey : ""
          readonly property var schemeInfo: Theme.schemes[themeCard.schemeKey]
          readonly property var preview: schemeInfo && schemeInfo[Theme.resolvedMode]
                                         ? schemeInfo[Theme.resolvedMode] : null
          readonly property bool selected: Theme.scheme === themeCard.schemeKey

          visible: themeCard.category === "theme"
          width: (root.width - 12) / 2
          height: 124
          radius: 8
          color: Theme.surfaceAlt
          border.width: selected ? 2 : 1
          border.color: selected ? Theme.accent : Theme.border

          // Behind the content so the action buttons keep their own clicks.
          MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
              if (themeCard.pluginEnabled) {
                if (themeCard.schemeKey.length > 0)
                  Theme.setScheme(themeCard.schemeKey)
              } else {
                root.enableTheme(themeCard.id)
              }
            }
          }

          Column {
            anchors {
              fill: parent
              margins: 12
            }
            spacing: 9

            Row {
              width: parent.width
              spacing: 8

              Text {
                width: parent.width - 22
                text: themeCard.name
                color: Theme.text
                font.pixelSize: 14
                font.bold: true
                elide: Text.ElideRight
              }

              Text {
                visible: themeCard.selected
                text: "\u2713"
                color: Theme.accent
                font.pixelSize: 14
                font.bold: true
              }
            }

            Row {
              spacing: 7

              Repeater {
                model: themeCard.preview ? [
                  themeCard.preview.background,
                  themeCard.preview.surface,
                  themeCard.preview.text,
                  themeCard.preview.accent
                ] : []

                delegate: Rectangle {
                  required property color modelData

                  width: 20
                  height: 20
                  radius: 10
                  color: modelData
                  border.width: 1
                  border.color: Theme.tint(Theme.text, 0.16)
                }
              }

              Text {
                visible: !themeCard.preview
                text: "Enable to preview this palette"
                color: Theme.textFaint
                font.pixelSize: 11
              }
            }

            Row {
              width: parent.width
              spacing: 10

              Text {
                width: Math.max(0, parent.width - 140)
                anchors.verticalCenter: parent.verticalCenter
                text: themeCard.pluginEnabled
                      ? (Theme.resolvedMode === "dark" ? "Dark palette" : "Light palette")
                      : "Disabled"
                color: themeCard.pluginEnabled ? Theme.textFaint : Theme.danger
                font.pixelSize: 11
                elide: Text.ElideRight
              }

              Text {
                anchors.verticalCenter: parent.verticalCenter
                text: themeCard.pluginEnabled ? "Disable" : "Enable"
                color: Theme.accent
                font.pixelSize: 11
                font.bold: true

                MouseArea {
                  anchors.fill: parent
                  cursorShape: Qt.PointingHandCursor
                  onClicked: {
                    if (themeCard.pluginEnabled)
                      root.disableTheme(themeCard.id)
                    else
                      root.enableTheme(themeCard.id)
                  }
                }
              }

              Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: themeCard.pluginEnabled
                text: "Customize"
                color: Theme.accent
                font.pixelSize: 11
                font.bold: true

                MouseArea {
                  anchors.fill: parent
                  cursorShape: Qt.PointingHandCursor
                  onClicked: root.customizingThemeId = themeCard.id
                }
              }
            }
          }
        }
      }
    }

    // Inline per-plugin settings (palette overrides) for the chosen theme.
    Column {
      visible: root.customizingThemeId.length > 0
      width: parent.width
      spacing: 16

      Row {
        width: parent.width
        spacing: 12

        Rectangle {
          id: themeBackButton
          width: 32
          height: 32
          radius: 8
          color: themeBackMouse.containsMouse ? Theme.surfaceHover : Theme.surfaceAlt
          border.width: 1
          border.color: Theme.border

          Text {
            anchors.centerIn: parent
            text: "←"
            color: Theme.text
            font.pixelSize: 16
            font.bold: true
          }

          MouseArea {
            id: themeBackMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.customizingThemeId = ""
          }
        }

        Column {
          anchors.verticalCenter: parent.verticalCenter
          width: parent.width - 44
          spacing: 2

          Text {
            text: {
              if (root.customizingThemeId.length === 0 ||
                  typeof pluginManager === "undefined" || !pluginManager)
                return ""
              const info = pluginManager.pluginInfo(root.customizingThemeId)
              return info.name ? info.name : root.customizingThemeId
            }
            color: Theme.text
            font.pixelSize: 15
            font.bold: true
          }

          Text {
            text: "Edit the dark and light palettes for this colour scheme."
            color: Theme.textMuted
            font.pixelSize: 11
          }
        }
      }

      Rectangle {
        width: parent.width
        height: 1
        color: Theme.border
      }

      Loader {
        id: themeSettingsLoader
        width: parent.width
        source: root.customizingThemeId.length > 0 &&
                typeof pluginManager !== "undefined" && pluginManager
                ? pluginManager.settingsComponent(root.customizingThemeId) : ""

        property var pluginContext: ({
          pluginId: root.customizingThemeId,
          getSetting: function(key) {
            return pluginManager
                   ? pluginManager.pluginSetting(root.customizingThemeId, key)
                   : undefined
          },
          setSetting: function(key, value) {
            if (pluginManager)
              pluginManager.setPluginSetting(root.customizingThemeId, key, value)
          },
          theme: Theme
        })
      }
    }
  }

  Rectangle {
    width: parent.width
    height: 1
    color: Theme.border
  }

  Column {
    width: parent.width
    spacing: 6

    Text {
      text: "Preview"
      color: Theme.text
      font.pixelSize: 15
      font.bold: true
    }

    Rectangle {
      width: parent.width
      height: 92
      radius: 8
      color: Theme.background
      border.width: 1
      border.color: Theme.border

      Column {
        anchors {
          fill: parent
          margins: 16
        }

        spacing: 7

        Text {
          text: Theme.schemeName
          color: Theme.text
          font.pixelSize: 17
          font.bold: true
        }

        Text {
          text: "The quick brown fox jumps over the lazy dog."
          color: Theme.textMuted
          font.pixelSize: 13
        }

        Row {
          spacing: 8

          Rectangle {
            width: 92
            height: 26
            radius: 8
            color: Theme.accent

            Text {
              anchors.centerIn: parent
              text: "Accent"
              color: Theme.accentText
              font.pixelSize: 11
              font.bold: true
            }
          }

          Rectangle {
            width: 92
            height: 26
            radius: 8
            color: Theme.surfaceAlt
            border.width: 1
            border.color: Theme.border

            Text {
              anchors.centerIn: parent
              text: "Surface"
              color: Theme.textMuted
              font.pixelSize: 11
            }
          }
        }
      }
    }
  }
}
