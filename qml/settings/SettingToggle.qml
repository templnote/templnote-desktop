import QtQuick

// A settings row with a title, an optional description and a custom switch.
Item {
  id: root

  property string title: ""
  property string description: ""
  property bool checked: false

  signal toggled(bool value)

  width: parent ? parent.width : 0
  implicitHeight: Math.max(labels.implicitHeight, 24) + 20

  Column {
    id: labels

    anchors {
      left: parent.left
      right: toggle.left
      rightMargin: 20
      verticalCenter: parent.verticalCenter
    }

    spacing: 3

    Text {
      width: parent.width
      text: root.title
      color: Theme.text
      font.pixelSize: 14
      wrapMode: Text.Wrap
    }

    Text {
      visible: root.description.length > 0
      width: parent.width
      text: root.description
      color: Theme.textMuted
      font.pixelSize: 12
      wrapMode: Text.Wrap
    }
  }

  Rectangle {
    id: toggle

    anchors {
      right: parent.right
      verticalCenter: parent.verticalCenter
    }

    width: 42
    height: 24
    radius: 12
    color: root.checked ? Theme.accent : Theme.surfaceHover
    border.width: root.checked ? 0 : 1
    border.color: Theme.border

    Rectangle {
      width: 18
      height: 18
      radius: 9
      y: 3
      x: root.checked ? parent.width - width - 3 : 3
      color: root.checked ? Theme.accentText : Theme.textMuted

      Behavior on x {
        NumberAnimation {
          duration: 140
          easing.type: Easing.OutCubic
        }
      }
    }

    MouseArea {
      anchors.fill: parent
      cursorShape: Qt.PointingHandCursor
      onClicked: {
        root.checked = !root.checked
        root.toggled(root.checked)
      }
    }
  }
}
