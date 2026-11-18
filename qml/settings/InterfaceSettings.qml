import QtQuick

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
