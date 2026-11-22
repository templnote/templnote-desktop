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
  // Freezes result mutation while that confirmation is open so the result
  // captured by confirmUnsavedChanges(proceed) stays consistent. Managed here
  // because Main.qml owns the confirmation function (not a binding we own).
  property bool freezeResults: false
  // Main.qml binds this while another modal covers the window. Escape must
  // then dismiss that modal instead of closing the search panel.
  property bool modalActive: false

  property var previousFocus: null
  property int currentIndex: -1
  property var currentResult: null
  property string selectedDocumentPath: ""

  // State for the asynchronous "Enter on a result" scan. The old code ran a
  // full synchronous corpus scan on the GUI thread when a result was opened.
  property int acceptToken: 0
  property bool acceptPending: false
  property string acceptBlockId: ""
  property int acceptMatchStart: -1
  // Kept across result updates so a background reindex does not snap the
  // selection back to result 0 while the user is reading a preview.
  property string selectedResultBlockId: ""

  // Enter pressed for a query whose result set is still in flight. Consumed
  // when the matching countChanged arrives so Enter never acts on the previous
  // query's results. Distinct from acceptPending, which tracks the later
  // async highlight scan started by accept().
  property bool pendingAccept: false
  // Bounded retries for the fallback below (SearchIndex can suppress
  // searchFinished entirely while a reindex changes its cache generation).
  property int pendingAcceptFallbacks: 0
  // The query that produced the current result set. Lets onAccepted skip a
  // redundant search (and a second accept) when results are already current.
  property string resultsQuery: ""
  property bool resultsReady: false

  property alias query: searchField.text

  signal closed()

  ModalFocusGuard { id: searchFocusGuard }

  Component.onCompleted: root.refreshFocusTargets()

  // Escape must work no matter which part of the panel was clicked last, but
  // must not compete with a modal (e.g. the save confirmation) that is on top.
  Shortcut {
    sequence: "Escape"
    enabled: root.visible && !root.modalActive
    onActivated: root.close(true)
  }

  // Rebuilt on every Tab because ListView recreates its section and row
  // delegates as results change, so headers and result rows stay in the
  // keyboard focus ring. focusTargets() supplies both, in visual order.
  function refreshFocusTargets() {
    const targets = [searchField]
    if (resultsList && resultsList.focusTargets)
      targets.push.apply(targets, resultsList.focusTargets())
    searchFocusGuard.setTargets(targets)
  }

  Keys.onTabPressed: function(event) {
    event.accepted = true
    root.refreshFocusTargets()
    searchFocusGuard.cycle(1)
    // The focus ring includes headers/rows ListView keeps outside the
    // viewport; reveal whichever delegate Tab just focused.
    Qt.callLater(resultsList.ensureFocusVisible)
  }
  Keys.onBacktabPressed: function(event) {
    event.accepted = true
    root.refreshFocusTargets()
    searchFocusGuard.cycle(-1)
    Qt.callLater(resultsList.ensureFocusVisible)
  }

  LocalSearchService {
    id: localSearch
  }

  Connections {
    target: localSearch

    function onSearchTermsFinished(token, matches) {
      if (token !== root.acceptToken || !root.acceptPending || !root.visible)
        return

      root.acceptPending = false
      root.applyAcceptMatches(matches)
      // The overlay can be dismissed once the highlights for the loaded
      // document have been handed to the editor.
      root.close(false)
    }
  }

  Connections {
    target: root.blockModel

    // A reply computed for a different document must never paint highlights
    // in the one that is now open. Loading a new document inside accept()
    // happens before the request's token is allocated, so it cannot cancel
    // its own in-flight scan.
    function onCurrentFilePathChanged() {
      ++root.acceptToken
      root.acceptPending = false
      root.pendingAccept = false
      root.pendingAcceptFallbacks = 0
      pendingAcceptFallback.stop()
    }
  }

  Timer {
    id: searchDebounce
    interval: 150
    repeat: false
    onTriggered: root.runSearch()
  }

  // SearchIndex::searchAsync suppresses searchFinished when the cache
  // generation changes mid-flight. If an Enter was deferred for that query,
  // no countChanged would ever consume it. Re-issue the query a couple of
  // times; consumePendingAccept() clears the flag first so a late reply can
  // never accept twice.
  Timer {
    id: pendingAcceptFallback
    interval: 600
    repeat: false
    onTriggered: {
      if (!root.pendingAccept || !root.visible || root.query.length === 0)
        return

      // The model may already hold the right result set even though its
      // countChanged did not fire for this accept.
      if (root.resultsReady && root.resultsQuery === root.query &&
          root.resultsModel && root.resultsModel.count > 0) {
        root.consumePendingAccept()
        return
      }

      if (root.pendingAcceptFallbacks >= 2)
        return
      ++root.pendingAcceptFallbacks
      root.runSearch()
      pendingAcceptFallback.restart()
    }
  }

  Connections {
    target: root.searchIndex

    function onIndexUpdated() {
      // Indexing can finish several times in quick succession; route through
      // the debounce instead of spawning an FTS query per signal. Do not let a
      // background reindex swap results behind an open confirmation modal.
      if (root.freezeResults) {
        searchDebounce.stop()
        return
      }
      if (root.visible)
        searchDebounce.restart()
    }
  }

  Connections {
    target: root.resultsModel

    function onCountChanged() {
      if (root.freezeResults || !root.visible || !root.resultsModel
          || root.query.length === 0)
        return

      root.resultsReady = true

      // A document header was selected explicitly; a reindex must not replace
      // that selection with result 0. An Enter deferred for a query that was
      // still in flight must still open that header: accept() routes to
      // openDocumentAtTop() when a header is selected and clears the pending
      // state via close(). Do not just discard pendingAccept here.
      if (root.selectedDocumentPath.length > 0) {
        if (root.pendingAccept)
          root.consumePendingAccept()
        root.pendingAccept = false
        root.pendingAcceptFallbacks = 0
        pendingAcceptFallback.stop()
        return
      }

      if (root.resultsModel.count === 0) {
        root.clearSelection()
        root.pendingAccept = false
        root.pendingAcceptFallbacks = 0
        pendingAcceptFallback.stop()
        return
      }

      // SearchResultsModel emits countChanged even when the result set is
      // unchanged. Re-find the previously selected block instead of always
      // jumping back to the first result.
      if (root.selectedResultBlockId.length > 0) {
        for (let i = 0; i < root.resultsModel.count; ++i) {
          const res = root.resultsModel.resultAt(i)

          if (res && res.blockId === root.selectedResultBlockId) {
            root.selectResult(i)
            root.consumePendingAccept()
            return
          }
        }
      }

      root.selectResult(0)
      root.consumePendingAccept()
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
    root.freezeResults = false
    // Invalidate any highlight scan still running for the previous query or
    // document so a late reply cannot repaint the editor after we are gone.
    ++root.acceptToken
    root.acceptPending = false
    root.pendingAccept = false
    root.pendingAcceptFallbacks = 0
    pendingAcceptFallback.stop()
    root.resultsReady = false
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
    root.selectedResultBlockId = ""
    root.selectedDocumentPath = ""
    resultsList.selectedIndex = -1
    resultsList.selectedDocumentPath = ""
    preview.result = null
  }

  function runSearch() {
    if (!root.visible || !root.resultsModel)
      return

    root.resultsQuery = root.query
    root.resultsReady = false

    if (root.query.length === 0) {
      root.clearSelection()
      root.resultsModel.searchAsync("")
      return
    }

    // Results arrive asynchronously; onCountChanged selects the first one.
    root.resultsModel.searchAsync(root.query)
  }

  // Enter pressed while results were still in flight. Runs once for the
  // result set that just arrived; clearing the flag first prevents a second
  // countChanged for the same query from accepting again.
  function consumePendingAccept() {
    if (!root.pendingAccept || root.resultsQuery !== root.query)
      return

    root.pendingAccept = false
    root.pendingAcceptFallbacks = 0
    pendingAcceptFallback.stop()
    root.accept()
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
    root.selectedResultBlockId = root.currentResult && root.currentResult.blockId
                                 ? root.currentResult.blockId : ""
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
    // A header selection must not be replaced by a result when the next
    // countChanged arrives.
    root.selectedResultBlockId = ""
    resultsList.selectedIndex = -1
    preview.result = null

    Qt.callLater(function() {
      resultsList.positionDocument(path)
    })
  }

  // Runs an action that may need the unsaved-edits confirmation. Results stay
  // frozen until the action runs, so a background reindex/count change cannot
  // mutate the selection while the confirmation is pending. Main.qml owns the
  // confirmation function, so the flag is managed here rather than bound from
  // window.modalOpen (which is also true while this search window is visible).
  function confirmDocumentAction(action) {
    if (typeof root.confirmUnsavedChanges !== "function") {
      action()
      return
    }
    root.freezeResults = true
    // A debounce scheduled just before the freeze would otherwise fire while
    // the confirmation is open and mutate the captured result set.
    searchDebounce.stop()
    root.confirmUnsavedChanges(function() {
      root.freezeResults = false
      action()
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
    if (switching)
      root.confirmDocumentAction(proceed)
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

  function applyAcceptMatches(matches) {
    // The reply is only meaningful for the still-open search whose request
    // produced it (close()/a document switch invalidate the token first).
    if (!root.editor || !root.visible)
      return

    if (!matches || matches.length === 0) {
      root.editor.setSearchMatches([], -1)
      // Otherwise the editor keeps treating Enter/F3 as search navigation
      // for a result set that is now empty.
      if (root.editor.setSearchNavigationActive)
        root.editor.setSearchNavigationActive(false)
      return
    }

    let active = -1

    // Prefer the exact occurrence shown in the clicked preview, then any
    // occurrence in the same block, then the first match as a fallback.
    for (let i = 0; i < matches.length; ++i) {
      if (matches[i].blockId === root.acceptBlockId &&
          (root.acceptMatchStart < 0 ||
           matches[i].start === root.acceptMatchStart)) {
        active = i
        break
      }
    }

    if (active < 0) {
      for (let i = 0; i < matches.length; ++i) {
        if (matches[i].blockId === root.acceptBlockId) {
          active = i
          break
        }
      }
    }

    if (active < 0)
      active = 0

    root.editor.setSearchMatches(matches, active)

    if (root.editor.setSearchNavigationActive)
      root.editor.setSearchNavigationActive(active >= 0)

    root.editor.focusSearchMatch(matches[active])
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
    const ranges = result.ranges || []
    const matchStart = ranges.length > 0 && ranges[0] ? ranges[0].start : -1
    const switching = !root.blockModel || targetPath.length === 0
                      ? false
                      : root.blockModel.currentFilePath !== targetPath

    const proceed = function() {
      if (!root.blockModel || targetPath.length === 0) {
        root.close(false)
        return
      }

      if (switching) {
        if (!root.blockModel.loadDocument(targetPath)) {
          root.close(false)
          return
        }

        if (root.documentList)
          root.documentList.currentFilePath = targetPath
      }

      // Recompute the highlight ranges from the loaded model in the worker.
      // The overlay stays open until onSearchTermsFinished applies them, so
      // root.visible can serve as a guard against late replies; close() and a
      // document switch both invalidate the request via acceptToken.
      root.acceptBlockId = targetBlockId
      root.acceptMatchStart = matchStart
      root.acceptPending = true
      ++root.acceptToken
      localSearch.searchTermsAsync(root.blockModel, searchQuery,
                                   root.acceptToken)
    }

    if (switching)
      root.confirmDocumentAction(proceed)
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
              if (root.visible) {
                // A cancelled confirmation must not leave results frozen once
                // the user resumes searching.
                root.freezeResults = false
                // A new query starts a fresh result set. Clear the whole
                // selection, including an explicitly selected document
                // header: leaving selectedDocumentPath set would make the
                // countChanged bail-out suppress every result for this query.
                root.clearSelection()
                // An Enter for the previous query is obsolete as soon as the
                // query changes.
                root.pendingAccept = false
                root.pendingAcceptFallbacks = 0
                pendingAcceptFallback.stop()
                searchDebounce.restart()
              }
            }

            onAccepted: {
              // Enter may arrive before the debounced query has produced
              // results. Remember it and accept the first result of the new
              // query when countChanged arrives, rather than the old one.
              searchDebounce.stop()
              if (root.query.length === 0) {
                root.pendingAccept = false
                root.accept()
                return
              }

              if (root.resultsReady && root.resultsQuery === root.query) {
                // Results are already for this query: accept once, directly.
                root.pendingAccept = false
                root.accept()
              } else {
                root.pendingAccept = true
                root.pendingAcceptFallbacks = 0
                pendingAcceptFallback.restart()
                root.runSearch()
              }
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
              if (root.modalActive)
                return
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
            // The row delegate focuses itself on a click; do not yank focus
            // back to the field or its keyboard ring/border would be lost.
            root.selectResult(index)
          }

          onNavigationRequested: function(direction) {
            // Up/Down on a focused row delegates to the same navigation the
            // search field uses so the selection state stays in one place.
            root.move(direction)
          }

          onResultActivated: {
            // Return/Space/Enter on a focused row must accept the current
            // selection, not merely re-select the row. Do not pass the
            // focused row's index: Up/Down may have moved the selection off
            // that delegate already, and accept() uses currentResult.
            root.accept()
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
