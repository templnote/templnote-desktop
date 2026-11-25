import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl

Rectangle {
  id: root

  property bool expanded: true

  // Set by Main.qml.
  property var documentModel
  property var documentList
  property bool modalOpen: false


  // Item that is currently being renamed inline after creation or rename click.
  property string editingEntryPath: ""

  // Recursive folder/file search shown in place of the normal entry list.
  property bool searchVisible: false
  property string searchQuery: ""
  property var searchResults: []
  property int searchIndex: -1
  property int selectedIndex: -1

  // State captured across a watcher-triggered model reset.
  property bool preserveScrollOnReset: false
  property real savedContentY: 0
  property string savedSelectedPath: ""

  readonly property bool searchHasResults: searchResults.length > 0

  // Deletion confirmation requested signal to display centered floating dialog at window level.
  signal deleteRequested(string path, string name, bool isFolder)
  signal newDocumentCreated()
  // Document switches are requested from here and resolved by the window so
  // unsaved edits are handled by one shared prompt.
  signal openDocumentRequested(string path)
  signal createDocumentRequested()

  function confirmDelete(path, name, isFolder) {
    root.deleteRequested(path, name, isFolder)
  }

  function toggleSearch() {
    if (root.searchVisible)
      root.closeSearch(true)
    else
      root.openSearch()
  }

  function openSearch() {
    searchDebounce.stop()
    root.searchVisible = true
    root.searchQuery = ""
    root.searchResults = []
    root.searchIndex = -1

    Qt.callLater(function() {
      searchField.forceActiveFocus()
      searchField.selectAll()
    })
  }

  // `restoreFocus` is false when the caller is about to move focus by
  // activating a result.
  function closeSearch(restoreFocus) {
    if (!root.searchVisible)
      return

    searchDebounce.stop()
    root.searchVisible = false
    root.searchQuery = ""
    root.searchResults = []
    root.searchIndex = -1
    searchField.text = ""

    if (restoreFocus)
      documentView.forceActiveFocus()
  }

  function updateSearch(text) {
    root.searchQuery = text

    if (text.trim().length === 0 || !root.documentList) {
      searchDebounce.stop()
      root.searchResults = []
      root.searchIndex = -1
      return
    }

    // The recursive scan runs on a worker thread; debounce keystrokes so one
    // query per pause is issued instead of one filesystem walk per character.
    searchDebounce.restart()
  }

  function applySearchResults(query, results) {
    if (query !== root.searchQuery)
      return

    root.searchResults = results
    root.searchIndex = results.length > 0 ? 0 : -1
    root.scrollToSearchIndex()
  }

  function scrollToSearchIndex() {
    Qt.callLater(function() {
      if (root.searchVisible && root.searchIndex >= 0)
        searchView.positionViewAtIndex(root.searchIndex, ListView.Contain)
    })
  }

  function moveSearch(direction) {
    if (root.searchResults.length === 0)
      return

    const count = root.searchResults.length
    let next = root.searchIndex + direction

    if (next < 0)
      next = count - 1
    else if (next >= count)
      next = 0

    root.searchIndex = next
    root.scrollToSearchIndex()
  }

  function moveSelection(direction) {
    if (!root.documentList || root.documentList.count === 0)
      return

    const count = root.documentList.count
    let next = root.selectedIndex + direction

    if (next < 0)
      next = count - 1
    else if (next >= count)
      next = 0

    root.selectedIndex = next
    scrollToSelectedIndex()
  }

  function scrollToSelectedIndex() {
    Qt.callLater(function() {
      if (!root.searchVisible && root.selectedIndex >= 0)
        documentView.positionViewAtIndex(root.selectedIndex, ListView.Contain)
    })
  }

  function activateSelection(index) {
    if (!root.documentList || index < 0 || index >= root.documentList.count)
      return

    const path = root.documentList.filePathAt(index)
    if (!path || path.length === 0)
      return

    const isFolder = root.documentList.data(root.documentList.index(index, 0), 259) // IsFolderRole
    if (isFolder) {
      root.documentList.enterFolder(path)
      root.selectedIndex = 0
      scrollToSelectedIndex()
      return
    }

    root.openDocumentRequested(path)
  }

  // Loading itself is kept separate from the unsaved-edits decision so the
  // window can route every document switch through the same prompt.
  function openDocumentNow(path) {
    if (!root.documentModel || !path || path.length === 0)
      return false

    return root.documentModel.loadDocument(path)
  }

  Connections {
    target: root.documentList

    function onRefreshedExternally() {
      root.preserveScrollOnReset = true
    }

    function onModelAboutToBeReset() {
      if (root.documentList) {
        root.savedContentY = documentView.contentY
        root.savedSelectedPath =
            (root.selectedIndex >= 0 && root.selectedIndex < root.documentList.count)
            ? root.documentList.filePathAt(root.selectedIndex) : ""
      }
    }

    function onModelReset() {
      if (!root.documentList)
        return

      const savedY = root.savedContentY
      const shouldRestoreScroll = root.preserveScrollOnReset
      root.preserveScrollOnReset = false

      if (root.documentList.count === 0) {
        root.selectedIndex = -1
      } else {
        // A refresh can remove or reorder rows; follow the previously selected
        // entry or at least clamp the selection into the new range.
        let selected = -1
        const preferredPaths = [root.savedSelectedPath,
                                root.documentModel ? root.documentModel.currentFilePath : ""]

        for (let p = 0; p < preferredPaths.length && selected < 0; ++p) {
          const path = preferredPaths[p]
          if (path.length === 0)
            continue

          for (let i = 0; i < root.documentList.count; ++i) {
            if (root.documentList.filePathAt(i) === path) {
              selected = i
              break
            }
          }
        }

        if (selected < 0)
          selected = root.selectedIndex

        if (selected >= root.documentList.count)
          selected = root.documentList.count - 1

        root.selectedIndex = selected
      }

      if (shouldRestoreScroll) {
        Qt.callLater(function() {
          const maxY = Math.max(0, documentView.contentHeight - documentView.height)
          documentView.contentY = Math.max(0, Math.min(savedY, maxY))
        })
      }
    }
  }

  function enterCurrentFolder() {
    if (!root.documentList || root.selectedIndex < 0 || root.selectedIndex >= root.documentList.count)
      return

    const path = root.documentList.filePathAt(root.selectedIndex)
    const isFolder = root.documentList.data(root.documentList.index(root.selectedIndex, 0), 259)
    if (isFolder && path) {
      root.documentList.enterFolder(path)
      root.selectedIndex = 0
      scrollToSelectedIndex()
    }
  }

  function goUpFolder() {
    if (!root.documentList)
      return

    if (root.documentList.goUp()) {
      root.selectedIndex = 0
      scrollToSelectedIndex()
    }
  }

  function activateSearch(index) {
    if (index < 0 || index >= root.searchResults.length)
      return

    const result = root.searchResults[index]

    root.closeSearch(false)

    if (!root.documentList || !root.documentModel)
      return

    if (result.isFolder) {
      root.documentList.enterFolder(result.filePath)
      return
    }

    // Reveal the containing folder so the opened document is visible in the
    // explorer again.
    const separator = result.filePath.lastIndexOf("/")

    if (separator > 0) {
      const parentDirectory = result.filePath.substring(0, separator)

      if (root.documentList.directory !== parentDirectory)
        root.documentList.navigateTo(parentDirectory)
    }

    root.openDocumentRequested(result.filePath)
  }

  function createDocument() {
    root.createDocumentRequested()
  }

  function createDocumentNow() {
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
        // If this document is open, Main.qml's pathRenamed handler retargets
        // it without reloading, so unsaved edits are preserved.
        root.editingEntryPath = ""
      }
    }
  }

  Timer {
    id: searchDebounce
    interval: 180
    repeat: false
    onTriggered: {
      if (root.documentList && root.searchQuery.trim().length > 0)
        root.documentList.searchEntriesAsync(root.searchQuery)
    }
  }

  QtObject { id: noTarget }

  Connections {
    target: root.documentList ? root.documentList : noTarget

    function onSearchCompleted(query, results) {
      root.applySearchResults(query, results)
    }
  }

  Shortcut {
    sequence: "Ctrl+Shift+N"
    enabled: root.expanded && !root.modalOpen
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

    MouseArea {
      anchors.fill: parent
      acceptedButtons: Qt.LeftButton
      propagateComposedEvents: true
      onClicked: function(mouse) {
        if (!searchField.activeFocus) {
          documentView.forceActiveFocus()
          if (root.selectedIndex < 0 && root.documentList && root.documentList.count > 0)
            root.selectedIndex = 0
        }
        mouse.accepted = false
      }
    }

    Column {
      id: header

      anchors {
        top: parent.top
        left: parent.left
        right: parent.right
        topMargin: 10
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
              action: "search"
            },
          ]

          delegate: Button {
            id: actionButton

            required property var modelData

            // The sort action is a reserved placeholder: keep the control in
            // the layout but make it visibly inert instead of silently doing
            // nothing when pressed.
            readonly property bool available: modelData.action.length > 0

            enabled: available
            icon.source: modelData.icon
            icon.width: 20
            icon.height: 20
            icon.color: !available ? Theme.textFaint
                        : modelData.action === "search" && root.searchVisible
                          ? Theme.accent : Theme.textMuted
            width: (actionRow.width - actionRow.spacing * (actionRepeater.count - 1)) / actionRepeater.count
            height: 32
            padding: 0

            ToolTip.visible: actionButton.hovered && !available
            ToolTip.text: "Sorting is coming soon"
            ToolTip.delay: 400

            background: Rectangle {
              radius: 8
              color: actionButton.hovered && actionButton.enabled ? Theme.surfaceHover : "transparent"

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
              else if (modelData.action === "search")
                root.toggleSearch()
            }
          }
        }
      }

      Rectangle {
        id: searchBar

        width: parent.width
        height: 32
        radius: 8
        visible: root.searchVisible
        color: Theme.background
        border.width: 1
        border.color: searchField.activeFocus ? Theme.accent : Theme.border

        Row {
          anchors {
            fill: parent
            leftMargin: 8
            rightMargin: 6
          }

          spacing: 6

          Item {
            width: 14
            height: parent.height

            IconImage {
              anchors.centerIn: parent
              width: 14
              height: 14
              source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_search_20_regular.svg"
              sourceSize.width: 14
              sourceSize.height: 14
              color: Theme.textFaint
            }
          }

          TextField {
            id: searchField

            width: parent.width - 14 - searchCount.width - searchClose.width
                   - parent.spacing * 3
            height: parent.height
            placeholderText: "Search files and folders"
            placeholderTextColor: Theme.textFaint
            color: Theme.text
            font.pixelSize: 13
            padding: 0
            background: null
            selectByMouse: true

            onTextChanged: root.updateSearch(text)

            Keys.onUpPressed: function(event) {
              event.accepted = true
              root.moveSearch(-1)
            }

            Keys.onDownPressed: function(event) {
              event.accepted = true
              root.moveSearch(1)
            }

            Keys.onReturnPressed: function(event) {
              event.accepted = true
              root.activateSearch(root.searchIndex)
            }

            Keys.onEnterPressed: function(event) {
              event.accepted = true
              root.activateSearch(root.searchIndex)
            }

            Keys.onEscapePressed: function(event) {
              event.accepted = true
              root.closeSearch(true)
            }
          }

          Text {
            id: searchCount

            width: implicitWidth
            height: parent.height
            visible: root.searchQuery.length > 0
            text: root.searchResults.length
            color: Theme.textFaint
            font.pixelSize: 11
            verticalAlignment: Text.AlignVCenter
          }

          Item {
            id: searchClose

            width: 20
            height: parent.height

            Text {
              anchors.centerIn: parent
              text: "✕"
              color: searchCloseMouse.containsMouse ? Theme.accent : Theme.textMuted
              font.pixelSize: 11
            }

            MouseArea {
              id: searchCloseMouse
              anchors.fill: parent
              hoverEnabled: true
              cursorShape: Qt.PointingHandCursor
              onClicked: root.closeSearch(true)
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

            //don't delete this line bcs breadcrumbs uses index and not only modelData
            required property int index

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

      visible: !root.searchVisible
               && (!root.documentList || root.documentList.count === 0)
      text: "This folder is empty"
      color: Theme.textFaint
      font.pixelSize: 13
      wrapMode: Text.WordWrap
    }

    Text {
      id: searchEmptyState

      anchors {
        top: header.bottom
        left: parent.left
        right: parent.right
        topMargin: 12
        leftMargin: 20
        rightMargin: 16
      }

      visible: root.searchVisible && !root.searchHasResults
      text: root.searchQuery.length > 0
            ? "No matching files or folders"
            : "Type to search files and folders"
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
      visible: !root.searchVisible
      spacing: 1
      clip: true
      boundsBehavior: Flickable.StopAtBounds
      focus: true

      MouseArea {
        anchors.fill: parent
        z: -1
        acceptedButtons: Qt.LeftButton
        onClicked: {
          documentView.forceActiveFocus()
          if (root.selectedIndex < 0 && root.documentList && root.documentList.count > 0)
            root.selectedIndex = 0
        }
      }

      Keys.onUpPressed: function(event) {
        event.accepted = true
        root.moveSelection(-1)
      }

      Keys.onDownPressed: function(event) {
        event.accepted = true
        root.moveSelection(1)
      }

      Keys.onReturnPressed: function(event) {
        event.accepted = true
        root.activateSelection(root.selectedIndex)
      }

      Keys.onEnterPressed: function(event) {
        event.accepted = true
        root.activateSelection(root.selectedIndex)
      }

      Keys.onRightPressed: function(event) {
        event.accepted = true
        root.enterCurrentFolder()
      }

      Keys.onLeftPressed: function(event) {
        event.accepted = true
        root.goUpFolder()
      }

      Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Backspace) {
          event.accepted = true
          root.goUpFolder()
        }
      }

      delegate: Item {
        id: entry

        // Keep an explicit reference to the containing FileExplorer from the
        // delegate scope, where the outer component id is not always resolved.
        property var explorerRoot: root

        required property string name
        required property string filePath
        required property bool isFolder
        required property bool isCurrent
        required property int index

        readonly property bool isSelected: entry.index === root.selectedIndex

        property bool renamingEntry: root.editingEntryPath === entry.filePath
        property string draftName: entry.name

        width: documentView.width
        height: 28

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
                 : (entry.isSelected || entryMouse.containsMouse) ? Theme.surfaceHover : "transparent"

          Behavior on color {
            ColorAnimation {
              duration: 150
              easing.type: Easing.OutQuad
            }
          }
        }

        Rectangle {
          visible: entry.isCurrent

          anchors {
            left: parent.left
            verticalCenter: parent.verticalCenter
          }

          width: 3
          height: 18
          radius: 2
          color: Theme.accent
        }

        Item {
          anchors.fill: parent
          visible: !entry.renamingEntry

          MouseArea {
            id: entryMouse

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor

            onClicked: function(mouse) {
              documentView.forceActiveFocus()
              root.selectedIndex = entry.index
              if (!root.documentList)
                return

              if (entry.isFolder) {
                root.documentList.enterFolder(entry.filePath)
                root.selectedIndex = 0
                return
              }

              entry.explorerRoot.openDocumentRequested(entry.filePath)
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
            visible: entryMouse.containsMouse || entry.isSelected || renameMouse.containsMouse || deleteMouse.containsMouse || duplicateMouse.containsMouse
            spacing: 4

            Rectangle {
              id: duplicateFileBtn
              width: 22
              height: 22
              radius: 4
              visible: !entry.isFolder
              color: duplicateMouse.containsMouse ? Theme.surfaceHover : "transparent"

              IconImage {
                anchors.centerIn: parent
                width: 14
                height: 14
                source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_copy_20_regular.svg"
                sourceSize.width: 14
                sourceSize.height: 14
                color: duplicateMouse.containsMouse ? Theme.accent : Theme.textMuted
              }

              MouseArea {
                id: duplicateMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: function(mouse) {
                  mouse.accepted = true
                  if (root.documentList) {
                    root.documentList.duplicateDocument(entry.filePath)
                  }
                }
              }
            }

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
                color: deleteMouse.containsMouse ? Theme.danger : Theme.textMuted
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

    ListView {
      id: searchView

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

      model: root.searchResults
      visible: root.searchVisible
      spacing: 1
      clip: true
      boundsBehavior: Flickable.StopAtBounds

      delegate: Item {
        id: searchEntry

        required property var modelData
        required property int index

        width: searchView.width
        height: 34

        Rectangle {
          anchors.fill: parent
          radius: 8
          color: searchEntry.index === root.searchIndex
                 ? Theme.tint(Theme.accent, 0.16)
                 : searchEntryMouse.containsMouse ? Theme.surfaceHover : "transparent"

          Behavior on color {
            ColorAnimation {
              duration: 150
              easing.type: Easing.OutQuad
            }
          }
        }

        Rectangle {
          visible: searchEntry.index === root.searchIndex

          anchors {
            left: parent.left
            verticalCenter: parent.verticalCenter
          }

          width: 3
          height: 18
          radius: 2
          color: Theme.accent
        }

        MouseArea {
          id: searchEntryMouse

          anchors.fill: parent
          hoverEnabled: true
          cursorShape: Qt.PointingHandCursor

          onClicked: root.activateSearch(searchEntry.index)
        }

        Row {
          anchors {
            fill: parent
            leftMargin: 10
            rightMargin: 10
          }

          spacing: 8

          IconImage {
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            source: searchEntry.modelData.isFolder
                    ? "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_folder_20_regular.svg"
                    : "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_document_20_regular.svg"
            sourceSize.width: 16
            sourceSize.height: 16
            color: searchEntry.index === root.searchIndex ? Theme.accent : Theme.textMuted
          }

          Column {
            width: parent.width - 24
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1

            Text {
              width: parent.width
              text: searchEntry.modelData.name
              color: searchEntry.index === root.searchIndex ? Theme.text : Theme.textMuted
              font.pixelSize: 13
              elide: Text.ElideRight
            }

            Text {
              width: parent.width
              visible: searchEntry.modelData.relativePath.length > 0
              text: searchEntry.modelData.relativePath
              color: Theme.textFaint
              font.pixelSize: 11
              elide: Text.ElideMiddle
            }
          }
        }
      }
    }
  }
}
