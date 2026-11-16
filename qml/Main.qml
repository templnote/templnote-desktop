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
  title: hasDocument
         ? "Templnote — " + blockModel.documentTitle + (blockModel.isModified ? " •" : "")
         : "Templnote"
  color: Theme.background

  // True while a document is loaded. When false the workspace shows the blank
  // home surface instead of an empty editor.
  readonly property bool hasDocument: blockModel.currentFilePath.length > 0

  // Set once the user chose Save/Discard so the close handler lets the
  // window go through on the second pass.
  property bool allowClose: false

  onHasDocumentChanged: {
    if (!hasDocument && searchBar.visible)
      searchBar.close(false)
  }

  onClosing: function(close) {
    if (window.allowClose || !blockModel.isModified)
      return

    close.accepted = false
    saveConfirmationModal.open()
  }

  function saveAndClose() {
    let saved = false

    if (blockModel.currentFilePath.length > 0)
      saved = blockModel.saveCurrentDocument()
    else {
      const path = documentList.createUntitledPath()
      saved = path.length > 0 && blockModel.saveDocument(path)
    }

    if (saved) {
      saveConfirmationModal.close()
      window.allowClose = true
      window.close()
      return
    }

    saveConfirmationModal.errorText = blockModel.lastError.length > 0
                                     ? blockModel.lastError
                                     : "Could not save the document."
  }

  function discardAndClose() {
    saveConfirmationModal.close()
    window.allowClose = true
    window.close()
  }

  BlockModel {
    id: blockModel

    autosaveEnabled: true
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
      const path = blockModel.currentFilePath

      documentList.currentFilePath = path
      documentList.refresh()

      // Remember the exact document so session restore does not have to guess
      // from modification times (and can find files inside subfolders).
      if (path.length > 0)
        Preferences.lastDocumentPath = path
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
      // Nothing to save while the home surface is showing.
      if (blockModel.currentFilePath.length === 0)
        return

      if (blockModel.saveCurrentDocument())
        documentList.refresh()
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
    enabled: !searchWindow.visible && window.hasDocument
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
    id: navigationRail

    visible: Preferences.showNavigationRail
    width: visible ? 56 : 0
    clip: true
    explorerExpanded: fileExplorer.expanded

    anchors {
      top: parent.top
      left: parent.left
      bottom: parent.bottom
    }
    onExplorerToggleRequested: {
      fileExplorer.expanded = !fileExplorer.expanded
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

    HomeScreen {
      id: homeScreen

      anchors.fill: parent
      visible: !window.hasDocument
      z: 0
    }

    Editor {
      id: editor
      anchors.fill: parent
      contentWidth: workspace.contentWidth
      bottomReservedSpace: promptPanel.height
      model: blockModel
      visible: window.hasDocument
    }

    DocumentHeader {
      id: documentHeader

      model: blockModel

      visible: window.hasDocument
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
      visible: window.hasDocument
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

      options: [
        { key: "find", label: "Find in document", shortcut: "Ctrl+/" },
        { key: "globalSearch", label: "Search all documents", shortcut: "Ctrl+Shift+/" },
        { key: "shortcuts", label: "Keyboard shortcuts", shortcut: "F1" },
        { key: "settings", label: "Settings", shortcut: "" }
      ]

      onOptionTriggered: function(key) {
        if (key === "find") {
          if (!window.hasDocument)
            return
          if (searchBar.visible)
            searchBar.close(true)
          else
            searchBar.open()
        } else if (key === "globalSearch") {
          if (searchWindow.visible) {
            searchWindow.close()
          } else {
            if (searchBar.visible)
              searchBar.close(false)
            searchWindow.open()
          }
        } else if (key === "shortcuts") {
          shortcutsSheet.open()
        } else if (key === "settings") {
          settingsWindow.open()
        }
      }

      onSearchRequested: {
        if (!window.hasDocument)
          return
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

  ShortcutsSheet {
    id: shortcutsSheet
  }

  Shortcut {
    sequence: "F1"
    context: Qt.ApplicationShortcut
    onActivated: shortcutsSheet.toggle()
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
              color: Theme.accentText
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

                // If the currently open document was deleted, switch to the
                // most recent survivor; otherwise fall back to the home
                // surface with no document open.
                if (blockModel.currentFilePath === pathToDelete) {
                  const recent = documentList.mostRecentDocumentPath()
                  if (recent.length > 0 && blockModel.loadDocument(recent)) {
                    documentList.currentFilePath = recent
                  } else {
                    Preferences.lastDocumentPath = ""
                    blockModel.closeDocument()
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  // Shown when closing with unsaved changes (autosave disabled, still
  // typing, or the last save failed).
  Item {
    id: saveConfirmationModal
    anchors.fill: parent
    visible: false
    z: 2001
    focus: true

    property string errorText: ""

    function open() {
      errorText = ""
      visible = true
      forceActiveFocus()
    }

    function close() {
      visible = false
      errorText = ""
    }

    Shortcut {
      sequence: "Escape"
      enabled: saveConfirmationModal.visible
      onActivated: saveConfirmationModal.close()
    }

    Rectangle {
      anchors.fill: parent
      color: Qt.rgba(0, 0, 0, 0.52)

      MouseArea {
        anchors.fill: parent
        onClicked: saveConfirmationModal.close()
      }
    }

    Rectangle {
      anchors.centerIn: parent
      width: 380
      height: saveModalContent.implicitHeight + 40
      radius: 8
      color: Theme.surface
      border.width: 1
      border.color: Theme.border

      MouseArea {
        anchors.fill: parent
      }

      Column {
        id: saveModalContent

        anchors {
          top: parent.top
          left: parent.left
          right: parent.right
          margins: 20
        }

        spacing: 16

        Text {
          width: parent.width
          text: "Save changes?"
          color: Theme.text
          font.pixelSize: 16
          font.bold: true
        }

        Text {
          width: parent.width
          text: "Your document has unsaved changes. Save it before closing?"
          color: Theme.textMuted
          font.pixelSize: 13
          wrapMode: Text.WordWrap
        }

        Text {
          width: parent.width
          visible: saveConfirmationModal.errorText.length > 0
          text: saveConfirmationModal.errorText
          color: Theme.danger
          font.pixelSize: 12
          wrapMode: Text.WordWrap
        }

        Row {
          anchors.right: parent.right
          spacing: 10

          Button {
            id: closeCancelButton
            text: "Cancel"
            height: 32
            leftPadding: 16
            rightPadding: 16

            background: Rectangle {
              radius: 8
              color: closeCancelButton.down ? Theme.surfaceHover
                     : (closeCancelButton.hovered ? Theme.surfaceAlt : "transparent")
              border.width: 1
              border.color: Theme.border
            }

            contentItem: Text {
              text: closeCancelButton.text
              color: Theme.text
              font.pixelSize: 13
              horizontalAlignment: Text.AlignHCenter
              verticalAlignment: Text.AlignVCenter
            }

            onClicked: saveConfirmationModal.close()
          }

          Button {
            id: closeDiscardButton
            text: "Don't Save"
            height: 32
            leftPadding: 16
            rightPadding: 16

            background: Rectangle {
              radius: 8
              color: closeDiscardButton.down ? Theme.surfaceHover
                     : (closeDiscardButton.hovered ? Theme.surfaceAlt : "transparent")
              border.width: 1
              border.color: Theme.border
            }

            contentItem: Text {
              text: closeDiscardButton.text
              color: Theme.text
              font.pixelSize: 13
              horizontalAlignment: Text.AlignHCenter
              verticalAlignment: Text.AlignVCenter
            }

            onClicked: window.discardAndClose()
          }

          Button {
            id: closeSaveButton
            text: "Save"
            height: 32
            leftPadding: 18
            rightPadding: 18

            background: Rectangle {
              radius: 8
              color: closeSaveButton.down ? Qt.darker(Theme.accent, 1.15)
                     : (closeSaveButton.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent)
            }

            contentItem: Text {
              text: closeSaveButton.text
              color: Theme.accentText
              font.pixelSize: 13
              font.bold: true
              horizontalAlignment: Text.AlignHCenter
              verticalAlignment: Text.AlignVCenter
            }

            onClicked: window.saveAndClose()
          }
        }
      }
    }
  }

  Component.onCompleted: {
    fileExplorer.expanded = Preferences.expandExplorerOnLaunch

    // Reopen the exact document from the previous session when restore is
    // enabled. The saved path is preferred because the last open document may
    // live in a subfolder or not be the most recently modified file. Fall back
    // to the newest document anywhere in the tree for older configs.
    let restored = ""

    if (Preferences.restoreLastSession) {
      const saved = Preferences.lastDocumentPath

      if (saved.length > 0 && blockModel.loadDocument(saved))
        restored = saved
      else {
        const recent = documentList.mostRecentDocumentPathRecursive()

        if (recent.length > 0 && blockModel.loadDocument(recent))
          restored = recent
      }
    }

    if (restored.length > 0)
      documentList.currentFilePath = restored
  }
}
