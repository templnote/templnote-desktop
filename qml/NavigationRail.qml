import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl

Rectangle {
  id: root

  property bool explorerExpanded: true

  width: 56
  color: Theme.surface

  signal explorerToggleRequested()
  signal homeRequested()
  signal settingsRequested()

  component RailButton: Rectangle {
    id: buttonRoot

    property alias iconSource: iconImage.source
    property string tooltipText: ""
    property bool active: false
    // Reserved controls stay visible but do not react to input yet.
    property bool placeholder: false
    signal clicked()

    width: 28
    height: 28
    radius: 8
    color: !placeholder && (buttonMouse.containsMouse || buttonRoot.active)
           ? Theme.surfaceHover
           : "transparent"

    Behavior on color {
      ColorAnimation {
        duration: Preferences.reduceMotion ? 0 : 150
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
      color: placeholder ? Theme.textFaint
             : buttonRoot.active ? Theme.accent : Theme.textMuted
    }

    MouseArea {
      id: buttonMouse
      anchors.fill: parent
      hoverEnabled: !buttonRoot.placeholder
      cursorShape: buttonRoot.placeholder ? Qt.ArrowCursor : Qt.PointingHandCursor
      onClicked: {
        if (!buttonRoot.placeholder)
          buttonRoot.clicked()
      }
    }

    ToolTip.visible: buttonMouse.containsMouse && tooltipText.length > 0
    ToolTip.text: buttonRoot.placeholder ? tooltipText + " (coming soon)" : tooltipText
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
      iconSource: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_home_heart_20_regular.svg"
      tooltipText: "Home"
      onClicked: root.homeRequested()
    }

    RailButton {
      iconSource: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_folder_20_regular.svg"
      tooltipText: "Toggle explorer"
      onClicked: root.explorerToggleRequested()
    }

    RailButton {
      iconSource: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_settings_20_regular.svg"
      tooltipText: "Settings"
      onClicked: root.settingsRequested()
    }

    RailButton {
      iconSource: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_person_20_regular.svg"
      tooltipText: "Profile"
      placeholder: true
    }
  }
}
