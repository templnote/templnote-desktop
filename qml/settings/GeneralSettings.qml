import QtQuick

Column {
  id: root

  width: parent ? parent.width : 0
  spacing: 22

  Text {
    text: "Startup"
    color: Theme.text
    font.pixelSize: 15
    font.bold: true
  }

  SettingToggle {
    title: "Restore last session"
    description: "Reopen the documents that were open when Templnote was closed."
    checked: Preferences.restoreLastSession
    onToggled: function(value) {
      Preferences.restoreLastSession = value
    }
  }

  Rectangle {
    width: parent.width
    height: 1
    color: Theme.border
  }

  Text {
    text: "About"
    color: Theme.text
    font.pixelSize: 15
    font.bold: true
  }

  Rectangle {
    width: parent.width
    height: 92
    radius: 8
    color: Theme.surfaceAlt
    border.width: 1
    border.color: Theme.border

    Column {
      anchors {
        left: parent.left
        leftMargin: 16
        verticalCenter: parent.verticalCenter
      }

      spacing: 4

      Text {
        text: "Templnote"
        color: Theme.text
        font.pixelSize: 16
        font.bold: true
      }

      Text {
        text: "Local first AI-native text editor"
        color: Theme.textMuted
        font.pixelSize: 12
      }

      Text {
        text: "Version 0.3.4"
        color: Theme.textFaint
        font.pixelSize: 11
      }
    }
  }
}
