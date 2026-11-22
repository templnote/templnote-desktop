import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Templnote

Column {
  id: root

  property var pluginContext: null
  readonly property string pluginId: pluginContext && pluginContext.pluginId
                                     ? pluginContext.pluginId : "paper-background"

  width: parent ? parent.width : 0
  spacing: 20

  // A Loader applies its extra properties (including pluginContext) after the
  // loaded component's own property initializers have run. Reading through
  // pluginContext here would therefore return every default, so go straight to
  // pluginManager. The revision read gives the bindings a real dependency on
  // saved values.
  readonly property int settingsRevision: typeof pluginManager !== "undefined" && pluginManager
                                          ? pluginManager.settingsRevision : 0

  function read(key, fallback) {
    const rev = root.settingsRevision
    if (typeof pluginManager !== "undefined" && pluginManager) {
      const value = pluginManager.pluginSetting(root.pluginId, key)
      if (value !== undefined && value !== null)
        return value
    }
    return fallback
  }

  function write(key, value) {
    if (typeof pluginManager !== "undefined" && pluginManager)
      pluginManager.setPluginSetting(root.pluginId, key, value)
  }

  property string paperPreset: read("paperPreset", "Journal").toString()
  property real grain: Number(read("paperGrain", 0.035))
  property bool fullWorkspace: Boolean(read("paperOverWorkspace", true))
  property string substrateMode: read("paperBgMode", "Solid").toString()
  property string gradientPreset: read("gradientPreset", "Warm Parchment").toString()
  property real gradientOpacity: Number(read("gradientOpacity", 0.35))
  property real gradientAngle: Number(read("gradientAngle", 135))
  property real paperDim: Number(read("paperDim", 0.40))
  property string wallpaperPath: read("wallpaperPath", "").toString()
  property string color1: read("gradientColor1", "#fdf6e3").toString()
  property string color2: read("gradientColor2", "#e0d5be").toString()

  readonly property var gradientPresets: ({
    "Warm Parchment": { c1: "#fdf6e3", c2: "#e0d5be", angle: 135 },
    "Sunset Glow":    { c1: "#ffecd2", c2: "#fcb69f", angle: 120 },
    "Northern Sky":   { c1: "#d4fc79", c2: "#96e6a1", angle: 135 },
    "Cyber Dusk":     { c1: "#2b1055", c2: "#7597de", angle: 160 },
    "Tokyo Neon":     { c1: "#1f1d36", c2: "#3f3351", angle: 135 },
    "Ethereal Mist":  { c1: "#e0c3fc", c2: "#8ec5fc", angle: 135 }
  })

  function applyPaperPreset(name) {
    let value = 0.035
    if (name === "Office") value = 0.015
    else if (name === "Craft") value = 0.065
    root.paperPreset = name
    root.grain = value
    root.write("paperPreset", name)
    root.write("paperGrain", value)
  }

  function applyGradientPreset(name) {
    const preset = root.gradientPresets[name]
    if (!preset) return
    root.gradientPreset = name
    root.color1 = preset.c1
    root.color2 = preset.c2
    root.gradientAngle = preset.angle
    root.write("gradientPreset", name)
    root.write("gradientColor1", preset.c1)
    root.write("gradientColor2", preset.c2)
    root.write("gradientAngle", preset.angle)
  }

  FileDialog {
    id: wallpaperDialog
    title: "Select Wallpaper Image"
    nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp)"]
    onAccepted: {
      const path = wallpaperDialog.selectedFile.toString()
      root.wallpaperPath = path
      root.write("wallpaperPath", path)
    }
  }

  // ── Paper fibre ──
  Column {
    width: parent.width
    spacing: 12

    Text {
      text: "Paper Preset Profile"
      color: Theme.text
      font.pixelSize: 14
      font.bold: true
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
          readonly property bool active: root.paperPreset === modelData.key

          width: (root.width - 24) / 3
          height: 72
          radius: 8
          color: active ? Theme.tint(Theme.accent, 0.16) : Theme.surfaceAlt
          border.width: active ? 2 : 1
          border.color: active ? Theme.accent : Theme.border

          Column {
            anchors.fill: parent
            anchors.margins: 10
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
            onClicked: root.applyPaperPreset(presetBox.modelData.key)
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
      value: root.grain
      valueText: (value * 100).toFixed(1) + "%"
      onMoved: function(val) {
        root.grain = val
        root.write("paperGrain", val)
      }
    }

    SettingToggle {
      title: "Full workspace paper background"
      description: "Extend the paper substrate across the whole workspace instead of only the document sheet."
      checked: root.fullWorkspace
      onToggled: function(value) {
        root.fullWorkspace = value
        root.write("paperOverWorkspace", value)
      }
    }
  }

  Rectangle {
    width: parent.width
    height: 1
    color: Theme.border
  }

  // ── Substrate ──
  Column {
    width: parent.width
    spacing: 12

    Text {
      text: "Substrate background"
      color: Theme.text
      font.pixelSize: 14
      font.bold: true
    }

    Text {
      width: parent.width
      text: "Choose the surface fused into the paper fibers: the plain theme colour, an ambient gradient or your own image."
      color: Theme.textMuted
      font.pixelSize: 12
      wrapMode: Text.Wrap
    }

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
          id: modeBox
          required property var modelData
          readonly property bool active: root.substrateMode === modelData.key

          width: (root.width - 24) / 3
          height: 70
          radius: 8
          color: active ? Theme.tint(Theme.accent, 0.16) : Theme.surfaceAlt
          border.width: active ? 2 : 1
          border.color: active ? Theme.accent : Theme.border

          Column {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 4

            Item {
              width: parent.width
              height: 18

              Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: modeBox.modelData.label
                color: modeBox.active ? Theme.accent : Theme.text
                font.pixelSize: 12
                font.bold: true
              }

              Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: modeBox.active
                text: "\u2713"
                color: Theme.accent
                font.pixelSize: 12
                font.bold: true
              }
            }

            Text {
              width: parent.width
              text: modeBox.modelData.desc
              color: Theme.textMuted
              font.pixelSize: 10
              wrapMode: Text.Wrap
            }
          }

          MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
              root.substrateMode = modeBox.modelData.key
              root.write("paperBgMode", modeBox.modelData.key)
            }
          }
        }
      }
    }
  }

  // Gradient controls
  Column {
    visible: root.substrateMode === "Gradient"
    width: parent.width
    spacing: 12

    Text {
      text: "Gradient Presets"
      color: Theme.text
      font.pixelSize: 13
      font.bold: true
    }

    Grid {
      columns: 3
      columnSpacing: 10
      rowSpacing: 10
      width: parent.width

      Repeater {
        model: [
          "Warm Parchment", "Sunset Glow", "Northern Sky",
          "Cyber Dusk", "Tokyo Neon", "Ethereal Mist"
        ]

        delegate: Rectangle {
          id: gradCard
          required property string modelData
          readonly property var preset: root.gradientPresets[modelData]
          readonly property bool active: root.gradientPreset === modelData

          width: (root.width - 20) / 3
          height: 48
          radius: 8
          border.width: active ? 2 : 1
          border.color: active ? Theme.accent : Theme.border

          gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: gradCard.preset ? gradCard.preset.c1 : "#fff" }
            GradientStop { position: 1.0; color: gradCard.preset ? gradCard.preset.c2 : "#000" }
          }

          Rectangle {
            anchors.fill: parent
            radius: 8
            color: Qt.rgba(0, 0, 0, 0.25)
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
            onClicked: root.applyGradientPreset(gradCard.modelData)
          }
        }
      }
    }

    SettingSlider {
      title: "Gradient blend opacity"
      description: "How strongly the gradient colours mix into the theme background."
      from: 0.05
      to: 0.90
      stepSize: 0.05
      value: root.gradientOpacity
      valueText: (value * 100).toFixed(0) + "%"
      onMoved: function(val) {
        root.gradientOpacity = val
        root.write("gradientOpacity", val)
      }
    }

    SettingSlider {
      title: "Gradient angle"
      description: "Direction of the gradient, in degrees."
      from: 0
      to: 360
      stepSize: 5
      value: root.gradientAngle
      valueText: value.toFixed(0) + "\u00b0"
      onMoved: function(val) {
        root.gradientAngle = val
        root.write("gradientAngle", val)
      }
    }
  }

  // Wallpaper controls
  Column {
    visible: root.substrateMode === "Wallpaper"
    width: parent.width
    spacing: 10

    Text {
      text: "Custom Wallpaper Substrate"
      color: Theme.text
      font.pixelSize: 13
      font.bold: true
    }

    Row {
      spacing: 12
      width: parent.width

      Button {
        text: "Choose image..."
        highlighted: true
        onClicked: wallpaperDialog.open()
      }

      Button {
        visible: root.wallpaperPath.length > 0
        text: "Clear"
        onClicked: {
          root.wallpaperPath = ""
          root.write("wallpaperPath", "")
        }
      }

      Text {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - 240
        text: root.wallpaperPath.length > 0
              ? root.wallpaperPath.split("/").pop()
              : "No wallpaper selected"
        color: Theme.textMuted
        font.pixelSize: 12
        elide: Text.ElideMiddle
      }
    }
  }

  // Shared legibility slider (the old "Text contrast & dimming" control).
  SettingSlider {
    visible: root.substrateMode !== "Solid"
    title: "Text contrast & dimming"
    description: "Blends the wallpaper or gradient toward the theme background so text blocks remain legible."
    from: 0.0
    to: 0.90
    stepSize: 0.05
    value: root.paperDim
    valueText: (value * 100).toFixed(0) + "%"
    onMoved: function(val) {
      root.paperDim = val
      root.write("paperDim", val)
    }
  }
}
