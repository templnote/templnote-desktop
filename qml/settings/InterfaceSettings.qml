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
    title: "Expand the file explorer on launch"
    description: "Open the document explorer automatically when Templnote starts."
    checked: Preferences.expandExplorerOnLaunch
    onToggled: function(value) {
      Preferences.expandExplorerOnLaunch = value
    }
  }

  SettingSlider {
    title: "Columns per layout"
    description: "Number of columns created when you insert a column layout."
    from: 2
    to: 5
    stepSize: 1
    value: Preferences.columnCount
    valueText: value.toFixed(0) + " columns"
    onMoved: function(val) {
      Preferences.columnCount = val
    }
  }
}
