import QtQuick
import QtQuick.Controls

Rectangle {
  id: root

  property bool expanded: true

  width: expanded ? 240 : 0
  color: "#e8e8f2"

  Behavior on width {
    NumberAnimation {
      duration: 180
      easing.type: Easing.OutCubic
    }
  }

  Column {
    visible: root.expanded

    anchors {
      top: parent.top
      left: parent.left
      right: parent.right
      topMargin: 24
      leftMargin: 20
      rightMargin: 16
    }

    spacing: 16

    Text {
      text: "EXPLORER"
      // color: "#8B8D94"
      font.pixelSize: 12
      font.bold: true
    }

    Button {
      text: "＋  New document"
      width: parent.width
      onClicked: console.log("New document")
    }

    Text {
      text: "Documents"
      // color: "#D5D7DB"
      font.pixelSize: 14
    }

    Text {
      text: "  Untitled document"
      // color: "#A5A7AD"
      font.pixelSize: 13
    }
  }
}
