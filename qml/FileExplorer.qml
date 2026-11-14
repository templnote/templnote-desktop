import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl

Rectangle {
  id: root

  property bool expanded: true

  width: expanded ? 240 : 0
  color: Theme.surface

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
      color: Theme.textFaint
      font.pixelSize: 12
      font.bold: true
      height: 24
      verticalAlignment: Text.AlignVCenter
    }

    Row {
      spacing: 4

      Repeater {
        model: [
          "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_document_folder_20_regular.svg",
          "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_arrow_sort_20_regular.svg",
          "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_search_20_regular.svg",
          "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_chat_sparkle_20_regular.svg"
        ]

        delegate: Button {
          id: actionButton

          required property string modelData

          icon.source: modelData
          icon.width: 20
          icon.height: 20
          icon.color: Theme.textMuted
          width: 32
          height: 32
          padding: 0

          background: Rectangle {
            radius: 8
            color: actionButton.hovered ? Theme.surfaceHover : "transparent"

            Behavior on color {
              ColorAnimation {
                duration: 150
                easing.type: Easing.OutCubic
              }
            }
          }
        }
      }
    }

    Button {
      id: newDocumentButton

      text: "New document"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_document_add_20_regular.svg"
      icon.width: 18
      icon.height: 18
      icon.color: Theme.textMuted
      width: parent.width
      height: 36
      spacing: 8
      leftPadding: 10
      rightPadding: 10
      topPadding: 0
      bottomPadding: 0

      background: Rectangle {
        radius: 8
        color: newDocumentButton.down
               ? Theme.surfaceHover
               : newDocumentButton.hovered ? Theme.tint(Theme.accent, 0.14) : Theme.surfaceAlt
        border.width: 1
        border.color: newDocumentButton.hovered ? Theme.accent : Theme.border
      }

      contentItem: IconLabel {
        spacing: newDocumentButton.spacing
        display: newDocumentButton.display
        icon: newDocumentButton.icon
        text: newDocumentButton.text
        font: newDocumentButton.font
        color: Theme.text
        alignment: Qt.AlignLeft | Qt.AlignVCenter
      }

      onClicked: console.log("New document")
    }

    Text {
      text: "Documents"
      color: Theme.textMuted
      font.pixelSize: 14
    }

    Text {
      text: "  Untitled document"
      color: Theme.textFaint
      font.pixelSize: 13
    }
  }
}
