import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl

Rectangle {
  id: root

  property bool explorerExpanded: true

  width: 56
  color: Theme.surface

  signal explorerToggleRequested()
  signal settingsRequested()

  component RailButton: Rectangle {
    id: buttonRoot

    property alias iconSource: iconImage.source
    property string tooltipText: ""
    property bool active: false
    signal clicked()

    width: 28
    height: 28
    radius: 8
    color: buttonMouse.containsMouse || buttonRoot.active
           ? Theme.surfaceHover
           : "transparent"

    Behavior on color {
      ColorAnimation {
        duration: 150
        easing.type: Easing.OutQuad
      }
    }

    IconImage {
      id: iconImage
      anchors.centerIn: parent
      width: 20
      height: 20
      sourceSize.width: 20
      sourceSize.height: 20
      color: buttonRoot.active ? Theme.accent : Theme.textMuted
    }

    MouseArea {
      id: buttonMouse
      anchors.fill: parent
      hoverEnabled: true
      cursorShape: Qt.PointingHandCursor
      onClicked: buttonRoot.clicked()
    }

    ToolTip.visible: buttonMouse.containsMouse && tooltipText.length > 0
    ToolTip.text: tooltipText
    ToolTip.delay: 500
  }

  Column {
    anchors.top: parent.top
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.topMargin: 10
    spacing: 1

    RailButton {
      id: collapseButton
      iconSource: root.explorerExpanded ? "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_panel_left_20_filled.svg"
                                        : "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_panel_left_expand_20_filled.svg"
      tooltipText: root.explorerExpanded ? "Collapse sidebar" : "Expand sidebar"
      onClicked: root.explorerToggleRequested()
    }

    RailButton {
      iconSource: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_align_space_evenly_horizontal_20_regular.svg"
      tooltipText: "Navigation"
      onClicked: console.log("Navigation menu")
    }

    RailButton {
      iconSource: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_folder_20_regular.svg"
      tooltipText: "Files"
      onClicked: console.log("Explorer selected")
    }

    RailButton {
      iconSource: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_settings_20_regular.svg"
      tooltipText: "Settings"
      onClicked: root.settingsRequested()
    }

    RailButton {
      iconSource: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_person_20_regular.svg"
      tooltipText: "Profile"
      onClicked: console.log("Profile selected")
    }
  }
}
