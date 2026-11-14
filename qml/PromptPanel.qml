import QtQuick
import QtQuick.Controls

Rectangle {
  id: root

  property bool expanded: true

  width: expanded ? Math.min(parent ? parent.width - 96 : 700, 700) : Math.min(parent ? parent.width - 96 : 200, 200)
  height: expanded ? 84 : 36
  color: Theme.surface
  border.width: 1
  border.color: Theme.border
  topLeftRadius: 8
  topRightRadius: 8
  bottomLeftRadius: 0
  bottomRightRadius: 0

  Behavior on height {
    NumberAnimation {
      duration: 180
      easing.type: Easing.OutCubic
    }
  }

  Button {
    id: catIcon

    icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_animal_cat_20_regular.svg"
    icon.width: 20
    icon.height: 20
    icon.color: Theme.accent
    width: 24
    height: 24
    padding: 0
    background: null

    anchors {
      left: parent.left
      leftMargin: 8
      verticalCenter: parent.verticalCenter
    }
  }

  Text {
    visible: !root.expanded

    anchors {
      left: catIcon.right
      leftMargin: 5
      verticalCenter: parent.verticalCenter
    }

    text: "Meow"
    color: Theme.textMuted
    font.pixelSize: 13
  }

  TextField {
    id: promptInput

    visible: root.expanded

    anchors {
      left: catIcon.right
      leftMargin: 10
      right: sendButton.left
      rightMargin: 8
      verticalCenter: parent.verticalCenter
    }

    height: 64
    font.pixelSize: 13
    color: Theme.text
    placeholderText: "Ask Templnote anything..."
    placeholderTextColor: Theme.textFaint
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText

    background: Rectangle {
      radius: 8
      color: Theme.surfaceAlt
      border.color: Theme.border
      border.width: 1
    }
  }

  Button {
    id: sendButton

    visible: root.expanded

    icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_send_16_regular.svg"
    icon.width: 20
    icon.height: 20
    icon.color: Theme.accent
    width: 24
    height: 24
    padding: 0
    background: null

    anchors {
      right: parent.right
      rightMargin: 10
      bottom: parent.bottom
      bottomMargin: 8
    }

    onClicked: {
      console.log("Send prompt")
    }
  }

  Button {
    id: toggleButton

    icon.source: root.expanded ? "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_chevron_double_down_20_filled.svg"
                               : "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_chevron_double_up_20_filled.svg"
    icon.width: 20
    icon.height: 20
    icon.color: Theme.textMuted
    width: 24
    height: 24
    padding: 0
    background: null

    anchors {
      top: parent.top
      right: parent.right
      topMargin: 8
      rightMargin: 10
    }

    onClicked: {
      root.expanded = !root.expanded
    }
  }
}
