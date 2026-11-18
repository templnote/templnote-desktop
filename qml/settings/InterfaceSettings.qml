import QtQuick
import QtQuick.Dialogs

Column {
  id: root

  width: parent ? parent.width : 0
  spacing: 22

  Text {
    text: "Layout"
    color: Theme.text
    font.pixelSize: 15
    font.bold: true
  }

  SettingToggle {
    title: "Show navigation rail"
    description: "Keep the icon rail visible on the left side of the window."
    checked: Preferences.showNavigationRail
    onToggled: function(value) {
      Preferences.showNavigationRail = value
    }
  }

  SettingToggle {
    title: "Expand the file explorer on launch"
    description: "Open the document explorer automatically when Templnote starts."
    checked: Preferences.expandExplorerOnLaunch
    onToggled: function(value) {
      Preferences.expandExplorerOnLaunch = value
    }
  }

  SettingChoice {
    title: "Density"
    description: "Control the amount of vertical space between interface elements."
    options: ["Compact", "Comfortable", "Spacious"]
    value: Preferences.density
    onSelected: function(value) {
      Preferences.density = value
    }
  }

  SettingToggle {
    title: "Full workspace paper background"
    description: "Extend tactile paper texture across the whole workspace instead of only the document sheet."
    checked: Preferences.paperOverWorkspace
    onToggled: function(value) {
      Preferences.paperOverWorkspace = value
    }
  }

  Rectangle {
    width: parent.width
    height: 1
    color: Theme.border
  }

  Text {
    text: "Paper Aesthetic"
    color: Theme.text
    font.pixelSize: 15
    font.bold: true
  }

  Column {
    width: parent.width
    spacing: 10

    Text {
      text: "Paper preset"
      color: Theme.text
      font.pixelSize: 14
    }

    Text {
      width: parent.width
      text: "Select a physical paper profile. Each preset calibrates the fiber grain roughness."
      color: Theme.textMuted
      font.pixelSize: 12
      wrapMode: Text.Wrap
    }

    Row {
      spacing: 12
      width: parent.width

      Repeater {
        model: [
          { key: "Office", label: "Office", desc: "Clean & smooth bond paper" },
          { key: "Journal", label: "Journal", desc: "Tactile textured washi paper" },
          { key: "Craft", label: "Craft", desc: "Heavy fibrous deckle cardstock" }
        ]

        delegate: Rectangle {
          id: presetBox
          required property var modelData
          readonly property bool active: Preferences.paperPreset === modelData.key

          width: (root.width - 24) / 3
          height: 72
          radius: 8
          color: active ? Theme.tint(Theme.accent, 0.16) : Theme.surfaceAlt
          border.width: active ? 2 : 1
          border.color: active ? Theme.accent : Theme.border

          Column {
            anchors {
              fill: parent
              margins: 10
            }
            spacing: 4

            Item {
              width: parent.width
              height: 20

              Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: presetBox.modelData.label
                color: presetBox.active ? Theme.accent : Theme.text
                font.pixelSize: 13
                font.bold: true
              }

              Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: presetBox.active
                text: "\u2713"
                color: Theme.accent
                font.pixelSize: 12
                font.bold: true
              }
            }

            Text {
              width: parent.width
              text: presetBox.modelData.desc
              color: Theme.textMuted
              font.pixelSize: 10
              wrapMode: Text.Wrap
            }
          }

          MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: Preferences.applyPaperPreset(presetBox.modelData.key)
          }
        }
      }
    }
  }

  SettingSlider {
    title: "Texture grain roughness"
    description: "Adjust the contrast and visibility of microscopic pulp fibers."
    from: 0.005
    to: 0.080
    stepSize: 0.005
    value: Preferences.paperGrain
    valueText: (value * 100).toFixed(1) + "%"
    onMoved: function(val) {
      Preferences.paperGrain = val
    }
  }

  // Paper Substrate Surface (Solid Theme, Gradient, or Custom Wallpaper)
  Column {
    width: parent.width
    spacing: 12

    Text {
      text: "Substrate background"
      color: Theme.text
      font.pixelSize: 14
    }

    Text {
      width: parent.width
      text: "Fuse a procedural gradient or custom wallpaper image into the paper fibers using photorealistic soft-light blending."
      color: Theme.textMuted
      font.pixelSize: 12
      wrapMode: Text.Wrap
    }

    // 3 Mode Selection Cards
    Row {
      spacing: 12
      width: parent.width

      Repeater {
        model: [
          { key: "Solid", label: "Theme Solid", desc: "Default clean palette" },
          { key: "Gradient", label: "Gradient", desc: "Two-tone ambient gradient" },
          { key: "Wallpaper", label: "Custom Wallpaper", desc: "User image substrate" }
        ]

        delegate: Rectangle {
          id: bgModeBox
          required property var modelData
          readonly property bool active: Preferences.paperBgMode === modelData.key

          width: (root.width - 24) / 3
          height: 64
          radius: 8
          color: active ? Theme.tint(Theme.accent, 0.16) : Theme.surfaceAlt
          border.width: active ? 2 : 1
          border.color: active ? Theme.accent : Theme.border

          Column {
            anchors {
              fill: parent
              margins: 10
            }
            spacing: 3

            Item {
              width: parent.width
              height: 18

              Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: bgModeBox.modelData.label
                color: bgModeBox.active ? Theme.accent : Theme.text
                font.pixelSize: 13
                font.bold: true
              }

              Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: bgModeBox.active
                text: "\u2713"
                color: Theme.accent
                font.pixelSize: 12
                font.bold: true
              }
            }

            Text {
              width: parent.width
              text: bgModeBox.modelData.desc
              color: Theme.textMuted
              font.pixelSize: 10
              wrapMode: Text.Wrap
            }
          }

          MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: Preferences.paperBgMode = bgModeBox.modelData.key
          }
        }
      }
    }

    // Gradient Presets Section (visible when Gradient mode is active)
    Column {
      visible: Preferences.paperBgMode === "Gradient"
      width: parent.width
      spacing: 8

      Text {
        text: "Gradient Presets"
        color: Theme.text
        font.pixelSize: 12
        font.bold: true
      }

      Grid {
        columns: 3
        columnSpacing: 10
        rowSpacing: 10
        width: parent.width

        Repeater {
          model: [
            "Warm Parchment",
            "Sunset Glow",
            "Northern Sky",
            "Cyber Dusk",
            "Tokyo Neon",
            "Ethereal Mist"
          ]

          delegate: Rectangle {
            id: gradCard
            required property string modelData
            readonly property var preset: Preferences.gradientPresets[modelData]
            readonly property bool active: Preferences.paperGradientPreset === modelData

            width: (root.width - 20) / 3
            height: 38
            radius: 6
            border.width: active ? 2 : 1
            border.color: active ? Theme.accent : Theme.border

            gradient: Gradient {
              orientation: Gradient.Horizontal
              GradientStop { position: 0.0; color: gradCard.preset ? gradCard.preset.c1 : "#fff" }
              GradientStop { position: 1.0; color: gradCard.preset ? gradCard.preset.c2 : "#000" }
            }

            Rectangle {
              anchors.fill: parent
              radius: 6
              color: Qt.rgba(0, 0, 0, 0.28)
            }

            Text {
              anchors.centerIn: parent
              text: modelData
              color: "#ffffff"
              font.pixelSize: 11
              font.bold: true
            }

            MouseArea {
              anchors.fill: parent
              cursorShape: Qt.PointingHandCursor
              onClicked: Preferences.applyGradientPreset(gradCard.modelData)
            }
          }
        }
      }
    }

    // Custom Wallpaper Picker Section (visible when Wallpaper mode is active)
    Column {
      visible: Preferences.paperBgMode === "Wallpaper"
      width: parent.width
      spacing: 10

      FileDialog {
        id: wallpaperFileDialog
        title: "Select Wallpaper Image"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp)"]
        onAccepted: {
          Preferences.paperWallpaperPath = wallpaperFileDialog.selectedFile
        }
      }

      Row {
        spacing: 12
        width: parent.width

        Rectangle {
          id: chooseFileBtn
          width: 140
          height: 36
          radius: 8
          color: Theme.accent

          Text {
            anchors.centerIn: parent
            text: "Choose image..."
            color: Theme.accentText
            font.pixelSize: 12
            font.bold: true
          }

          MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: wallpaperFileDialog.open()
          }
        }

        Rectangle {
          visible: Preferences.paperWallpaperPath.length > 0
          width: 90
          height: 36
          radius: 8
          color: Theme.surfaceAlt
          border.width: 1
          border.color: Theme.border

          Text {
            anchors.centerIn: parent
            text: "Clear"
            color: Theme.textMuted
            font.pixelSize: 12
          }

          MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: Preferences.paperWallpaperPath = ""
          }
        }

        Text {
          anchors.verticalCenter: chooseFileBtn.verticalCenter
          width: parent.width - 250
          text: Preferences.paperWallpaperPath.length > 0
                ? Preferences.paperWallpaperPath.split("/").pop()
                : "No wallpaper selected"
          color: Theme.textMuted
          font.pixelSize: 12
          elide: Text.ElideMiddle
        }
      }
    }

    // Text Contrast & Substrate Dimming Slider (for Wallpaper or Gradient)
    SettingSlider {
      visible: Preferences.paperBgMode !== "Solid"
      title: "Text contrast & dimming"
      description: "Blends the wallpaper or gradient toward the theme background so text blocks remain legible."
      from: 0.0
      to: 0.90
      stepSize: 0.05
      value: Preferences.paperWallpaperDim
      valueText: (value * 100).toFixed(0) + "%"
      onMoved: function(val) {
        Preferences.paperWallpaperDim = val
      }
    }
  }

  Rectangle {
    width: parent.width
    height: 1
    color: Theme.border
  }

  Text {
    text: "Motion"
    color: Theme.text
    font.pixelSize: 15
    font.bold: true
  }

  SettingToggle {
    title: "Reduce motion"
    description: "Disable panel and toggle animations across the interface."
    checked: Preferences.reduceMotion
    onToggled: function(value) {
      Preferences.reduceMotion = value
    }
  }

  SettingToggle {
    title: "Animate the file explorer"
    description: "Slide the explorer open and closed instead of snapping it."
    checked: Preferences.animateExplorer
    onToggled: function(value) {
      Preferences.animateExplorer = value
    }
  }
}
