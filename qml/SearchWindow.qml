import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Layouts
import "SearchService"

// Full-window global search overlay. The query bar stays focused while the
// SplitView shows grouped results on the left and the selected block on the
// right.
Item {
  id: root

  anchors.fill: parent
  visible: false
  z: 1600

  property var blockModel: null
  property var documentList: null
  property var editor: null
  property var resultsModel: null
  property var searchIndex: null
  // Set by the host window so document jumps reuse the unsaved-edits prompt.
  property var confirmUnsavedChanges: null

  property var previousFocus: null
  property int currentIndex: -1
  property var currentResult: null
  property string selectedDocumentPath: ""

  property alias query: searchField.text

  signal closed()

  ModalFocusGuard { id: searchFocusGuard }

  Component.onCompleted: searchFocusGuard.setTargets([searchField])

  // Escape must work no matter which part of the panel was clicked last.
  Shortcut {
    sequence: "Escape"
    enabled: root.visible
    onActivated: root.close(true)
  }

  Keys.onTabPressed: function(event) {
    event.accepted = true
    searchFocusGuard.cycle(1)
  }
  Keys.onBacktabPressed: function(event) {
    event.accepted = true
    searchFocusGuard.cycle(-1)
  }

  LocalSearchService {
    id: localSearch
  }

  Timer {
    id: searchDebounce
    interval: 150
    repeat: false
    onTriggered: root.runSearch()
  }

  Connections {
    target: root.searchIndex

    function onIndexUpdated() {
      if (root.visible)
        root.runSearch()
    }
  }

  Connections {
    target: root.resultsModel

    function onCountChanged() {
      if (!root.visible || !root.resultsModel || root.query.length === 0)
        return

      if (root.resultsModel.count > 0)
        root.selectResult(0)
      else
        root.clearSelection()
    }
  }

  function open() {
    if (root.visible) {
      searchField.forceActiveFocus()
      searchField.selectAll()
      return
    }

    root.previousFocus = Window.window ? Window.window.activeFocusItem : null

    if (root.searchIndex)
      root.searchIndex.syncIncrementalAsync()

    root.visible = true
    searchField.forceActiveFocus()
    searchField.selectAll()
    root.runSearch()
  }

  function close(restoreFocus) {
    if (!root.visible)
      return

    const shouldRestore = restoreFocus === undefined ? true : restoreFocus
    const focusTarget = root.previousFocus

    searchDebounce.stop()
    root.visible = false
    root.previousFocus = null
    searchField.text = ""

    if (root.resultsModel)
      root.resultsModel.searchAsync("")

    root.currentIndex = -1
    root.currentResult = null
    root.selectedDocumentPath = ""
    resultsList.selectedIndex = -1
    resultsList.selectedDocumentPath = ""
    preview.result = null

    if (shouldRestore && focusTarget)
      focusTarget.forceActiveFocus()

    root.closed()
  }

  function clearSelection() {
    root.currentIndex = -1
    root.currentResult = null
    root.selectedDocumentPath = ""
    resultsList.selectedIndex = -1
    resultsList.selectedDocumentPath = ""
    preview.result = null
  }

  function runSearch() {
    if (!root.visible || !root.resultsModel)
      return

    if (root.query.length === 0) {
      root.clearSelection()
      root.resultsModel.searchAsync("")
      return
    }

    // Results arrive asynchronously; onCountChanged selects the first one.
    root.resultsModel.searchAsync(root.query)
  }

  function selectResult(index) {
    if (!root.resultsModel || root.resultsModel.count === 0) {
      root.currentIndex = -1
      root.currentResult = null
      root.selectedDocumentPath = ""
      resultsList.selectedIndex = -1
      resultsList.selectedDocumentPath = ""
      preview.result = null
      return
    }

    const count = root.resultsModel.count
    let next = index % count

    if (next < 0)
      next += count

    root.selectedDocumentPath = ""
    resultsList.selectedDocumentPath = ""
    root.currentIndex = next
    root.currentResult = root.resultsModel.resultAt(next)
    resultsList.selectedIndex = next
    preview.result = root.currentResult

    Qt.callLater(function() {
      resultsList.positionCurrent()
    })
  }

  function selectDocument(path) {
    if (!path || path.length === 0)
      return

    root.selectedDocumentPath = path
    resultsList.selectedDocumentPath = path
    root.currentIndex = -1
    root.currentResult = null
    resultsList.selectedIndex = -1
    preview.result = null

    Qt.callLater(function() {
      resultsList.positionDocument(path)
    })
  }

  function openDocumentAtTop(path) {
    if (!path || path.length === 0 || !root.blockModel)
      return

    const switching = root.blockModel.currentFilePath !== path

    const proceed = function() {
      root.close(false)

      if (switching) {
        if (!root.blockModel.loadDocument(path))
          return

        if (root.documentList)
          root.documentList.currentFilePath = path
      }

      Qt.callLater(function() {
        if (root.editor)
          root.editor.focusFirstBlock()
      })
    }

    // Unsaved edits are never saved silently just because a search result was
    // activated; the window decides through the shared confirmation prompt.
    if (switching && typeof root.confirmUnsavedChanges === "function")
      root.confirmUnsavedChanges(proceed)
    else
      proceed()
  }

  // Keyboard navigation only visits actual results. Document headers are a
  // visual grouping and are reachable with the mouse, but selecting one with
  // Up/Down would leave currentIndex at -1.
  function navigationItems() {
    const items = []
    if (!root.resultsModel)
      return items

    for (let i = 0; i < root.resultsModel.count; ++i) {
      const res = root.resultsModel.resultAt(i)
      if (res)
        items.push({ type: "result", index: i, result: res })
    }
    return items
  }

  function move(direction) {
    const items = navigationItems()
    if (items.length === 0)
      return

    let currentPos = -1
    for (let i = 0; i < items.length; ++i) {
      if (root.currentIndex >= 0 && items[i].index === root.currentIndex) {
        currentPos = i
        break
      }
    }

    if (currentPos < 0) {
      currentPos = direction > 0 ? -1 : items.length
    }

    let nextPos = (currentPos + direction) % items.length
    if (nextPos < 0)
      nextPos += items.length

    root.selectResult(items[nextPos].index)
  }

  function accept() {
    if (root.selectedDocumentPath.length > 0) {
      root.openDocumentAtTop(root.selectedDocumentPath)
      return
    }

    if (root.currentIndex < 0 || !root.currentResult)
      return

    const result = root.currentResult
    const searchQuery = root.query
    const targetPath = result.documentPath
    const targetBlockId = result.blockId
    const switching = !root.blockModel || targetPath.length === 0
                      ? false
                      : root.blockModel.currentFilePath !== targetPath

    const proceed = function() {
      root.close(false)

      if (!root.blockModel || targetPath.length === 0)
        return

      if (switching) {
        if (!root.blockModel.loadDocument(targetPath))
          return

        if (root.documentList)
          root.documentList.currentFilePath = targetPath
      }

      Qt.callLater(function() {
        // Recompute from the loaded model rather than trusting possibly stale
        // index ranges. All query terms are highlighted, matching the preview.
        const matches = localSearch.searchTerms(root.blockModel, searchQuery)
        let active = -1

        for (let i = 0; i < matches.length; ++i) {
          if (matches[i].blockId === targetBlockId) {
            active = i
            break
          }
        }

        if (active < 0 && matches.length > 0)
          active = 0

        if (root.editor) {
          root.editor.setSearchMatches(matches, active)

          if (root.editor.setSearchNavigationActive)
            root.editor.setSearchNavigationActive(active >= 0)

          if (active >= 0)
            root.editor.focusSearchMatch(matches[active])
        }
      })
    }

    if (switching && typeof root.confirmUnsavedChanges === "function")
      root.confirmUnsavedChanges(proceed)
    else
      proceed()
  }

  // Dimmed backdrop. Clicking it dismisses the search window.
  Rectangle {
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0.48)

    MouseArea {
      anchors.fill: parent
      onClicked: root.close(true)
    }
  }

  Rectangle {
    id: panel

    anchors.centerIn: parent
    width: Math.min(parent.width - 80, 1040)
    height: Math.min(parent.height - 80, 700)
    radius: 12
    color: Theme.surface
    border.width: 0
    clip: true

    // Swallow clicks so clicking inside the panel does not dismiss it.
    MouseArea {
      anchors.fill: parent
    }

    ColumnLayout {
      anchors.fill: parent
      anchors.margins: 14
      spacing: 10

      // Search input bar with subtle pill background
      Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 44
        radius: 8
        color: Theme.surfaceAlt

        RowLayout {
          anchors.fill: parent
          anchors.leftMargin: 12
          anchors.rightMargin: 12
          spacing: 10

          IconImage {
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            source: Qt.resolvedUrl("../assets/icons/ic_fluent_search_20_regular.svg")
            sourceSize.width: 18
            sourceSize.height: 18
            color: Theme.textFaint
          }

          TextField {
            id: searchField

            Layout.fillWidth: true
            Layout.preferredHeight: 34
            padding: 0
            topPadding: 0
            bottomPadding: 0

            color: Theme.text
            selectionColor: Theme.accent
            selectedTextColor: Theme.accentText
            placeholderText: "Search all documents"
            placeholderTextColor: Theme.textFaint
            font.pixelSize: 15
            selectByMouse: true
            background: null

            onTextChanged: {
              if (root.visible)
                searchDebounce.restart()
            }

            onAccepted: {
              // Flush a pending query so Enter never acts on stale results.
              searchDebounce.stop()
              root.runSearch()
              root.accept()
            }

            Keys.onUpPressed: function(event) {
              event.accepted = true
              root.move(-1)
            }

            Keys.onDownPressed: function(event) {
              event.accepted = true
              root.move(1)
            }

            Keys.onEscapePressed: function(event) {
              event.accepted = true
              root.close(true)
            }
          }

          Text {
            visible: searchField.text.length > 0
            text: root.resultsModel
                  ? root.resultsModel.count
                    + (root.resultsModel.count === 1 ? " result"
                                                     : " results")
                  : ""
            color: Theme.textMuted
            font.pixelSize: 11
          }
        }

        Text {
          visible: root.resultsModel ? root.resultsModel.truncated : false
          Layout.fillWidth: true
          text: "Showing first 200 matches"
          color: Theme.textFaint
          font.pixelSize: 11
        }
      }

      SplitView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        orientation: Qt.Horizontal

        handle: Rectangle {
          implicitWidth: 8
          implicitHeight: 8
          color: "transparent"
        }

        ResultsList {
          id: resultsList

          SplitView.preferredWidth: 360
          SplitView.minimumWidth: 260
          SplitView.fillHeight: true

          resultsModel: root.resultsModel
          emptyText: root.query.length > 0
                     ? "No documents match this search"
                     : "Type to search all documents"

          onResultClicked: function(index) {
            root.selectResult(index)
            searchField.forceActiveFocus()
          }

          onDocumentHeaderClicked: function(path) {
            root.selectDocument(path)
            searchField.forceActiveFocus()
          }

          onDocumentHeaderDoubleClicked: function(path) {
            root.openDocumentAtTop(path)
          }
        }

        ResultPreview {
          id: preview

          SplitView.fillWidth: true
          SplitView.minimumWidth: 320
        }
      }
    }
  }
}
