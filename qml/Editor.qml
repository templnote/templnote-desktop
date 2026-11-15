import QtQuick
import QtQuick.Controls

Item {
  id: root

  required property var model

  // for a centrally content
  property real contentWidth: 800
  property var activeCommandDelegate: null
  property bool commandMenuAbove: false
  property real commandMenuAnchorY: 0
  property real commandMenuGap: 8

  // Search state. The find bar owns the query and passes the complete match
  // list here; the delegates only render the matches for their own block.
  property var searchMatches: []
  property int activeSearchMatch: -1
  property bool searchActive: false

  // True while the highlights come from the global search window. In this
  // mode the editor keeps focus, so Up/Down cycle matches, Escape dismisses
  // the highlights, and the first edit returns to normal writing.
  property bool searchNavigationActive: false

  // Last caret seen in the document, used to start local search at the first
  // match at or after the caret.
  property string caretBlockId: ""
  property int caretPosition: 0

  function setSearchActive(active) {
    root.searchActive = active
  }

  function caretContext() {
    const row = root.caretBlockId.length > 0
                ? root.model.indexOfBlock(root.caretBlockId)
                : -1

    if (row < 0)
      return { row: 0, position: 0 }

    return { row: row, position: root.caretPosition }
  }

  function setSearchMatches(matches, activeIndex) {
    root.searchMatches = matches
    root.activeSearchMatch = activeIndex

    if (activeIndex >= 0 && activeIndex < matches.length)
      root.scrollToSearchMatch(matches[activeIndex])
  }

  function clearSearch() {
    root.searchMatches = []
    root.activeSearchMatch = -1
    root.searchNavigationActive = false
  }

  function setSearchNavigationActive(active) {
    root.searchNavigationActive = active && root.searchMatches.length > 0
  }

  // Cycles the global-search selection. Called from the focused block when
  // the user presses Up/Down, mirroring the local find bar behaviour.
  function moveSearchMatch(direction) {
    if (!root.searchNavigationActive || root.searchMatches.length === 0)
      return

    const count = root.searchMatches.length
    let next = root.activeSearchMatch + direction

    if (next < 0)
      next = count - 1
    else if (next >= count)
      next = 0

    const match = root.searchMatches[next]

    root.activeSearchMatch = next
    root.scrollToSearchMatch(match)
    root.focusSearchMatch(match)
  }

  // Escape during global-search navigation only drops the highlights; the
  // caret stays on the match the user navigated to.
  function dismissSearchHighlights() {
    if (!root.searchNavigationActive)
      return

    root.clearSearch()
  }

  function scrollToSearchMatch(match) {
    const row = root.model.indexOfBlock(match.blockId)

    if (row < 0)
      return

    documentView.forceLayout()

    Qt.callLater(function() {
      documentView.positionViewAtIndex(row, ListView.Contain)
    })
  }

  // Enter on the find bar: move the caret into the matched block and select
  // the matched range so it stays visible after the highlights are cleared.
  function focusSearchMatch(match) {
    const row = root.model.indexOfBlock(match.blockId)

    if (row < 0)
      return

    documentView.positionViewAtIndex(row, ListView.Contain)
    documentView.forceLayout()

    Qt.callLater(function() {
      const delegate = documentView.itemAtIndex(row)

      if (delegate) {
        delegate.refreshBlock()
        delegate.focusBlockAtRange(match.start, match.start + match.length)
      }
    })
  }

  // Returns the highlight descriptors for one block. Compared by blockId
  // because the row can change while the find bar is open.
  function searchHighlightsFor(blockId) {
    const highlights = []
    const active = root.activeSearchMatch >= 0
                   && root.activeSearchMatch < root.searchMatches.length
                 ? root.searchMatches[root.activeSearchMatch]
                 : null

    for (let i = 0; i < root.searchMatches.length; ++i) {
      const match = root.searchMatches[i]

      if (match.blockId !== blockId)
        continue

      highlights.push({
        start: match.start,
        length: match.length,
        active: active !== null
                && active.blockId === match.blockId
                && active.start === match.start
                && active.length === match.length
      })
    }

    return highlights
  }

  function focusDelegateAt(row, position, attempt) {
    const focusAttempt = attempt === undefined ? 0 : attempt

    if (row < 0 || row >= root.model.count)
      return

    documentView.forceLayout()

    Qt.callLater(function(){
      const delegate = documentView.itemAtIndex(row)

      if (delegate) {
        delegate.refreshBlock()
        delegate.focusBlock(position)
      } else if (focusAttempt < 10) {
        // A model reset can take more than one QML turn to create the
        // delegate. Retry briefly so new-document focus is reliable without
        // affecting normal editor navigation.
        root.focusDelegateAt(row, position, focusAttempt + 1)
      }
    })
  }

  function focusFirstBlock() {
    focusDelegateAt(0, 0)
  }

  Connections {
    target: root.model

    function onDocumentCreated() {
      // The model has finished its reset and save; wait for ListView to
      // materialize the first delegate, then place the caret at its start.
      Qt.callLater(function() {
        root.focusFirstBlock()
      })
    }

    function onDocumentLoaded() {
      Qt.callLater(function() {
        root.focusFirstBlock()
      })
    }
  }

  function restoreFocusAfterHistory(blockId, oldRow, undo, cursorPosition) {
    documentView.forceLayout()

    Qt.callLater(function() {
      let row = root.model.indexOfBlock(blockId)

      // The undo/redo may have removed the focused block (for example when
      // undoing a split). Move to the nearest surviving block in that case.
      if (row < 0) {
        const fallback = undo ? oldRow - 1 : oldRow
        row = Math.max(0, Math.min(fallback, root.model.count - 1))
      }

      if (row < 0)
        return

      documentView.positionViewAtIndex(row, ListView.Contain)
      documentView.forceLayout()

      const delegate = documentView.itemAtIndex(row)

      if (delegate) {
        delegate.refreshBlock()
        // Restore the caret to the position associated with the history step.
        // A negative position means "end of text" (structural steps).
        delegate.focusBlock(cursorPosition)
      }
    })
  }

  function undoFrom(blockId, row) {
    root.model.undo()

    const targetId = root.model.lastHistoryBlockId()
    restoreFocusAfterHistory(targetId.length > 0 ? targetId : blockId,
                             row, true,
                             root.model.lastHistoryCursorPosition())
  }

  function redoFrom(blockId, row) {
    root.model.redo()

    const targetId = root.model.lastHistoryBlockId()
    restoreFocusAfterHistory(targetId.length > 0 ? targetId : blockId,
                             row, false,
                             root.model.lastHistoryCursorPosition())
  }

  // Global entry points used by application-level shortcuts. Undo/redo still
  // focuses the block affected by the history step, even when the document
  // itself did not have focus.
  function undo() {
    if (!root.model.canUndo)
      return

    root.model.undo()

    const targetId = root.model.lastHistoryBlockId()

    if (targetId.length > 0)
      restoreFocusAfterHistory(targetId, 0, true,
                               root.model.lastHistoryCursorPosition())
  }

  function redo() {
    if (!root.model.canRedo)
      return

    root.model.redo()

    const targetId = root.model.lastHistoryBlockId()

    if (targetId.length > 0)
      restoreFocusAfterHistory(targetId, 0, false,
                               root.model.lastHistoryCursorPosition())
  }

  Rectangle {
    anchors.fill: parent
    color: Theme.background
  }

  // Document content

  ListView {
    id: documentView

    width: Math.min(root.contentWidth, parent.width - 48)

    anchors {
      top: parent.top
      bottom: parent.bottom
      horizontalCenter: parent.horizontalCenter

      topMargin: root.searchActive ? 96 : 48
      bottomMargin: 160
    }

    model: root.model
    spacing: 0
    clip: true
    // Keep the command menu anchored while it is open. Programmatic scrolling
    // is still allowed when the menu is first positioned.
    interactive: root.activeCommandDelegate === null

    delegate: Item {
      id: delegateRoot

      // These properties receive the C++ model roles.
      required property string blockId
      required property string content
      required property int blockType
      required property int headingLevel
      required property int nextBlockType
      required property int numberIndex
      required property int index

      readonly property bool isBullet: delegateRoot.blockType === 2
      readonly property bool nextIsBullet: delegateRoot.nextBlockType === 2
      readonly property bool isNumbered: delegateRoot.blockType === 3
      readonly property bool nextIsNumbered: delegateRoot.nextBlockType === 3
      readonly property bool isCompactList: (isBullet && nextIsBullet) || (isNumbered && nextIsNumbered)

      width: documentView.width
      height: renderer.implicitHeight + (delegateRoot.isCompactList ? 0 : 5)

      function focusBlock(position) {
        renderer.focusEditor(position)
      }

      function focusBlockAtX(xPosition) {
        renderer.focusEditorAtX(xPosition)
      }

      function focusBlockAtRange(start, end) {
        renderer.focusEditorRange(start, end)
      }

      function applyCommand(command) {
        if (!command)
          return
        root.model.updateBlockText(delegateRoot.blockId, "")
        if (root.model.changeBlockType(delegateRoot.blockId, command.type, command.level))
          root.focusDelegateAt(delegateRoot.index, 0)
      }

      function closeCommandMenu() {
        renderer.commandMenuActive = false
      }

      function refreshBlock() {
        renderer.refreshFromModel()
      }

      BlockRenderer {
        id: renderer

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right

        model: root.model
        blockId: delegateRoot.blockId
        content: delegateRoot.content
        blockType: delegateRoot.blockType
        headingLevel: delegateRoot.headingLevel
        numberIndex: delegateRoot.numberIndex
        searchHighlights: root.searchHighlightsFor(delegateRoot.blockId)
        searchNavigationActive: root.searchNavigationActive

        onCaretMoved: function(position) {
          root.caretBlockId = delegateRoot.blockId
          root.caretPosition = position
        }

        onSearchNavigated: function(direction) {
          root.moveSearchMatch(direction)
        }

        onSearchDismissed: {
          root.dismissSearchHighlights()
        }

        onSearchEditStarted: {
          // Typing or pressing Enter ends the navigation session before the
          // text change reaches the model.
          root.dismissSearchHighlights()
        }

        onMoveFocusRequested: function(direction, xPosition) {
          const targetIndex = delegateRoot.index + direction

          if (targetIndex < 0 || targetIndex >= root.model.count)
            return

          const targetDelegate = documentView.itemAtIndex(targetIndex)

          if (!targetDelegate)
            return

          targetDelegate.focusBlockAtX(xPosition)
        }

        onCommandMenuRequested: function(query, xPosition, yPosition) {
          // Subsequent text changes update the filtered commands, but do not
          // re-run placement. This keeps an already-open menu above or below
          // the cursor instead of flipping as its height changes.
          if (root.activeCommandDelegate === delegateRoot
              && renderer.commandMenuActive) {
            commandMenu.openFor(query)
            return
          }

          root.activeCommandDelegate = delegateRoot

          documentView.positionViewAtIndex(
            delegateRoot.index,
            ListView.Contain
          )

          Qt.callLater(function() {
            // Build the filtered list first so commandMenu.height reflects the
            // actual number of commands before collision handling.
            commandMenu.openFor(query)

            const menuPosition = renderer.mapToItem(
              root,
              xPosition,
              yPosition
            )

            const gap = root.commandMenuGap
            const belowGap = 0
            // Include the line's visual height so the flipped menu keeps a
            // similar breathing space to the below-cursor placement.
            const cursorHeight = 30
            const availableBottom = root.height - 96
            const belowY = menuPosition.y + belowGap
            const aboveY = menuPosition.y - cursorHeight
                             - commandMenu.height - gap
            let menuY = belowY

            // Prefer below the cursor. Flip above it only when the complete
            // menu fits there; otherwise keep it below rather than clamping
            // the menu over the active cursor.
            let flippedAbove = false
            if (belowY + commandMenu.height > availableBottom && aboveY >= 0) {
              menuY = aboveY
              flippedAbove = true
            }

            if (flippedAbove)
              menuY = Math.max(0, menuY)

            commandMenu.x = Math.max(
              8,
              Math.min(root.width - commandMenu.width - 8,
                       menuPosition.x)
            )

            root.commandMenuAbove = flippedAbove
            root.commandMenuAnchorY = menuPosition.y - cursorHeight
            commandMenu.y = menuY
            renderer.commandMenuActive = true
          })
        }

        onCommandMenuDismissed: {
          if (root.activeCommandDelegate === delegateRoot) {
            renderer.commandMenuActive = false
            commandMenu.visible = false
            root.activeCommandDelegate = null
          }
        }

        onCommandMenuAccepted: {
          if (root.activeCommandDelegate === delegateRoot) {
            renderer.commandMenuActive = false
            commandMenu.visible = false
            root.activeCommandDelegate.applyCommand(commandMenu.selectedCommand)
            root.activeCommandDelegate = null
          }
        }

        onCommandMenuNavigated: function(direction) {
          if (root.activeCommandDelegate === delegateRoot)
            commandMenu.moveSelection(direction)
        }

        onEnterPressed: {

          const currentText = renderer.editorText
          const cursorPosition = renderer.cursorPosition
          
          // for a smoother markdown behaviour
          if ((delegateRoot.blockType === 2 || delegateRoot.blockType === 3) && renderer.editorText.length === 0) {
            if (root.model.changeBlockType(delegateRoot.blockId, 0, 0)) {
              root.focusDelegateAt(delegateRoot.index, 0)
            }
            return
          }

          const newId = root.model.splitBlock(
            delegateRoot.blockId,
            cursorPosition,
            currentText
          )
          if (newId.length === 0) return

          const newIndex = delegateRoot.index + 1;
          //new block should be at index + 1 since items is at index , ez mate, so don't do wtf

          documentView.forceLayout()

          Qt.callLater(function() {
            const newDelegate = documentView.itemAtIndex(newIndex)

            if (newDelegate) {
              newDelegate.refreshBlock()
              newDelegate.focusBlock(0)
            }
          })
        }

        onBackspacePressed: {
          const currentIndex = delegateRoot.index
          const currentText = renderer.editorText
          const currentType = delegateRoot.blockType

          // don't get confused, it first removes the blockType and then removes the block
          if ( currentText.length === 0 && currentType !== 0 ) {
            if (root.model.changeBlockType(delegateRoot.blockId, 0, 0)) {
              root.focusDelegateAt(currentIndex, 0)
            }
            return
          }

          if (currentIndex === 0)
          return

          if (currentText.length === 0) {
            if ( root.model.removeBlock(delegateRoot.blockId)) {
              root.focusDelegateAt(currentIndex - 1, -1)
            }
            return
          }

          if (root.model.mergeWithPrevious(
                delegateRoot.blockId,
                currentText)) {
            root.focusDelegateAt(currentIndex - 1, -1)
          }
        }

        onUndoRequested: {
          root.undoFrom(delegateRoot.blockId, delegateRoot.index)
        }

        onRedoRequested: {
          root.redoFrom(delegateRoot.blockId, delegateRoot.index)
        }
      }
    }

    footer: Item {
      width: documentView.width
      height: documentView.height * 0.6
    }

  CommandMenu {
    id: commandMenu

    onHeightChanged: {
      if (root.commandMenuAbove && root.activeCommandDelegate !== null)
        y = root.commandMenuAnchorY - height - root.commandMenuGap
    }

    onCommandSelected: function(command) {
      if (root.activeCommandDelegate) {
        root.activeCommandDelegate.applyCommand(command)
        root.activeCommandDelegate.closeCommandMenu()
        root.activeCommandDelegate = null
      }
      visible = false
    }
  }

    Component.onCompleted: {
      console.log("Editor initialized")
      console.log("Model count:", root.model.count)
    }
  }
}
