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

  Accessible.name: root.title
  Accessible.description: root.description

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
        id: optionChoice

        required property string modelData

        readonly property bool active: root.value === modelData

        activeFocusOnTab: true

        Accessible.name: modelData
        Accessible.description: root.title
        Accessible.role: Accessible.RadioButton
        Accessible.checked: optionChoice.active
        Accessible.selected: optionChoice.active
        Accessible.focusable: true
        // RadioButton exposes a single press action; keeping onToggleAction
        // as well double-fires select() on assistive technologies that emit
        // both handlers for one gesture.
        Accessible.onPressAction: optionChoice.select()

        function select() {
          // value is bound to a preference in the page; emit the choice and
          // let the page persist it instead of breaking that binding.
          root.selected(modelData)
        }

        Keys.onReturnPressed: function(event) {
          event.accepted = true
          optionChoice.select()
        }
        Keys.onEnterPressed: function(event) {
          event.accepted = true
          optionChoice.select()
        }
        Keys.onSpacePressed: function(event) {
          event.accepted = true
          optionChoice.select()
        }

        width: Math.max(58, optionLabel.implicitWidth + 20)
        height: 30
        radius: 8
        color: active || optionChoice.activeFocus ? Theme.tint(Theme.accent, 0.18) : "transparent"
        border.width: optionChoice.activeFocus ? 2 : 1
        border.color: active || optionChoice.activeFocus ? Theme.accent : Theme.border

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
          onClicked: optionChoice.select()
        }
      }
    }
  }
}
