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

    icon.source: root.expanded ? "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_chevron_double_down_20_filled.svg"
                               : "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_chevron_double_up_20_filled.svg"
    icon.width: 20
    icon.height: 20
    width: 24
    height: 24
    padding: 0
    background: null

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

    text: "Meow"
    // color: "#777A82"
    font.pixelSize: 13
  }

  Text {
    visible: !root.expanded

    anchors.centerIn: parent

    text: "Prompt"
    // color: "#777A82"
    font.pixelSize: 12
  }
}
