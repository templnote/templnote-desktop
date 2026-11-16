import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl

Item {
  id: root

  required property var model

  // for a centrally content
  property real contentWidth: 800
  property real bottomReservedSpace: 0
  property var activeCommandDelegate: null
  property bool commandMenuAbove: false
  property real commandMenuAnchorY: 0
  property real commandMenuGap: 2

  property var activeSelectionDelegate: null
  property bool selectionToolbarAbove: true
  property real selectionToolbarAnchorY: 0
  property real selectionToolbarGap: 6
  property var pendingSelection: null

  // Drag and drop reordering state
  property string hoveredBlockId: ""
  property string draggingBlockId: ""
  property int draggingSourceIndex: -1
  property int dropTargetIndex: -1
  property real draggingVisualY: 0
  property real draggingItemHeight: 0
  property real dragGlobalY: 0
  property bool isDraggingBlock: draggingBlockId.length > 0

  Timer {
    id: dragAutoScrollTimer
    interval: 16
    repeat: true
    running: root.isDraggingBlock
    onTriggered: {
      if (!root.isDraggingBlock) return
      // Map global drag Y to documentView coordinates
      const viewPos = documentView.mapFromItem(null, 0, root.dragGlobalY)
      const topEdge = 40
      const bottomEdge = documentView.height - 40
      const maxScroll = Math.max(0, documentView.contentHeight - documentView.height)

      if (viewPos.y < topEdge) {
        const factor = Math.max(0, Math.min(1, (topEdge - viewPos.y) / topEdge))
        const step = (factor * 12) + 2
        documentView.contentY = Math.max(0, documentView.contentY - step)
        root.updateDragTarget()
      } else if (viewPos.y > bottomEdge) {
        const factor = Math.max(0, Math.min(1, (viewPos.y - bottomEdge) / 40))
        const step = (factor * 12) + 2
        documentView.contentY = Math.min(maxScroll, documentView.contentY + step)
        root.updateDragTarget()
      }
    }
  }

  function updateDragTarget() {
    if (!root.isDraggingBlock) return
    const src = root.draggingSourceIndex
    const curContentY = documentView.mapFromItem(null, 0, root.dragGlobalY).y + documentView.contentY

    // Determine target index based on original block unshifted center lines
    let bestTarget = src
    let minDist = 999999

    for (let i = 0; i < root.model.count; ++i) {
      const item = documentView.itemAtIndex(i)
      if (!item) continue
      const itemCenter = item.y + (item.height / 2)
      const dist = Math.abs(curContentY - itemCenter)
      if (dist < minDist) {
        minDist = dist
        bestTarget = i
      }
    }

    if (bestTarget >= 0 && bestTarget < root.model.count) {
      root.dropTargetIndex = bestTarget
    }
  }

  Timer {
    id: selectionTimer
    interval: 200
    repeat: false
    onTriggered: {
      if (!root.pendingSelection || !root.pendingSelection.delegate)
        return

      const item = root.pendingSelection
      const delegateRoot = item.delegate
      const renderer = item.renderer

      root.activeSelectionDelegate = delegateRoot
      selectionToolbar.currentBlockType = delegateRoot.blockType
      selectionToolbar.currentHeadingLevel = delegateRoot.headingLevel
      selectionToolbar.isBold = renderer.isFormatActive("bold")
      selectionToolbar.isItalic = renderer.isFormatActive("italic")
      selectionToolbar.isUnderline = renderer.isFormatActive("underline")
      selectionToolbar.isStrikethrough = renderer.isFormatActive("strike")

      const pos = renderer.mapToItem(root, item.x, item.y)
      const gap = root.selectionToolbarGap
      const actualCursorHeight = item.cursorHeight > 0 ? item.cursorHeight : 20
      const aboveY = pos.y - gap - selectionToolbar.height
      const belowY = pos.y + actualCursorHeight + gap
      const availableBottom = root.height - root.bottomReservedSpace - 1

      let flippedAbove = true
      let toolbarY = aboveY

      // Default is displaying on topside of cursor. If not enough space above, flip below.
      if (aboveY < 0 && belowY + selectionToolbar.height <= availableBottom) {
        toolbarY = belowY
        flippedAbove = false
      }

      // Center horizontally around the active cursor position, constrained to editor bounds
      const idealX = pos.x - (selectionToolbar.implicitWidth / 2)
      selectionToolbar.x = Math.max(
        8,
        Math.min(root.width - selectionToolbar.implicitWidth - 8, idealX)
      )

      root.selectionToolbarAbove = flippedAbove
      root.selectionToolbarAnchorY = pos.y
      selectionToolbar.y = toolbarY
      selectionToolbar.visible = true
    }
  }

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

  function ensureCursorVisible(cursorGlobalY, cursorHeight) {
    if (cursorHeight <= 0 || documentView.height <= 0)
      return

    // Map global Y coordinate to documentView local coordinates
    const localCursor = documentView.mapFromItem(null, 0, cursorGlobalY)
    const localCursorY = localCursor.y
    const viewHeight = documentView.height

    // When the cursor gets close to the bottom (within bottom 35% of the view)
    // or goes below the view, smoothly scroll so the cursor is centered.
    const triggerThreshold = viewHeight * 0.65
    if (localCursorY + cursorHeight > triggerThreshold) {
      const currentContentY = scrollAnimation.running ? scrollAnimation.to : documentView.contentY
      const targetContentY = currentContentY + (localCursorY - (viewHeight / 2))
      const maxContentY = Math.max(0, documentView.contentHeight - viewHeight)
      const clampedTarget = Math.max(0, Math.min(targetContentY, maxContentY))

      if (Math.abs(clampedTarget - currentContentY) > 2) {
        scrollAnimation.stop()
        scrollAnimation.from = documentView.contentY
        scrollAnimation.to = clampedTarget
        scrollAnimation.start()
      }
    } else if (localCursorY < 0) {
      // If cursor is scrolled above the view (e.g. arrow up), scroll up to keep it visible
      const currentContentY = scrollAnimation.running ? scrollAnimation.to : documentView.contentY
      const targetContentY = currentContentY + localCursorY - (viewHeight * 0.2)
      const clampedTarget = Math.max(0, targetContentY)

      if (Math.abs(clampedTarget - currentContentY) > 2) {
        scrollAnimation.stop()
        scrollAnimation.from = documentView.contentY
        scrollAnimation.to = clampedTarget
        scrollAnimation.start()
      }
    }
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

      topMargin: 120
      bottomMargin: 160
    }

    model: root.model
    spacing: 0
    clip: false
    // Keep the command menu anchored while it is open, and disable normal list scrolling while dragging
    interactive: root.activeCommandDelegate === null && !root.isDraggingBlock

    NumberAnimation {
      id: scrollAnimation
      target: documentView
      property: "contentY"
      duration: 180
      easing.type: Easing.OutCubic
    }

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
      readonly property var rendererItem: renderer

      readonly property bool isBeingDragged: root.draggingBlockId === delegateRoot.blockId
      readonly property bool isHovered: blockHoverArea.containsMouse || handleHover.hovered

      // MouseArea covering the block AND extending 48px into the left gutter (across the drag handle and beyond)
      MouseArea {
        id: blockHoverArea
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: -48
        hoverEnabled: true
        preventStealing: false
        propagateComposedEvents: true
        z: 10

        // Allow click and press events to pass right through to the text editor, links, or handles
        onPressed: function(mouse) { mouse.accepted = false }
        onReleased: function(mouse) { mouse.accepted = false }
        onClicked: function(mouse) { mouse.accepted = false }
        onDoubleClicked: function(mouse) { mouse.accepted = false }
      }

      onIsHoveredChanged: {
        if (isHovered) {
          root.hoveredBlockId = delegateRoot.blockId
        } else if (root.hoveredBlockId === delegateRoot.blockId) {
          root.hoveredBlockId = ""
        }
      }

      // Dynamic vertical shift to make room for dragged block
      property real visualDisplacement: {
        if (!root.isDraggingBlock || isBeingDragged)
          return 0
        const from = root.draggingSourceIndex
        const to = root.dropTargetIndex
        if (from < to) {
          // Dragged downwards: items between (from, to] shift UP by draggingItemHeight
          if (delegateRoot.index > from && delegateRoot.index <= to)
            return -root.draggingItemHeight
        } else if (from > to) {
          // Dragged upwards: items between [to, from) shift DOWN by draggingItemHeight
          if (delegateRoot.index >= to && delegateRoot.index < from)
            return root.draggingItemHeight
        }
        return 0
      }

      width: documentView.width
      height: renderer.implicitHeight + (delegateRoot.isCompactList ? 0 : 5)
      z: isBeingDragged ? 100 : 1

      transform: Translate {
        y: delegateRoot.isBeingDragged ? root.draggingVisualY : delegateRoot.visualDisplacement
        Behavior on y {
          enabled: !delegateRoot.isBeingDragged
          NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
        }
      }

      opacity: isBeingDragged ? 0.85 : 1.0
      Behavior on opacity {
        NumberAnimation { duration: 120 }
      }

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
        showPlaceholder: delegateRoot.index === 0 && root.model.count === 1
        searchHighlights: root.searchHighlightsFor(delegateRoot.blockId)
        searchNavigationActive: root.searchNavigationActive

        onCaretMoved: function(position, cursorGlobalY, cursorHeight) {
          root.caretBlockId = delegateRoot.blockId
          root.caretPosition = position
          root.ensureCursorVisible(cursorGlobalY, cursorHeight)
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

        onCommandMenuRequested: function(query, xPosition, yPosition, cursorHeight) {
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
            commandMenu.openFor(query)

            // xPosition and yPosition are already in root coordinates of renderer
            const menuPosition = renderer.mapToItem(
              root,
              xPosition,
              yPosition
            )

            const gap = root.commandMenuGap
            const actualCursorHeight = cursorHeight > 0 ? cursorHeight : 20
            const availableBottom = root.height - root.bottomReservedSpace - 1
            const belowY = menuPosition.y + actualCursorHeight + gap
            const aboveY = menuPosition.y - gap - commandMenu.height

            let flippedAbove = false
            let menuY = belowY

            // Default to below. Flip above when not enough space below to show the full height,
            // and there is enough space above.
            if (belowY + commandMenu.height > availableBottom && aboveY >= 0) {
              menuY = aboveY
              flippedAbove = true
            }

            // Constrain horizontally within visible editor bounds
            commandMenu.x = Math.max(
              8,
              Math.min(root.width - commandMenu.width - 8, menuPosition.x)
            )

            root.commandMenuAbove = flippedAbove
            root.commandMenuAnchorY = menuPosition.y
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

        onSelectionChanged: function(hasSelection, xPosition, yPosition, cursorHeight) {
          if (!hasSelection) {
            selectionTimer.stop()
            root.pendingSelection = null
            if (root.activeSelectionDelegate === delegateRoot && !selectionToolbar.hasActivePopup) {
              selectionToolbar.visible = false
              root.activeSelectionDelegate = null
            }
            return
          }

          // If toolbar is currently visible, hide it while actively selecting/adjusting
          if (root.activeSelectionDelegate !== delegateRoot) {
            selectionToolbar.visible = false
          }

          root.pendingSelection = {
            delegate: delegateRoot,
            renderer: renderer,
            x: xPosition,
            y: yPosition,
            cursorHeight: cursorHeight
          }

          selectionTimer.restart()
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

      // Drag handle on the left margin
      Item {
        id: dragHandle
        width: 18
        height: 24
        x: -28
        y: 4
        z: 20

        // Purely mouse-hover driven: show only on the block the mouse is hovering over
        property bool shouldShow: {
          if (root.isDraggingBlock) return false
          if (handleArea.pressed) return true
          return root.hoveredBlockId === delegateRoot.blockId
        }

        Timer {
          id: showTimer
          interval: 200
          repeat: false
          running: dragHandle.shouldShow && dragHandle.opacity === 0
          onTriggered: {
            if (dragHandle.shouldShow) {
              dragHandle.opacity = 1.0
            }
          }
        }

        onShouldShowChanged: {
          if (!shouldShow) {
            showTimer.stop()
            dragHandle.opacity = 0.0
          }
        }

        opacity: 0.0
        Behavior on opacity {
          NumberAnimation { duration: 250; easing.type: Easing.InOutQuad }
        }

        Rectangle {
          anchors.fill: parent
          radius: 4
          color: handleArea.pressed ? Theme.surfaceAlt : (handleHover.hovered ? Theme.surfaceHover : "transparent")
        }

        IconImage {
          anchors.centerIn: parent
          width: 16
          height: 16
          source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_re_order_dots_vertical_20_regular.svg"
          color: Theme.textMuted
        }

        HoverHandler {
          id: handleHover
        }

        MouseArea {
          id: handleArea
          anchors.fill: parent
          cursorShape: Qt.OpenHandCursor
          preventStealing: true

          property real startGlobalY: 0

          Timer {
            id: holdTimer
            interval: 200
            repeat: false
            onTriggered: {
              if (handleArea.pressed) {
                root.draggingBlockId = delegateRoot.blockId
                root.draggingSourceIndex = delegateRoot.index
                root.dropTargetIndex = delegateRoot.index
                root.draggingVisualY = 0
                root.draggingItemHeight = delegateRoot.height
                root.dragGlobalY = handleArea.startGlobalY
                handleArea.cursorShape = Qt.ClosedHandCursor
              }
            }
          }

          onPressed: function(mouse) {
            startGlobalY = mapToItem(null, mouse.x, mouse.y).y
            root.dragGlobalY = startGlobalY
            holdTimer.restart()
          }

          onPositionChanged: function(mouse) {
            const currentGlobalY = mapToItem(null, mouse.x, mouse.y).y
            root.dragGlobalY = currentGlobalY

            if (!root.isDraggingBlock) return

            root.draggingVisualY = currentGlobalY - startGlobalY
            root.updateDragTarget()
          }

          onReleased: function(mouse) {
            holdTimer.stop()
            handleArea.cursorShape = Qt.OpenHandCursor

            if (root.isDraggingBlock) {
              const src = root.draggingSourceIndex
              const dst = root.dropTargetIndex
              const blockId = root.draggingBlockId

              root.draggingBlockId = ""
              root.draggingSourceIndex = -1
              root.dropTargetIndex = -1
              root.draggingVisualY = 0

              if (src !== dst && src >= 0 && dst >= 0) {
                root.model.moveBlock(blockId, dst)
              }
            }
          }

          onCanceled: {
            holdTimer.stop()
            handleArea.cursorShape = Qt.OpenHandCursor
            root.draggingBlockId = ""
            root.draggingSourceIndex = -1
            root.dropTargetIndex = -1
            root.draggingVisualY = 0
          }
        }
      }
    }

    footer: Item {
      width: documentView.width
      height: documentView.height * 0.8
    }

    // Drop indicator line (rendered inside contentItem to correctly track content scrolling)
    Rectangle {
      id: dropIndicator
      parent: documentView.contentItem
      width: documentView.width
      height: 2
      radius: 1
      color: Theme.accent
      visible: root.isDraggingBlock && root.dropTargetIndex >= 0
      z: 200

      // Compute Y position in contentItem coordinates
      y: {
        if (!visible) return 0
        const from = root.draggingSourceIndex
        const to = root.dropTargetIndex
        const targetItem = documentView.itemAtIndex(to)
        if (!targetItem) return 0

        if (from < to) {
          // Dragged downward: candidate drop position is right below targetItem (accounting for its upward shift)
          return targetItem.y + targetItem.height + targetItem.visualDisplacement
        } else if (from > to) {
          // Dragged upward: candidate drop position is right above targetItem (accounting for its downward shift)
          return targetItem.y + targetItem.visualDisplacement
        } else {
          // At original position: show at top of source item
          return targetItem.y
        }
      }

      Behavior on y {
        NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
      }
    }

    Component.onCompleted: {
      console.log("Editor initialized")
      console.log("Model count:", root.model.count)
    }
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

  SelectionToolbar {
    id: selectionToolbar

    function syncFormattingState() {
      if (!root.activeSelectionDelegate) return
      const renderer = root.activeSelectionDelegate.rendererItem
      if (renderer) {
        selectionToolbar.isBold = renderer.isFormatActive("bold")
        selectionToolbar.isItalic = renderer.isFormatActive("italic")
        selectionToolbar.isUnderline = renderer.isFormatActive("underline")
        selectionToolbar.isStrikethrough = renderer.isFormatActive("strike")
      }
    }

    onBoldToggled: {
      if (root.activeSelectionDelegate && root.activeSelectionDelegate.rendererItem) {
        root.activeSelectionDelegate.rendererItem.toggleFormat("bold")
        syncFormattingState()
      }
    }

    onItalicToggled: {
      if (root.activeSelectionDelegate && root.activeSelectionDelegate.rendererItem) {
        root.activeSelectionDelegate.rendererItem.toggleFormat("italic")
        syncFormattingState()
      }
    }

    onUnderlineToggled: {
      if (root.activeSelectionDelegate && root.activeSelectionDelegate.rendererItem) {
        root.activeSelectionDelegate.rendererItem.toggleFormat("underline")
        syncFormattingState()
      }
    }

    onStrikethroughToggled: {
      if (root.activeSelectionDelegate && root.activeSelectionDelegate.rendererItem) {
        root.activeSelectionDelegate.rendererItem.toggleFormat("strike")
        syncFormattingState()
      }
    }

    onBlockTypeSelected: function(type, level) {
      if (root.activeSelectionDelegate) {
        const id = root.activeSelectionDelegate.blockId
        const idx = root.activeSelectionDelegate.index
        if (root.model.changeBlockType(id, type, level)) {
          root.focusDelegateAt(idx, -1)
        }
      }
    }
  }
}
