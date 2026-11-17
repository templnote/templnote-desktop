import QtQuick

// Floating reference sheet for the application's keyboard shortcuts.
// Opened with F1 or from the options menu, dismissed with Escape or a click
// on the backdrop. Styling mirrors the settings surface.
Item {
  id: root

  anchors.fill: parent
  visible: false
  z: 1001
  focus: true

  readonly property var groups: [
    {
      title: "File",
      items: [
        { label: "Save document", keys: "Ctrl+S" }
      ]
    },
    {
      title: "Editing",
      items: [
        { label: "Undo", keys: "Ctrl+Z" },
        { label: "Redo", keys: "Ctrl+Shift+Z" },
        { label: "Move block up", keys: "Alt+↑" },
        { label: "Move block down", keys: "Alt+↓" }
      ]
    },
    {
      title: "Formatting",
      items: [
        { label: "Bold", keys: "Ctrl+B" },
        { label: "Italic", keys: "Ctrl+I" },
        { label: "Underline", keys: "Ctrl+U" },
        { label: "Strikethrough", keys: "Ctrl+Shift+X" },
        { label: "Subscript", keys: "Ctrl+," },
        { label: "Superscript", keys: "Ctrl+." }
      ]
    },
    {
      title: "Search",
      items: [
        { label: "Find in document", keys: "Ctrl+/" },
        { label: "Search all documents", keys: "Ctrl+Shift+/" }
      ]
    },
    {
      title: "Workspace",
      items: [
        { label: "Create folder", keys: "Ctrl+Shift+N" },
        { label: "Keyboard shortcuts", keys: "F1" },
        { label: "Close a dialog", keys: "Esc" }
      ]
    }
  ]

  function open() {
    visible = true
    forceActiveFocus()
  }

  function close() {
    visible = false
  }

  function toggle() {
    if (visible)
      close()
    else
      open()
  }

  Shortcut {
    sequence: "Escape"
    enabled: root.visible
    onActivated: root.close()
  }

  Rectangle {
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0.52)

    MouseArea {
      anchors.fill: parent
      onClicked: root.close()
    }
  }

  Rectangle {
    id: card

    anchors.centerIn: parent
    width: Math.min(root.width - 80, 520)
    height: Math.min(root.height - 80, 500)
    radius: 8
    color: Theme.surface
    border.width: 1
    border.color: Theme.border
    clip: true

    MouseArea {
      anchors.fill: parent
    }

    Column {
      anchors {
        fill: parent
        margins: 24
      }

      spacing: 16

      Text {
        text: "Keyboard shortcuts"
        color: Theme.text
        font.pixelSize: 16
        font.bold: true
      }

      Flickable {
        width: parent.width
        height: parent.height - 40
        contentHeight: shortcutList.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
          id: shortcutList

          width: parent.width
          spacing: 18

          Repeater {
            model: root.groups

            delegate: Column {
              required property var modelData

              width: shortcutList.width
              spacing: 8

              Text {
                text: modelData.title
                color: Theme.textMuted
                font.pixelSize: 12
                font.bold: true
              }

              Repeater {
                model: modelData.items

                delegate: Item {
                  required property var modelData

                  width: shortcutList.width
                  height: 28

                  Text {
                    anchors {
                      left: parent.left
                      right: keys.left
                      rightMargin: 12
                      verticalCenter: parent.verticalCenter
                    }

                    text: modelData.label
                    color: Theme.text
                    font.pixelSize: 13
                    elide: Text.ElideRight
                  }

                  Rectangle {
                    id: keys

                    anchors {
                      right: parent.right
                      verticalCenter: parent.verticalCenter
                    }

                    width: keysLabel.implicitWidth + 14
                    height: 22
                    radius: 6
                    color: Theme.surfaceAlt
                    border.width: 1
                    border.color: Theme.border

                    Text {
                      id: keysLabel

                      anchors.centerIn: parent
                      text: modelData.keys
                      color: Theme.textMuted
                      font.pixelSize: 11
                      font.family: "monospace"
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
