import QtQuick
import QtQuick.Controls

Rectangle {
  id: root

  property bool expanded: true

  width: Math.min(parent ? parent.width - 96 : 600, 600)
  height: expanded ? 72 : 36
  color: "#e8e8f2"
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
    id: toggleButton

    text: root.expanded ? "⌄" : "⌃"

    anchors {
      top: parent.top
      right: parent.right
      topMargin: 8
      rightMargin: 12
    }

    onClicked: {
      root.expanded = !root.expanded
    }
  }

  Text {
    visible: root.expanded

    anchors {
      left: parent.left
      verticalCenter: parent.verticalCenter
      leftMargin: 20
    }

    text: "Prompt"
    // color: "#777A82"
    font.pixelSize: 13
  }

  Text {
    visible: !root.expanded

    anchors.centerIn: parent

    text: "⌃  Prompt"
    // color: "#777A82"
    font.pixelSize: 12
  }
}
