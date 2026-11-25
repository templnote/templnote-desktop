import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Templnote

ApplicationWindow {
  id: window

  width: 1100
  height: 760
  minimumWidth: 900
  // The editor reserves fixed top/bottom bands and every overlay is sized from
  // the window height, so a shorter window would collapse the writing area and
  // give the dialogs negative dimensions.
  minimumHeight: 600

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

  // True while any full-window surface covers the editor. Used to keep
  // application shortcuts from firing behind a modal.
  readonly property bool modalOpen: settingsWindow.visible
                                    || searchWindow.visible
                                    || shortcutsSheet.visible
                                    || deleteConfirmationModal.visible
                                    || saveConfirmationModal.visible

  onHasDocumentChanged: {
    if (!hasDocument && searchBar.visible)
      searchBar.close(false)
  }

  onClosing: function(close) {
    if (window.allowClose || !blockModel.isModified)
      return

    close.accepted = false
    requestClose()
  }

  // Single policy for unsaved edits. Every operation that would replace or
  // discard the current document routes through here, so switching files from
  // the explorer or global search behaves the same as closing the window.
  // `action` runs after a successful save, after an explicit discard, or
  // immediately when there is nothing to save.
  function confirmUnsavedChanges(action) {
    if (typeof action !== "function")
      return

    if (!blockModel.isModified) {
      action()
      return
    }

    if (saveConfirmationModal.visible)
      return

    saveConfirmationModal.resolveAction = action
    saveConfirmationModal.open()
  }

  function saveCurrentDocument() {
    if (blockModel.currentFilePath.length > 0)
      return blockModel.saveCurrentDocument()

    const path = documentList.createUntitledPath()
    return path.length > 0 && blockModel.saveDocument(path)
  }

  function finishSaveAction() {
    const action = saveConfirmationModal.resolveAction
    saveConfirmationModal.close()

    if (typeof action === "function")
      action()
  }

  function requestClose() {
    confirmUnsavedChanges(function() {
      window.allowClose = true
      window.close()
    })
  }

  function saveAndClose() {
    if (saveCurrentDocument()) {
      finishSaveAction()
      return
    }

    saveConfirmationModal.errorText = blockModel.lastError.length > 0
                                     ? blockModel.lastError
                                     : "Could not save the document."
  }

  function discardAndClose() {
    finishSaveAction()
  }

  function openHome() {
    if (!hasDocument)
      return

    if (searchBar.visible)
      searchBar.close(false)

    blockModel.closeDocument()
  }

  function requestHome() {
    if (!hasDocument)
      return

    confirmUnsavedChanges(openHome)
  }

  function requestOpenDocument(path) {
    if (!path || path.length === 0)
      return

    confirmUnsavedChanges(function() {
      if (blockModel.loadDocument(path))
        documentList.currentFilePath = path
    })
  }

  function requestNewDocument() {
    confirmUnsavedChanges(function() {
      fileExplorer.createDocumentNow()
    })
  }

  // Settings is a page in this window, just like the home surface: the rail
  // and explorer stay put and the workspace swaps the editor for settings.
  function openSettings(section) {
    if (searchBar.visible)
      searchBar.close(false)
    if (searchWindow.visible)
      searchWindow.close()
    if (shortcutsSheet.visible)
      shortcutsSheet.close()

    settingsWindow.open(section)
  }

  // Shared by the delete confirmation dialog and any other caller that needs
  // to remove an entry and reconcile the currently open document.
  function deleteDocument(pathToDelete) {
    if (pathToDelete.length === 0 || !documentList.deleteEntry(pathToDelete))
      return

    // If the open document itself was deleted, switch to the most recent
    // survivor; otherwise fall back to the home surface. Documents rescued
    // from a deleted folder were retargeted by the pathRenamed handler above
    // and stay open.
    const current = blockModel.currentFilePath

    if (current === pathToDelete || current.startsWith(pathToDelete + "/")) {
      const recent = documentList.mostRecentDocumentPath()
      if (recent.length > 0 && blockModel.loadDocument(recent)) {
        documentList.currentFilePath = recent
      } else {
        Preferences.lastDocumentPath = ""
        blockModel.closeDocument()
      }
    }
  }

  // `blockModel` is a C++ context object shared by the editor and the search
  // services.

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
      // from modification times (and can find files inside subfolders). Clear
      // it on close too, otherwise "restore last session" reopens a document
      // the user intentionally closed.
      Preferences.lastDocumentPath = path
    }

    function onDocumentLoaded() {
      // onCurrentFilePathChanged already refreshes the explorer for path
      // changes; a reload of the same file does not need another scan.
      documentList.currentFilePath = blockModel.currentFilePath
    }
  }

  Connections {
    target: documentList

    // Folder renames and folder deletions can move the open document without
    // changing its content. Follow the move instead of reloading from disk so
    // unsaved edits survive and autosave targets the new path.
    function onPathRenamed(oldPath, newPath) {
      const current = blockModel.currentFilePath

      if (current === oldPath)
        blockModel.adoptRenamedPath(newPath)
      else if (current.startsWith(oldPath + "/"))
        blockModel.adoptRenamedPath(newPath + current.substring(oldPath.length))
    }
  }

  Shortcut {
    // each can expand to multiple key combination so better to bind with an array, so don't get astonished
    sequences: [StandardKey.Save]
    context: Qt.ApplicationShortcut
    enabled: !window.modalOpen
    onActivated: {
      // Nothing to save while the home surface is showing.
      if (blockModel.currentFilePath.length === 0)
        return

      if (blockModel.saveCurrentDocument())
        documentList.refresh()
    }
  }

  Shortcut {
    sequences: [StandardKey.Undo]
    context: Qt.ApplicationShortcut
    enabled: !window.modalOpen
    onActivated: {
      editor.undo()
    }
  }
  Shortcut {
    sequences: [StandardKey.Redo]
    context: Qt.ApplicationShortcut
    enabled: !window.modalOpen
    onActivated: {
      editor.redo()
    }
  }

  Shortcut {
    sequence: "Ctrl+/"
    context: Qt.ApplicationShortcut
    enabled: !window.modalOpen && window.hasDocument
    onActivated: searchBar.open()
  }

  Shortcut {
    // Shift+/ is reported as '?' on some keyboard layouts, so bind both
    // spellings of the same physical shortcut.
    sequences: ["Ctrl+Shift+/", "Ctrl+Shift+?"]
    context: Qt.ApplicationShortcut
    enabled: !window.modalOpen
    onActivated: {
      if (searchBar.visible)
        searchBar.close(false)

      searchWindow.open()
    }
  }

  NavigationRail {
    id: navigationRail

    width: 56
    explorerExpanded: fileExplorer.expanded

    anchors {
      top: parent.top
      left: parent.left
      bottom: parent.bottom
    }
    onExplorerToggleRequested: {
      fileExplorer.expanded = !fileExplorer.expanded
    }
    onHomeRequested: {
      if (settingsWindow.visible)
        settingsWindow.close()
      window.requestHome()
    }
    onSettingsRequested: window.openSettings()
  }

  FileExplorer {
    id: fileExplorer

    documentModel: blockModel
    documentList: documentList
    modalOpen: window.modalOpen

    anchors {
      top: parent.top
      left: navigationRail.right
      bottom: parent.bottom
    }

    onDeleteRequested: function(path, name, isFolder) {
      deleteConfirmationModal.open(path, name, isFolder)
    }

    onOpenDocumentRequested: function(path) {
      if (settingsWindow.visible)
        settingsWindow.close()
      window.requestOpenDocument(path)
    }

    onCreateDocumentRequested: function() {
      if (settingsWindow.visible)
        settingsWindow.close()
      window.requestNewDocument()
    }

    onNewDocumentCreated: {
      Qt.callLater(function() {
        editor.focusFirstBlock()
      })
    }
  }

  Item {
    id: workspace

    readonly property bool isFullWidth: {
      if (blockModel.hasFullWidthSet) {
        return blockModel.fullWidth
      }
      return Preferences.lineWidth === "Wide"
    }

    property real contentWidth: isFullWidth
      ? Math.max(800, width - 240)
      : Math.min(width - 64, 820)

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
      visible: !window.hasDocument && !settingsWindow.visible
      z: 0
    }

    Editor {
      id: editor
      anchors.fill: parent
      contentWidth: workspace.contentWidth
      model: blockModel
      modalOpen: window.modalOpen
      visible: window.hasDocument && !settingsWindow.visible

      // Full-window surfaces sit above the editor; wheel events must not move
      // the document while one of them is open.
      scrollingEnabled: !settingsWindow.visible && !searchWindow.visible
                        && !shortcutsSheet.visible
                        && !deleteConfirmationModal.visible
                        && !saveConfirmationModal.visible
    }

    DocumentHeader {
      id: documentHeader

      model: blockModel

      visible: window.hasDocument && !settingsWindow.visible
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

    OptionsMenu {
      id: optionsMenu
      anchors.fill: parent
      z: 3
      searchActive: searchBar.visible
      globalSearchActive: searchWindow.visible
      fullWidthActive: workspace.isFullWidth

      onToggleFullWidthRequested: {
        if (!window.hasDocument)
          return
        blockModel.fullWidth = !workspace.isFullWidth
        blockModel.saveCurrentDocument()
      }

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
          window.openSettings()
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

    // A page, not a second window: the rail and explorer stay visible while
    // this covers the workspace in place of the editor/home surface.
    SettingsWindow {
      id: settingsWindow

      anchors.fill: parent
    }
  }

  ShortcutsSheet {
    id: shortcutsSheet
  }

  Shortcut {
    sequence: "F1"
    context: Qt.ApplicationShortcut
    enabled: !window.modalOpen || shortcutsSheet.visible
    onActivated: shortcutsSheet.toggle()
  }

  SearchWindow {
    id: searchWindow

    blockModel: blockModel
    documentList: documentList
    editor: editor
    resultsModel: searchResults
    searchIndex: searchIndex

    // While a modal is on top, Escape belongs to that modal; the search panel
    // keeps its own focus and result state until the modal is dismissed.
    modalActive: saveConfirmationModal.visible || settingsWindow.visible
                 || shortcutsSheet.visible

    // Documents opened from search share the same unsaved-edits policy as
    // the explorer and window close.
    confirmUnsavedChanges: function(action) {
      window.confirmUnsavedChanges(action)
    }
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

    ModalFocusGuard { id: deleteFocusGuard }

    Keys.onTabPressed: function(event) {
      event.accepted = true
      deleteFocusGuard.cycle(1)
    }
    Keys.onBacktabPressed: function(event) {
      event.accepted = true
      deleteFocusGuard.cycle(-1)
    }

    function open(path, name, isFolder) {
      targetPath = path
      targetName = name
      targetIsFolder = isFolder
      visible = true

      // Focus the non-destructive action first and keep Tab inside the dialog.
      Qt.callLater(function() {
        deleteFocusGuard.setTargets([cancelModalBtn, confirmModalBtn])
        cancelModalBtn.forceActiveFocus()
      })
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
              window.deleteDocument(pathToDelete)
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
    // Callback run after Save or Don't Save; cleared on cancel/dismiss.
    property var resolveAction: null

    // Dismissing the prompt must also unfreeze global search results: the
    // action passed to confirmUnsavedChanges() is deliberately not invoked on
    // Cancel, so SearchWindow cannot clear its own guard in that path.
    onVisibleChanged: {
      if (!visible && searchWindow)
        searchWindow.freezeResults = false
    }

    ModalFocusGuard { id: saveFocusGuard }

    Keys.onTabPressed: function(event) {
      event.accepted = true
      saveFocusGuard.cycle(1)
    }
    Keys.onBacktabPressed: function(event) {
      event.accepted = true
      saveFocusGuard.cycle(-1)
    }

    function open() {
      errorText = ""
      visible = true

      Qt.callLater(function() {
        saveFocusGuard.setTargets([closeCancelButton, closeDiscardButton, closeSaveButton])
        closeCancelButton.forceActiveFocus()
      })
    }

    function close() {
      visible = false
      errorText = ""
      resolveAction = null
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
          text: "Your document has unsaved changes. Save them before continuing?"
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

  // Developer FPS counter & performance diagnostics overlay
  Item {
    id: fpsOverlay
    visible: Preferences.showFpsOverlay
    z: 99999
    width: fpsBadge.width
    height: fpsBadge.height

    property int frameCount: 0
    property int currentFps: 0
    property real lastTimestamp: 0

    anchors {
      left: Preferences.fpsOverlayPosition.indexOf("Left") !== -1 ? parent.left : undefined
      right: Preferences.fpsOverlayPosition.indexOf("Right") !== -1 ? parent.right : undefined
      top: Preferences.fpsOverlayPosition.indexOf("Top") !== -1 ? parent.top : undefined
      bottom: Preferences.fpsOverlayPosition.indexOf("Bottom") !== -1 ? parent.bottom : undefined
      margins: 14
    }

    Rectangle {
      id: fpsBadge
      width: fpsLayout.implicitWidth + 18
      height: fpsLayout.implicitHeight + 10
      radius: 6
      color: Qt.rgba(0.08, 0.08, 0.12, 0.88)
      border.color: fpsOverlay.currentFps >= 50 ? "#22c55e" : (fpsOverlay.currentFps >= 30 ? "#eab308" : "#ef4444")
      border.width: 1

      Row {
        id: fpsLayout
        anchors.centerIn: parent
        spacing: 6

        Rectangle {
          width: 7
          height: 7
          radius: 3.5
          anchors.verticalCenter: parent.verticalCenter
          color: fpsOverlay.currentFps >= 50 ? "#22c55e" : (fpsOverlay.currentFps >= 30 ? "#eab308" : "#ef4444")
        }

        Text {
          text: fpsOverlay.currentFps + " FPS"
          color: "#f8fafc"
          font.pixelSize: 12
          font.bold: true
          font.family: "monospace"
          anchors.verticalCenter: parent.verticalCenter
        }
      }
    }

    // Measure frame intervals via active render loop animation
    NumberAnimation on opacity {
      from: 0.999
      to: 1.0
      duration: 1000
      loops: Animation.Infinite
      running: fpsOverlay.visible
    }

    onOpacityChanged: {
      fpsOverlay.frameCount++
    }

    Timer {
      interval: 500
      running: fpsOverlay.visible
      repeat: true
      onTriggered: {
        fpsOverlay.currentFps = Math.round(fpsOverlay.frameCount * 2)
        fpsOverlay.frameCount = 0
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
