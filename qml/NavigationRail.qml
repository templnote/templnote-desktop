import QtQuick
import QtQuick.Controls

Rectangle {
  id: root

  width: 56
  color: Theme.surface

  signal explorerToggleRequested()
  signal settingsRequested()

  Column {
    anchors.top: parent.top
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.topMargin: 24
    spacing: 16

    Button {
      id: collapseButton

      icon.source: root.explorerExpanded ? "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_panel_left_20_filled.svg"
                                         : "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_panel_left_expand_20_filled.svg"
      icon.width: 20
      icon.height: 20
      icon.color: Theme.textMuted
      width: 24
      height: 24
      padding: 0
      background: null

      onClicked: {
        root.explorerToggleRequested()
      }
    }

    Button {
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_align_space_evenly_horizontal_20_regular.svg"
      icon.width: 20
      icon.height: 20
      icon.color: Theme.textMuted
      width: 24
      height: 24
      padding: 0
      background: null
      onClicked: console.log("Navigation menu")
    }

    Button {
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_folder_20_regular.svg"
      icon.width: 20
      icon.height: 20
      icon.color: Theme.textMuted
      width: 24
      height: 24
      padding: 0
      background: null
      onClicked: console.log("Explorer selected")
    }

    Button {
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_settings_20_regular.svg"
      icon.width: 20
      icon.height: 20
      icon.color: Theme.textMuted
      width: 24
      height: 24
      padding: 0
      background: null
      onClicked: root.settingsRequested()
    }

    Button {
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_person_20_regular.svg"
      icon.width: 20
      icon.height: 20
      icon.color: Theme.textMuted
      width: 24
      height: 24
      padding: 0
      background: null
      onClicked: console.log("Profile selected")
    }
  }
}
