import QtQuick

Column {
  id: root

  width: parent ? parent.width : 0
  spacing: 22

  property string density: "Comfortable"

  Text {
    text: "Layout"
    color: Theme.text
    font.pixelSize: 15
    font.bold: true
  }

  SettingToggle {
    title: "Show navigation rail"
    description: "Keep the icon rail visible on the left side of the window."
    checked: true
  }

  SettingToggle {
    title: "Expand the file explorer on launch"
    description: "Open the document explorer automatically when Templnote starts."
    checked: true
  }

  SettingChoice {
    title: "Density"
    description: "Control the amount of vertical space between interface elements."
    options: ["Compact", "Comfortable", "Spacious"]
    value: root.density
    onSelected: function(value) {
      root.density = value
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
    checked: false
  }

  SettingToggle {
    title: "Animate the file explorer"
    description: "Slide the explorer open and closed instead of snapping it."
    checked: true
  }
}
