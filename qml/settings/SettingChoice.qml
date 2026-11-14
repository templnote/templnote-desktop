import QtQuick

// A settings row with a title, an optional description and a segmented choice.
Item {
  id: root

  property string title: ""
  property string description: ""
  property var options: []
  property string value: ""

  signal selected(string value)

  width: parent ? parent.width : 0
  implicitHeight: Math.max(labels.implicitHeight, controls.implicitHeight) + 20

  Column {
    id: labels

    anchors {
      left: parent.left
      right: controls.left
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

  Row {
    id: controls

    anchors {
      right: parent.right
      verticalCenter: parent.verticalCenter
    }

    spacing: 6

    Repeater {
      model: root.options

      delegate: Rectangle {
        required property string modelData

        readonly property bool active: root.value === modelData

        width: Math.max(58, optionLabel.implicitWidth + 20)
        height: 30
        radius: 8
        color: active ? Theme.tint(Theme.accent, 0.18) : "transparent"
        border.width: 1
        border.color: active ? Theme.accent : Theme.border

        Text {
          id: optionLabel

          anchors.centerIn: parent
          text: modelData
          color: parent.active ? Theme.accent : Theme.textMuted
          font.pixelSize: 12
        }

        MouseArea {
          anchors.fill: parent
          cursorShape: Qt.PointingHandCursor
          onClicked: {
            root.value = modelData
            root.selected(modelData)
          }
        }
      }
    }
  }
}
