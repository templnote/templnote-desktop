import QtQuick
import QtQuick.Controls

// Centrally floating settings surface. It lives inside the main window so it
// can dim the editor and keep the app feeling like a single window.
Item {
  id: root

  anchors.fill: parent
  visible: false
  z: 1000
  focus: true

  property string activeSection: "appearance"

  readonly property var sections: [
    {
      key: "general",
      title: "General",
      description: "Startup, updates and application information",
      file: "settings/GeneralSettings.qml"
    },
    {
      key: "appearance",
      title: "Appearance",
      description: "Theme mode and colour scheme",
      file: "settings/AppearanceSettings.qml"
    },
    {
      key: "interface",
      title: "Interface",
      description: "Layout and paper aesthetic",
      file: "settings/InterfaceSettings.qml"
    },
    {
      key: "editor",
      title: "Editor",
      description: "Writing, formatting and editing behaviour",
      file: "settings/EditorSettings.qml"
    },
    {
      key: "plugins",
      title: "Plugins",
      description: "Extend Templnote with plugins",
      file: "settings/PluginsSettings.qml"
    },
    {
      key: "developer",
      title: "Developer",
      description: "Inspection, diagnostics and debug visualizers",
      file: "settings/DeveloperSettings.qml"
    }
  ]

  readonly property var currentSection: {
    for (let i = 0; i < sections.length; ++i) {
      if (sections[i].key === activeSection)
        return sections[i]
    }
    return sections[0]
  }

  function open(section) {
    if (section !== undefined)
      activeSection = section
    visible = true
    forceActiveFocus()
  }

  function close() {
    visible = false
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
    id: panel

    anchors.centerIn: parent
    width: Math.min(root.width - 80, 900)
    height: Math.min(root.height - 80, 580)
    radius: 8
    color: Theme.surface
    border.width: 1
    border.color: Theme.border
    clip: true

    MouseArea {
      anchors.fill: parent
    }

    Rectangle {
      id: sidebar

      anchors {
        top: parent.top
        topMargin: 1
        left: parent.left
        leftMargin: 1
        bottom: parent.bottom
        bottomMargin: 1
      }

      width: 207
      color: Theme.surfaceAlt
      topLeftRadius: 8
      bottomLeftRadius: 8

      Column {
        anchors {
          top: parent.top
          left: parent.left
          right: parent.right
          topMargin: 22
          leftMargin: 14
          rightMargin: 14
        }

        spacing: 4

        Column {
          width: parent.width
          topPadding: 6
          bottomPadding: 18
          leftPadding: 10

          spacing: 2

          Text {
            text: "Templnote"
            color: Theme.text
            font.pixelSize: 16
            font.bold: true
          }

          Text {
            text: "Settings"
            color: Theme.textFaint
            font.pixelSize: 12
          }
        }

        Repeater {
          model: root.sections

          delegate: Rectangle {
            id: sectionRow

            required property var modelData

            readonly property bool selected: root.activeSection === modelData.key

            width: parent.width
            height: 38
            radius: 8
            color: selected
                   ? Theme.tint(Theme.accent, 0.16)
                   : sectionMouse.containsMouse ? Theme.surfaceHover : "transparent"

            Rectangle {
              visible: sectionRow.selected

              anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
              }

              width: 3
              height: 18
              radius: 2
              color: Theme.accent
            }

            Text {
              anchors {
                left: parent.left
                leftMargin: 14
                right: parent.right
                rightMargin: 10
                verticalCenter: parent.verticalCenter
              }

              text: sectionRow.modelData.title
              color: sectionRow.selected ? Theme.accent : Theme.textMuted
              font.pixelSize: 13
              elide: Text.ElideRight
            }

            MouseArea {
              id: sectionMouse

              anchors.fill: parent
              hoverEnabled: true
              cursorShape: Qt.PointingHandCursor
              onClicked: root.activeSection = sectionRow.modelData.key
            }
          }
        }
      }

      Text {
        anchors {
          left: parent.left
          leftMargin: 24
          bottom: parent.bottom
          bottomMargin: 18
        }

        text: "v0.1.0"
        color: Theme.textFaint
        font.pixelSize: 11
      }
    }

    Item {
      id: content

      anchors {
        top: parent.top
        left: sidebar.right
        right: parent.right
        bottom: parent.bottom
      }

      Item {
        id: header

        anchors {
          top: parent.top
          left: parent.left
          right: parent.right
        }

        height: 74

        Column {
          anchors {
            left: parent.left
            leftMargin: 28
            right: closeButton.left
            rightMargin: 16
            verticalCenter: parent.verticalCenter
          }

          spacing: 3

          Text {
            width: parent.width
            text: root.currentSection.title
            color: Theme.text
            font.pixelSize: 20
            font.bold: true
            elide: Text.ElideRight
          }

          Text {
            width: parent.width
            text: root.currentSection.description
            color: Theme.textMuted
            font.pixelSize: 12
            elide: Text.ElideRight
          }
        }

        Rectangle {
          id: closeButton

          anchors {
            right: parent.right
            rightMargin: 20
            verticalCenter: parent.verticalCenter
          }

          width: 30
          height: 30
          radius: 8
          color: closeMouse.containsMouse ? Theme.surfaceHover : "transparent"

          Text {
            anchors.centerIn: parent
            text: "\u2715"
            color: Theme.textMuted
            font.pixelSize: 13
          }

          MouseArea {
            id: closeMouse

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.close()
          }
        }

        Rectangle {
          anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
          }

          height: 1
          color: Theme.border
        }
      }

      Flickable {
        id: scroller

        anchors {
          top: header.bottom
          left: parent.left
          right: parent.right
          bottom: parent.bottom
          leftMargin: 28
          rightMargin: 28
          topMargin: 24
          bottomMargin: 24
        }

        contentWidth: width
        contentHeight: pageLoader.item ? pageLoader.item.implicitHeight : 0
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Loader {
          id: pageLoader

          width: scroller.width
          source: root.currentSection.file
        }
      }
    }
  }
}
