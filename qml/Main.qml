import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Templnote

ApplicationWindow {
  id: window

  width: 1100
  height: 760
  minimumWidth: 900

  visible: true
  title: "Templnote — " + blockModel.documentTitle + (blockModel.isModified ? " •" : "")
  color: Theme.background

  BlockModel {
    id: blockModel
  }

  DocumentListModel {
    id: documentList
  }

  SearchIndex {
    id: searchIndex

    blockModel: blockModel
    documentList: documentList
  }

  SearchResultsModel {
    id: searchResults

    index: searchIndex
  }

  Connections {
    target: blockModel

    function onCurrentFilePathChanged() {
      documentList.currentFilePath = blockModel.currentFilePath
      documentList.refresh()
    }

    function onDocumentLoaded() {
      documentList.currentFilePath = blockModel.currentFilePath
      documentList.refresh()
    }
  }

  Shortcut {
    sequence: StandardKey.Save
    context: Qt.ApplicationShortcut
    onActivated: {
      if (blockModel.currentFilePath.length > 0) {
        if (blockModel.saveCurrentDocument())
          documentList.refresh()
        return
      }

      const path = documentList.createUntitledPath()

      if (path.length > 0 && blockModel.createDocument(path)) {
        documentList.currentFilePath = path
        documentList.refresh()
        Qt.callLater(function() {
          editor.focusFirstBlock()
        })
      }
    }
  }

  Shortcut {
    sequence: StandardKey.Undo
    context: Qt.ApplicationShortcut
    onActivated: {
      editor.undo()
    }
  }
  Shortcut {
    sequence: StandardKey.Redo
    context: Qt.ApplicationShortcut
    onActivated: {
      editor.redo()
    }
  }

  Shortcut {
    sequence: "Ctrl+/"
    context: Qt.ApplicationShortcut
    enabled: !searchWindow.visible
    onActivated: searchBar.open()
  }

  Shortcut {
    // Shift+/ is reported as '?' on some keyboard layouts, so bind both
    // spellings of the same physical shortcut.
    sequences: ["Ctrl+Shift+/", "Ctrl+Shift+?"]
    context: Qt.ApplicationShortcut
    onActivated: {
      if (searchBar.visible)
        searchBar.close(false)

      searchWindow.open()
    }
  }

  NavigationRail {
    property bool explorerExpanded: true
    id: navigationRail

    anchors {
      top: parent.top
      left: parent.left
      bottom: parent.bottom
    }
    onExplorerToggleRequested: {
      fileExplorer.expanded = !fileExplorer.expanded
      navigationRail.explorerExpanded = fileExplorer.expanded
    }
    onSettingsRequested: settingsWindow.open()
  }

  FileExplorer {
    id: fileExplorer

    documentModel: blockModel
    documentList: documentList

    anchors {
      top: parent.top
      left: navigationRail.right
      bottom: parent.bottom
    }

    onDeleteRequested: function(path, name, isFolder) {
      deleteConfirmationModal.open(path, name, isFolder)
    }

    onNewDocumentCreated: {
      Qt.callLater(function() {
        editor.focusFirstBlock()
      })
    }
  }

  Item {
    id: workspace

    property real contentWidth: Math.min(width - 64, 820)

    anchors {
      top: parent.top
      left: fileExplorer.right
      right: parent.right
      bottom: parent.bottom
    }

    Rectangle {
      anchors.fill: parent
      color: Theme.background
    }

    Editor {
      id: editor
      anchors.fill: parent
      contentWidth: workspace.contentWidth
      bottomReservedSpace: promptPanel.height
      model: blockModel
    }

    DocumentHeader {
      id: documentHeader

      model: blockModel

      width: Math.min(workspace.contentWidth, workspace.width - 48)
      z: 2

      anchors {
        top: parent.top
        left: parent.left
        leftMargin: 24
        topMargin: 10
      }
    }

    SearchBar {
      id: searchBar

      model: blockModel
      editor: editor

      width: Math.min(440, workspace.width - 48)
      z: 4

      anchors {
        top: parent.top
        topMargin: 56
        horizontalCenter: parent.horizontalCenter
      }
    }

    PromptPanel {
      id: promptPanel
      z: 1
      expandedWidth: workspace.contentWidth
      anchors {
        horizontalCenter: parent.horizontalCenter
        bottom: parent.bottom
      }
    }

    OptionsMenu {
      id: optionsMenu
      anchors.fill: parent
      z: 3
      searchActive: searchBar.visible
      globalSearchActive: searchWindow.visible
      onSearchRequested: {
        if (searchBar.visible)
          searchBar.close(true)
        else
          searchBar.open()
      }
      onGlobalSearchRequested: {
        if (searchWindow.visible) {
          searchWindow.close()
        } else {
          if (searchBar.visible)
            searchBar.close(false)
          searchWindow.open()
        }
      }
    }
  }

  SettingsWindow {
    id: settingsWindow
  }

  SearchWindow {
    id: searchWindow

    blockModel: blockModel
    documentList: documentList
    editor: editor
    resultsModel: searchResults
    searchIndex: searchIndex
  }

  // Centered floating modal window for delete confirmation with backdrop overlay.
  Item {
    id: deleteConfirmationModal
    anchors.fill: parent
    visible: false
    z: 2000
    focus: true

    property string targetPath: ""
    property string targetName: ""
    property bool targetIsFolder: false

    function open(path, name, isFolder) {
      targetPath = path
      targetName = name
      targetIsFolder = isFolder
      visible = true
      forceActiveFocus()
    }

    function close() {
      visible = false
      targetPath = ""
      targetName = ""
    }

    Shortcut {
      sequence: "Escape"
      enabled: deleteConfirmationModal.visible
      onActivated: deleteConfirmationModal.close()
    }

    // Dimmed background backdrop
    Rectangle {
      anchors.fill: parent
      color: Qt.rgba(0, 0, 0, 0.52)

      MouseArea {
        anchors.fill: parent
        onClicked: deleteConfirmationModal.close()
      }
    }

    // Floating modal card centered on screen
    Rectangle {
      anchors.centerIn: parent
      width: 360
      height: modalContent.implicitHeight + 40
      radius: 8
      color: Theme.surface
      border.width: 1
      border.color: Theme.border

      MouseArea {
        anchors.fill: parent
      }

      Column {
        id: modalContent
        anchors {
          top: parent.top
          left: parent.left
          right: parent.right
          margins: 20
        }
        spacing: 20

        Row {
          spacing: 14
          width: parent.width

          Rectangle {
            width: 36
            height: 36
            radius: 8
            color: Theme.tint(Theme.danger, 0.15)

            IconImage {
              anchors.centerIn: parent
              width: 20
              height: 20
              source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_delete_20_regular.svg"
              sourceSize.width: 20
              sourceSize.height: 20
              color: Theme.danger
            }
          }

          Column {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 50
            spacing: 4

            Text {
              text: "Move to Trash"
              color: Theme.text
              font.pixelSize: 15
              font.bold: true
            }

            Text {
              width: parent.width
              text: "Are you sure you want to move " +
                    (deleteConfirmationModal.targetIsFolder ? "folder '" : "document '") +
                    deleteConfirmationModal.targetName + "' to Trash?"
              color: Theme.textMuted
              font.pixelSize: 13
              wrapMode: Text.WordWrap
            }
          }
        }

        Row {
          anchors.right: parent.right
          spacing: 10

          Button {
            id: cancelModalBtn
            text: "Cancel"
            height: 32
            leftPadding: 16
            rightPadding: 16

            background: Rectangle {
              radius: 8
              color: cancelModalBtn.down ? Theme.surfaceHover : (cancelModalBtn.hovered ? Theme.surfaceAlt : "transparent")
              border.width: 1
              border.color: Theme.border
            }

            contentItem: Text {
              text: cancelModalBtn.text
              color: Theme.text
              font.pixelSize: 13
              horizontalAlignment: Text.AlignHCenter
              verticalAlignment: Text.AlignVCenter
            }

            onClicked: deleteConfirmationModal.close()
          }

          Button {
            id: confirmModalBtn
            text: "Delete"
            height: 32
            leftPadding: 16
            rightPadding: 16

            background: Rectangle {
              radius: 8
              color: confirmModalBtn.down ? Qt.darker(Theme.danger, 1.2) : (confirmModalBtn.hovered ? Qt.darker(Theme.danger, 1.1) : Theme.danger)
            }

            contentItem: Text {
              text: confirmModalBtn.text
              color: "#ffffff"
              font.pixelSize: 13
              font.bold: true
              horizontalAlignment: Text.AlignHCenter
              verticalAlignment: Text.AlignVCenter
            }

            onClicked: {
              const pathToDelete = deleteConfirmationModal.targetPath
              deleteConfirmationModal.close()

              if (pathToDelete.length > 0) {
                documentList.deleteEntry(pathToDelete)

                // If currently open document was deleted, switch to another or open untitled
                if (blockModel.currentFilePath === pathToDelete) {
                  const recent = documentList.mostRecentDocumentPath()
                  if (recent.length > 0 && blockModel.loadDocument(recent)) {
                    documentList.currentFilePath = recent
                  } else {
                    const newPath = documentList.createUntitledPath()
                    if (newPath.length > 0 && blockModel.createDocument(newPath)) {
                      documentList.currentFilePath = newPath
                      documentList.refresh()
                      Qt.callLater(function() {
                        editor.focusFirstBlock()
                      })
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

  Component.onCompleted: {
    // Open the most recently touched document; otherwise start a new one in
    // the user document folder so Ctrl+S always has a destination.
    const recent = documentList.mostRecentDocumentPath()

    if (recent.length > 0 && blockModel.loadDocument(recent)) {
      documentList.currentFilePath = recent
      return
    }

    const path = documentList.createUntitledPath()

    if (path.length > 0 && blockModel.createDocument(path)) {
      documentList.currentFilePath = path
      documentList.refresh()
      Qt.callLater(function() {
        editor.focusFirstBlock()
      })
    }
  }
}
