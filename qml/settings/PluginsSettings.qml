import QtQuick

Column {
  id: root

  width: parent ? parent.width : 0
  spacing: 20

  property string searchText: ""

  Text {
    text: "Installed plugins"
    color: Theme.text
    font.pixelSize: 15
    font.bold: true
  }

  Rectangle {
    width: parent.width
    height: 38
    radius: 8
    color: Theme.surfaceAlt
    border.width: 1
    border.color: searchInput.activeFocus ? Theme.accent : Theme.border

    TextInput {
      id: searchInput

      anchors {
        fill: parent
        leftMargin: 12
        rightMargin: 12
      }

      verticalAlignment: TextInput.AlignVCenter
      color: Theme.text
      selectionColor: Theme.accent
      selectedTextColor: Theme.accentText
      font.pixelSize: 13
      clip: true

      onTextChanged: root.searchText = text

      Text {
        anchors.fill: parent
        verticalAlignment: Text.AlignVCenter
        visible: !searchInput.text.length
        text: "Search plugins"
        color: Theme.textFaint
        font.pixelSize: 13
      }
    }
  }

  Rectangle {
    width: parent.width
    height: 168
    radius: 8
    color: Theme.surfaceAlt
    border.width: 1
    border.color: Theme.border

    Column {
      anchors.centerIn: parent
      width: parent.width - 48
      spacing: 8

      Text {
        width: parent.width
        text: root.searchText.length > 0 ? "No matching plugins" : "No plugins installed yet"
        color: Theme.text
        font.pixelSize: 15
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
      }

      Text {
        width: parent.width
        text: root.searchText.length > 0
              ? "Try a different search term."
              : "Plugins can add commands, integrations and new block types to Templnote."
        color: Theme.textMuted
        font.pixelSize: 12
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
      }

      Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        width: 132
        height: 34
        radius: 8
        color: browseMouse.containsMouse ? Theme.accent : Theme.tint(Theme.accent, 0.18)
        border.width: 1
        border.color: Theme.accent

        Text {
          anchors.centerIn: parent
          text: "Browse plugins"
          color: browseMouse.containsMouse ? Theme.accentText : Theme.accent
          font.pixelSize: 12
          font.bold: true
        }

        MouseArea {
          id: browseMouse

          anchors.fill: parent
          hoverEnabled: true
          cursorShape: Qt.PointingHandCursor
        }
      }
    }
  }
}
