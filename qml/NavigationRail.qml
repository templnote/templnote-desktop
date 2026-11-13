import QtQuick
import QtQuick.Controls

Rectangle {
  id: root

  width: 56
  color: "#fafafd"

  signal explorerToggleRequested()

  Column {
    anchors.top: parent.top
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.topMargin: 24
    spacing: 16
    Button {
      id: collapseButton

      text: root.explorerExpanded ? "‹" : "›"
      width: 40
      height: 40

      onClicked: {
        root.explorerToggleRequested()
      }
    }

    Button {
      text: "☰"
      width: 40
      height: 40
      onClicked: console.log("Navigation menu")
    }

    Button {
      text: "▣"
      width: 40
      height: 40
      onClicked: console.log("Explorer selected")
    }

    Button {
      text: "⚙"
      width: 40
      height: 40
      onClicked: console.log("Settings selected")
    }

    Button {
      text: "●"
      width: 40
      height: 40
      onClicked: console.log("Profile selected")
    }
  }
}
