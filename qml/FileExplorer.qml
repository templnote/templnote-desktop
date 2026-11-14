import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl

Rectangle {
  id: root

  property bool expanded: true

  // Set by Main.qml.
  property var documentModel
  property var documentList

  // Item that is currently being renamed inline after creation or rename click.
  property string editingEntryPath: ""

  // Deletion confirmation requested signal to display centered floating dialog at window level.
  signal deleteRequested(string path, string name, bool isFolder)
  signal newDocumentCreated()

  function confirmDelete(path, name, isFolder) {
    root.deleteRequested(path, name, isFolder)
  }

  function createDocument() {
    if (!root.documentModel || !root.documentList)
      return

    const path = root.documentList.createUntitledPath()

    if (path.length === 0)
      return

    if (root.documentModel.createDocument(path)) {
      root.documentList.currentFilePath = path
      root.documentList.refresh()
      root.newDocumentCreated()
    }
  }

  function createFolder() {
    if (!root.documentList)
      return

    const path = root.documentList.createUntitledFolder()

    if (path.length === 0)
      return

    root.editingEntryPath = path

    // Scroll to the newly created folder so the user sees the inline rename field
    Qt.callLater(function() {
      for (let i = 0; i < root.documentList.count; ++i) {
        if (root.documentList.filePathAt(i) === path) {
          documentView.positionViewAtIndex(i, ListView.Contain)
          break
        }
      }
    })
  }

  function startRename(path) {
    root.editingEntryPath = path
  }

  function commitRename(path, name, isFolder) {
    if (!root.documentList)
      return

    if (isFolder) {
      if (root.documentList.renameFolder(path, name))
        root.editingEntryPath = ""
    } else {
      const newPath = root.documentList.renameDocument(path, name)
      if (newPath.length > 0) {
        // If current document in editor was renamed, update it
        if (root.documentModel && root.documentModel.currentFilePath === path) {
          root.documentModel.loadDocument(newPath)
        }
        root.editingEntryPath = ""
      }
    }
  }

  Shortcut {
    sequence: "Ctrl+Shift+N"
    enabled: root.expanded
    onActivated: root.createFolder()
  }

  width: expanded ? 240 : 0
  color: Theme.surface
  clip: true

  Behavior on width {
    NumberAnimation {
      duration: 180
      easing.type: Easing.OutCubic
    }
  }

  Item {
    anchors.fill: parent
    visible: root.expanded

    Column {
      id: header

      anchors {
        top: parent.top
        left: parent.left
        right: parent.right
        topMargin: 24
        leftMargin: 20
        rightMargin: 16
      }

      spacing: 12

      Text {
        text: "EXPLORER"
        color: Theme.textFaint
        font.pixelSize: 12
        font.bold: true
        height: 24
        verticalAlignment: Text.AlignVCenter
      }

      Row {
        id: actionRow

        width: parent.width
        spacing: 4

        Repeater {
          id: actionRepeater

          model: [
            {
              icon: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_folder_add_20_regular.svg",
              action: "newFolder"
            },
            {
              icon: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_document_add_20_regular.svg",
              action: "newDocument"
            },
            {
              icon: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_arrow_sort_20_regular.svg",
              action: ""
            },
            {
              icon: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_search_20_regular.svg",
              action: ""
            },
          ]

          delegate: Button {
            id: actionButton

            required property var modelData

            icon.source: modelData.icon
            icon.width: 20
            icon.height: 20
            icon.color: Theme.textMuted
            width: (actionRow.width - actionRow.spacing * (actionRepeater.count - 1)) / actionRepeater.count
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

            onClicked: {
              if (modelData.action === "newFolder")
                root.createFolder()
              else if (modelData.action === "newDocument")
                root.createDocument()
            }
          }
        }
      }

      Flickable {
        id: breadcrumbRow

        width: parent.width
        height: 24
        contentWidth: breadcrumbContent.width
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick
        interactive: contentWidth > width
        visible: root.documentList !== null && root.documentList !== undefined

        Row {
          id: breadcrumbContent

          height: parent.height
          spacing: 2

        Button {
          id: upButton

          anchors.verticalCenter: parent.verticalCenter
          width: 24
          height: 24
          padding: 0
          enabled: root.documentList && root.documentList.relativeDirectory.length > 0
          icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_chevron_left_20_regular.svg"
          icon.width: 14
          icon.height: 14
          icon.color: upButton.enabled ? Theme.textMuted : Theme.textFaint

          background: Rectangle {
            radius: 6
            color: upButton.hovered && upButton.enabled ? Theme.surfaceHover : "transparent"
          }

          onClicked: {
            if (root.documentList)
              root.documentList.goUp()
          }
        }

        Repeater {
          id: breadcrumbRepeater

          model: root.documentList ? root.documentList.breadcrumbs : []

          delegate: Row {
            required property var modelData

            height: 24
            spacing: 2

            Text {
              anchors.verticalCenter: parent.verticalCenter
              width: Math.min(implicitWidth, 90)
              text: modelData.name
              color: crumbHover.containsMouse && index < breadcrumbRepeater.count - 1
                     ? Theme.accent
                     : index === breadcrumbRepeater.count - 1 ? Theme.text : Theme.textMuted
              font.pixelSize: 12
              elide: Text.ElideMiddle

              MouseArea {
                id: crumbHover

                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                  if (root.documentList)
                    root.documentList.navigateTo(modelData.path)
                }
              }
            }

            Text {
              anchors.verticalCenter: parent.verticalCenter
              visible: index < breadcrumbRepeater.count - 1
              text: "/"
              color: Theme.textFaint
              font.pixelSize: 12
            }
          }
        }
        }
      }

      Text {
        text: "Documents"
        color: Theme.textMuted
        font.pixelSize: 14
        font.bold: true
      }
    }

    Text {
      id: emptyState

      anchors {
        top: header.bottom
        left: parent.left
        right: parent.right
        topMargin: 12
        leftMargin: 20
        rightMargin: 16
      }

      visible: !root.documentList || root.documentList.count === 0
      text: "This folder is empty"
      color: Theme.textFaint
      font.pixelSize: 13
      wrapMode: Text.WordWrap
    }

    ListView {
      id: documentView

      anchors {
        top: header.bottom
        left: parent.left
        right: parent.right
        bottom: parent.bottom
        topMargin: 12
        leftMargin: 12
        rightMargin: 12
        bottomMargin: 16
      }

      model: root.documentList
      spacing: 2
      clip: true
      boundsBehavior: Flickable.StopAtBounds

      delegate: Item {
        id: entry

        required property string name
        required property string filePath
        required property bool isFolder
        required property bool isCurrent

        property bool renamingEntry: root.editingEntryPath === entry.filePath
        property string draftName: entry.name

        width: documentView.width
        height: 34

        onRenamingEntryChanged: {
          if (entry.renamingEntry) {
            entry.draftName = entry.name
            Qt.callLater(function() {
              nameEditField.forceActiveFocus()
              nameEditField.selectAll()
            })
          }
        }

        Rectangle {
          anchors.fill: parent
          radius: 8
          color: entry.isCurrent
                 ? Theme.tint(Theme.accent, 0.16)
                 : entryMouse.containsMouse ? Theme.surfaceHover : "transparent"

          Behavior on color {
            ColorAnimation {
              duration: 120
              easing.type: Easing.OutCubic
            }
          }
        }

        Item {
          anchors.fill: parent
          visible: !entry.renamingEntry

          MouseArea {
            id: entryMouse

            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.PointingHandCursor

            onClicked: function(mouse) {
              if (!root.documentList)
                return

              if (mouse.button === Qt.RightButton) {
                itemContextMenu.entryPath = entry.filePath
                itemContextMenu.entryName = entry.name
                itemContextMenu.entryIsFolder = entry.isFolder
                itemContextMenu.popup()
                return
              }

              if (entry.isFolder) {
                root.documentList.enterFolder(entry.filePath)
                return
              }

              if (root.documentModel &&
                  root.documentModel.loadDocument(entry.filePath)) {
                root.documentList.currentFilePath = entry.filePath
              }
            }
          }

          Row {
            anchors {
              left: parent.left
              right: itemActions.left
              verticalCenter: parent.verticalCenter
              leftMargin: 10
              rightMargin: 4
            }

            spacing: 8

            IconImage {
              anchors.verticalCenter: parent.verticalCenter
              width: 16
              height: 16
              source: entry.isFolder
                      ? "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_folder_20_regular.svg"
                      : "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_document_20_regular.svg"
              sourceSize.width: 16
              sourceSize.height: 16
              color: entry.isCurrent ? Theme.accent : Theme.textMuted
            }

            Text {
              anchors.verticalCenter: parent.verticalCenter
              width: parent.width - 24
              text: entry.name
              color: entry.isCurrent ? Theme.text : Theme.textMuted
              font.pixelSize: 13
              elide: Text.ElideRight
            }
          }

          Row {
            id: itemActions
            anchors {
              right: parent.right
              rightMargin: 8
              verticalCenter: parent.verticalCenter
            }
            visible: entryMouse.containsMouse || renameMouse.containsMouse || deleteMouse.containsMouse
            spacing: 4

            Rectangle {
              id: renameBtn
              width: 22
              height: 22
              radius: 4
              color: renameMouse.containsMouse ? Theme.surfaceHover : "transparent"

              IconImage {
                anchors.centerIn: parent
                width: 14
                height: 14
                source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_edit_20_regular.svg"
                sourceSize.width: 14
                sourceSize.height: 14
                color: renameMouse.containsMouse ? Theme.accent : Theme.textMuted
              }

              MouseArea {
                id: renameMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: function(mouse) {
                  mouse.accepted = true
                  root.startRename(entry.filePath)
                }
              }
            }

            Rectangle {
              id: deleteBtn
              width: 22
              height: 22
              radius: 4
              color: deleteMouse.containsMouse ? Theme.surfaceHover : "transparent"

              IconImage {
                anchors.centerIn: parent
                width: 14
                height: 14
                source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_delete_20_regular.svg"
                sourceSize.width: 14
                sourceSize.height: 14
                color: deleteMouse.containsMouse ? "#ef4444" : Theme.textMuted
              }

              MouseArea {
                id: deleteMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: function(mouse) {
                  mouse.accepted = true
                  root.confirmDelete(entry.filePath, entry.name, entry.isFolder)
                }
              }
            }
          }
        }

        Row {
          anchors {
            fill: parent
            leftMargin: 10
            rightMargin: 10
          }
          visible: entry.renamingEntry
          spacing: 8

          IconImage {
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            source: entry.isFolder
                    ? "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_folder_20_regular.svg"
                    : "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_document_20_regular.svg"
            sourceSize.width: 16
            sourceSize.height: 16
            color: Theme.accent
          }

          TextField {
            id: nameEditField

            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 24
            text: entry.draftName
            color: Theme.text
            font.pixelSize: 13
            padding: 0
            selectByMouse: true

            background: Rectangle {
              color: "transparent"

              Rectangle {
                anchors {
                  left: parent.left
                  right: parent.right
                  bottom: parent.bottom
                }

                height: 1
                color: Theme.accent
              }
            }

            onTextEdited: entry.draftName = text
            onAccepted: root.commitRename(entry.filePath, entry.draftName, entry.isFolder)

            Keys.onEscapePressed: function(event) {
              root.editingEntryPath = ""
              event.accepted = true
            }

            onActiveFocusChanged: {
              if (!activeFocus && entry.renamingEntry)
                root.commitRename(entry.filePath, entry.draftName, entry.isFolder)
            }
          }
        }
      }
    }
  }

  Menu {
    id: itemContextMenu

    property string entryPath: ""
    property string entryName: ""
    property bool entryIsFolder: false

    MenuItem {
      text: "Rename"
      onTriggered: root.startRename(itemContextMenu.entryPath)
    }

    MenuItem {
      text: "Delete"
      onTriggered: root.confirmDelete(itemContextMenu.entryPath, itemContextMenu.entryName, itemContextMenu.entryIsFolder)
    }
  }
}
