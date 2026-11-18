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

  // Focus is applied once the target delegate exists (or is reused), instead
  // of polling/forcing synchronous layouts.
  property string pendingFocusBlockId: ""
  property int pendingFocusPosition: 0
  property int pendingFocusRangeStart: -1
  property int pendingFocusRangeEnd: -1

  Timer {
    id: pendingFocusTimer
    interval: 0
    repeat: false
    onTriggered: root.applyPendingFocus()
  }

  // Drag and drop reordering state
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

    const count = root.model.count
    if (count <= 0) return

    const contentY = documentView.mapFromItem(null, 0, root.dragGlobalY).y
                     + documentView.contentY

    // Insertion point: the row the dragged block should be placed before.
    // indexAt is O(1) and avoids measuring every row on long documents.
    let insertion = documentView.indexAt(documentView.width / 2, contentY)

    if (insertion < 0)
      insertion = contentY < 0 ? 0 : count
    else {
      const item = documentView.itemAtIndex(insertion)

      if (item && contentY > item.y + item.height / 2)
        ++insertion
    }

    // moveBlock() takes the final index after the source is removed. For a
    // downward move that is one slot before the raw insertion point; without
    // this correction the block lands one row too low.
    const target = insertion > root.draggingSourceIndex
                 ? insertion - 1
                 : insertion

    root.dropTargetIndex = Math.max(0, Math.min(target, count - 1))
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
      selectionToolbar.isSubscript = renderer.isFormatActive("subscript")
      selectionToolbar.isSuperscript = renderer.isFormatActive("superscript")
      selectionToolbar.alignment = delegateRoot.alignment || "left"

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
      selectionToolbar.showToolbar = true
    }
  }

  // Search state. The find bar owns the query and passes the complete match
  // list here; the delegates only render the matches for their own block.
  property var searchMatches: []
  property int activeSearchMatch: -1
  property bool searchActive: false

  // blockId -> [{ start, length, active }]. Built once per search update so a
  // delegate only reads its own slice instead of scanning every match.
  property var searchHighlightsByBlock: ({})

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

  function rebuildSearchHighlightMap() {
    const byBlock = ({})
    const active = root.activeSearchMatch >= 0
                   && root.activeSearchMatch < root.searchMatches.length
                 ? root.searchMatches[root.activeSearchMatch]
                 : null

    for (let i = 0; i < root.searchMatches.length; ++i) {
      const match = root.searchMatches[i]
      let list = byBlock[match.blockId]

      if (!list) {
        list = []
        byBlock[match.blockId] = list
      }

      list.push({
        start: match.start,
        length: match.length,
        active: active !== null
                && active.blockId === match.blockId
                && active.start === match.start
                && active.length === match.length
      })
    }

    root.searchHighlightsByBlock = byBlock
  }

  function setSearchMatches(matches, activeIndex) {
    root.searchMatches = matches
    root.activeSearchMatch = activeIndex
    root.rebuildSearchHighlightMap()

    if (activeIndex >= 0 && activeIndex < matches.length)
      root.scrollToSearchMatch(matches[activeIndex])
  }

  function clearSearch() {
    root.searchMatches = []
    root.activeSearchMatch = -1
    root.searchHighlightsByBlock = ({})
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
    root.rebuildSearchHighlightMap()
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

    documentView.positionViewAtIndex(row, ListView.Contain)
  }

  // Enter on the find bar: move the caret into the matched block and select
  // the matched range so it stays visible after the highlights are cleared.
  function focusSearchMatch(match) {
    if (root.model.indexOfBlock(match.blockId) < 0)
      return

    root.requestFocusBlock(match.blockId, match.start,
                           match.start, match.start + match.length)
  }

  // Sets a focus intent. The delegate applies it when it exists, so a model
  // reset or delegate reuse never leaves the caret in the wrong block.
  function requestFocusBlock(blockId, position, rangeStart, rangeEnd) {
    if (!blockId || blockId.length === 0)
      return

    root.pendingFocusBlockId = blockId
    root.pendingFocusPosition = position
    root.pendingFocusRangeStart = rangeStart === undefined ? -1 : rangeStart
    root.pendingFocusRangeEnd = rangeEnd === undefined ? -1 : rangeEnd

    const row = root.model.indexOfBlock(blockId)

    if (row >= 0)
      documentView.positionViewAtIndex(row, ListView.Contain)

    pendingFocusTimer.restart()
  }

  function applyPendingFocus() {
    if (root.pendingFocusBlockId.length === 0)
      return

    const row = root.model.indexOfBlock(root.pendingFocusBlockId)

    if (row < 0) {
      root.pendingFocusBlockId = ""
      return
    }

    const delegate = documentView.itemAtIndex(row)

    // The delegate's Component.onCompleted/onBlockIdChanged applies the
    // intent once it materializes.
    if (!delegate)
      return

    const position = root.pendingFocusPosition
    const rangeStart = root.pendingFocusRangeStart
    const rangeEnd = root.pendingFocusRangeEnd

    root.pendingFocusBlockId = ""
    root.pendingFocusRangeStart = -1
    root.pendingFocusRangeEnd = -1

    delegate.refreshBlock()

    if (rangeStart >= 0)
      delegate.focusBlockAtRange(rangeStart, rangeEnd)
    else
      delegate.focusBlock(position)
  }

  function focusDelegateAt(row, position) {
    if (row < 0 || row >= root.model.count)
      return

    const blockId = root.model.blockIdAt(row)

    if (blockId.length > 0)
      root.requestFocusBlock(blockId, position)
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
    let row = root.model.indexOfBlock(blockId)

    // The undo/redo may have removed the focused block (for example when
    // undoing a split). Move to the nearest surviving block in that case.
    if (row < 0) {
      const fallback = undo ? oldRow - 1 : oldRow
      row = Math.max(0, Math.min(fallback, root.model.count - 1))
    }

    if (row < 0)
      return

    const targetId = root.model.blockIdAt(row)

    if (targetId.length > 0) {
      // A negative position means "end of text" (structural steps).
      root.requestFocusBlock(targetId, cursorPosition)
    }
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

  // Alt+Up / Alt+Down move the focused block, mirroring drag reordering from
  // the keyboard. The caret stays at the same offset inside the block and the
  // list animates the swap with the same timing as the drag displacement.
  property bool keyboardMoveAnimating: false

  Timer {
    id: keyboardMoveTimer
    interval: 160
    repeat: false
    onTriggered: root.keyboardMoveAnimating = false
  }

  function moveFocusedBlock(direction) {
    if (root.caretBlockId.length === 0 || root.isDraggingBlock
        || root.keyboardMoveAnimating)
      return

    const row = root.model.indexOfBlock(root.caretBlockId)

    if (row < 0)
      return

    const target = Math.max(0, Math.min(row + direction,
                                        root.model.count - 1))

    if (target === row)
      return

    const position = root.caretPosition

    // The view's move transitions are enabled only for this short window so
    // drag-and-drop keeps its existing behaviour.
    root.keyboardMoveAnimating = true

    if (root.model.moveBlock(root.caretBlockId, target)) {
      keyboardMoveTimer.restart()
      root.requestFocusBlock(root.caretBlockId, position)
    } else {
      root.keyboardMoveAnimating = false
    }
  }

  Shortcut {
    sequence: "Alt+Up"
    context: Qt.ApplicationShortcut
    enabled: !root.isDraggingBlock && !root.keyboardMoveAnimating
    onActivated: root.moveFocusedBlock(-1)
  }

  Shortcut {
    sequence: "Alt+Down"
    context: Qt.ApplicationShortcut
    enabled: !root.isDraggingBlock && !root.keyboardMoveAnimating
    onActivated: root.moveFocusedBlock(1)
  }

  Rectangle {
    anchors.fill: parent
    color: Theme.background
  }

  // GPU-accelerated tactile paper sheet backing the document or entire workspace
  PaperSheet {
    id: paperCanvas

    readonly property bool fullWorkspace: Preferences.paperOverWorkspace

    radius: fullWorkspace ? 0 : 12
    showShadow: !fullWorkspace
    baseColor: fullWorkspace ? Theme.background : Theme.surface

    anchors {
      fill: fullWorkspace ? parent : undefined
      top: fullWorkspace ? undefined : parent.top
      bottom: fullWorkspace ? undefined : parent.bottom
      horizontalCenter: fullWorkspace ? undefined : parent.horizontalCenter
      topMargin: fullWorkspace ? 0 : 40
      bottomMargin: fullWorkspace ? 0 : 40
    }
    width: fullWorkspace ? parent.width : Math.min(root.contentWidth + 64, parent.width - 24)

    Behavior on radius {
      NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }
  }

  // Document content

  // Clicks in the margin left of the document are still visually part of the
  // block gutter. Focus the block at that height so clicking beside a block
  // does not leave the caret in the previous one.
  MouseArea {
    id: leftMarginClickArea

    width: documentView.x
    anchors {
      top: documentView.top
      bottom: documentView.bottom
      left: parent.left
    }

    acceptedButtons: Qt.LeftButton

    onClicked: function(mouse) {
      const point = documentView.mapFromItem(leftMarginClickArea, mouse.x,
                                             mouse.y)
      const contentY = point.y + documentView.contentY
      let row = documentView.indexAt(documentView.width / 2, contentY)

      if (row < 0 && root.model)
        row = contentY < 0 ? 0 : root.model.count - 1

      if (row >= 0 && root.model && row < root.model.count)
        root.focusDelegateAt(row, 0)
    }
  }

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
    // Recycle delegates instead of rebuilding the whole TextArea stack while
    // scrolling, and materialize a little ahead of the viewport.
    reuseItems: true
    cacheBuffer: 400

    // Keyboard reordering glides with the same 150 ms OutCubic feel as the
    // drag displacement. Enabled only while moveFocusedBlock() runs so drop
    // handling is unchanged.
    move: Transition {
      enabled: root.keyboardMoveAnimating
      NumberAnimation {
        properties: "y"
        duration: 150
        easing.type: Easing.OutCubic
      }
    }

    moveDisplaced: Transition {
      enabled: root.keyboardMoveAnimating
      NumberAnimation {
        properties: "y"
        duration: 150
        easing.type: Easing.OutCubic
      }
    }
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
      property Item editorRoot: root

      // These properties receive the C++ model roles.
      required property string blockId
      required property string content
      required property int blockType
      required property int headingLevel
      required property int nextBlockType
      required property int numberIndex
      required property string alignment
      required property bool checked
      required property string calloutType
      required property int index

      readonly property real bottomGap: BlockSpacing.bottomGap(
        delegateRoot.blockType,
        delegateRoot.nextBlockType,
        delegateRoot.index === root.model.count - 1
      )
      readonly property var rendererItem: renderer

      readonly property bool isBeingDragged: root.draggingBlockId === delegateRoot.blockId
      readonly property bool isHovered: blockHoverArea.containsMouse || handleHover.hovered

      // Applies a focus intent set before this delegate existed (or while it
      // was recycled for another block).
      function maybeApplyPendingFocus() {
        if (editorRoot.pendingFocusBlockId.length === 0
            || editorRoot.pendingFocusBlockId !== delegateRoot.blockId)
          return

        const position = editorRoot.pendingFocusPosition
        const rangeStart = editorRoot.pendingFocusRangeStart
        const rangeEnd = editorRoot.pendingFocusRangeEnd

        editorRoot.pendingFocusBlockId = ""
        editorRoot.pendingFocusRangeStart = -1
        editorRoot.pendingFocusRangeEnd = -1

        refreshBlock()

        if (rangeStart >= 0)
          focusBlockAtRange(rangeStart, rangeEnd)
        else
          focusBlock(position)
      }

      Timer {
        id: delegateFocusTimer
        interval: 0
        repeat: false
        onTriggered: delegateRoot.maybeApplyPendingFocus()
      }

      Component.onCompleted: delegateFocusTimer.restart()
      onBlockIdChanged: delegateFocusTimer.restart()

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
      height: renderer.implicitHeight + delegateRoot.bottomGap
      z: isBeingDragged ? 100 : 1

      // Visualization overlay to inspect delegate bounding box & focus state
      Rectangle {
        anchors.fill: parent
        visible: Preferences.showBlockDebugBorders
        color: renderer.isFocused ? Qt.rgba(0.2, 0.6, 1.0, 0.15) : Qt.rgba(1.0, 0.5, 0.0, 0.08)
        border.color: renderer.isFocused ? "#3b82f6" : Qt.rgba(1.0, 0.5, 0.0, 0.4)
        border.width: 1
        z: -1

        // Highlight inner renderer area to show any bottom gap/margin
        Rectangle {
          x: 0
          y: 0
          width: parent.width
          height: renderer.implicitHeight
          color: "transparent"
          border.color: renderer.isFocused ? "#2563eb" : "#f97316"
          border.width: 1

          Text {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 2
            text: (renderer.isFocused ? "FOCUSED | " : "UNFOCUSED | ") + "h: " + Math.round(renderer.implicitHeight) + " / total: " + Math.round(delegateRoot.height)
            font.pixelSize: 10
            color: renderer.isFocused ? "#2563eb" : "#c2410c"
          }
        }
      }

      // Block Type and Index Badge for debugging
      Rectangle {
        visible: Preferences.showBlockTypeBadge
        z: 11
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: 2
        anchors.leftMargin: 2
        width: typeBadgeText.implicitWidth + 8
        height: 16
        radius: 3
        color: Qt.rgba(0, 0, 0, 0.75)

        Text {
          id: typeBadgeText
          anchors.centerIn: parent
          text: "#" + delegateRoot.index + " [" + delegateRoot.blockType + (delegateRoot.blockType === "heading" ? " h" + delegateRoot.headingLevel : "") + "] id:" + delegateRoot.blockId.substring(0, 6)
          color: "#38bdf8"
          font.pixelSize: 9
          font.family: "monospace"
          font.bold: true
        }
      }

      // Drop target boundary visualizer during drag-and-drop
      Rectangle {
        visible: Preferences.showDragDropBounds && root.isDraggingBlock && root.dropTargetIndex === delegateRoot.index && !delegateRoot.isBeingDragged
        z: 20
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 3
        color: "#10b981"

        Rectangle {
          anchors.verticalCenter: parent.verticalCenter
          anchors.left: parent.left
          width: 8
          height: 8
          radius: 4
          color: "#10b981"
        }
      }

      transform: Translate {
        y: delegateRoot.isBeingDragged ? root.draggingVisualY : delegateRoot.visualDisplacement
        Behavior on y {
          enabled: !delegateRoot.isBeingDragged
          NumberAnimation {
            duration: 220
            easing.type: Easing.OutBack
            easing.overshoot: 1.15
          }
        }
      }

      // Gentle scale-lift and opacity when grabbing to reorder
      scale: isBeingDragged ? 1.02 : 1.0
      opacity: isBeingDragged ? 0.90 : 1.0

      Behavior on scale {
        NumberAnimation {
          duration: 160
          easing.type: Easing.OutCubic
        }
      }

      Behavior on opacity {
        NumberAnimation { duration: 140 }
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
        if (root.model.changeBlockType(delegateRoot.blockId, command.type, command.level)) {
          if (command.type === 7 /* Divider */) {
            // Insert a new paragraph below the divider so typing can immediately continue
            const row = delegateRoot.index
            root.model.insertBlock(row + 1, 0, "")
            root.focusDelegateAt(row + 1, 0)
          } else {
            root.focusDelegateAt(delegateRoot.index, 0)
          }
        }
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
        alignment: delegateRoot.alignment
        isChecked: delegateRoot.checked
        calloutType: delegateRoot.calloutType || "note"
        showPlaceholder: delegateRoot.index === 0 && root.model.count === 1
        searchHighlights: root.searchHighlightsByBlock[delegateRoot.blockId] || []
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

        onFocusRequested: function(position) {
          root.focusDelegateAt(delegateRoot.index, position)
        }

        onFocusDelegateRequested: function(row, position) {
          root.focusDelegateAt(row, position)
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
              selectionToolbar.showToolbar = false
              root.activeSelectionDelegate = null
            }
            return
          }

          // If toolbar is currently visible, hide it while actively selecting/adjusting
          if (root.activeSelectionDelegate !== delegateRoot) {
            selectionToolbar.showToolbar = false
          }

          root.pendingSelection = {
            delegate: delegateRoot,
            renderer: renderer,
            x: xPosition,
            y: yPosition,
            cursorHeight: cursorHeight
          }

          if (selectionToolbar.showToolbar && root.activeSelectionDelegate === delegateRoot) {
            selectionTimer.stop()
            selectionTimer.triggered()
          } else {
            selectionTimer.restart()
          }
        }

        onEnterPressed: {
          if (delegateRoot.blockType === 7 /* Divider */) {
            const nextRow = delegateRoot.index + 1
            root.model.insertBlock(nextRow, 0, "")
            root.focusDelegateAt(nextRow, 0)
            return
          }

          const currentText = renderer.editorText
          const cursorPosition = renderer.cursorPosition
          
          // for a smoother markdown behaviour:
          // Check if block is empty. For Callout (type 10), check if content is empty ignoring prefix.
          const isCallout = delegateRoot.blockType === 10
          const isCleanEmpty = isCallout
                               ? ((renderer.block && "cleanBody" in renderer.block) ? renderer.block.cleanBody.trim().length === 0 : (currentText.trim() === "" || currentText.indexOf("\n") < 0))
                               : (currentText.length === 0)

          if ((delegateRoot.blockType === 2 || delegateRoot.blockType === 3 || delegateRoot.blockType === 4 || delegateRoot.blockType === 5 || delegateRoot.blockType === 10) && isCleanEmpty) {
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

          // The new block's delegate may not exist yet; the focus intent is
          // applied as soon as it is created.
          root.requestFocusBlock(newId, 0)
        }

        onBackspacePressed: {
          const currentIndex = delegateRoot.index
          const currentText = renderer.editorText
          const currentType = delegateRoot.blockType

          if (currentType === 7 /* Divider */) {
            if (root.model.removeBlock(delegateRoot.blockId)) {
              root.focusDelegateAt(Math.max(0, currentIndex - 1), -1)
            }
            return
          }

          // If this is a specialized block (Quote, List, Todo, Callout), pressing backspace
          // at cursor position 0 should demote it to a Paragraph first instead of
          // immediately merging into the previous block and deleting the current block!
          if (currentType !== 0) {
            if (root.model.changeBlockType(delegateRoot.blockId, 0, 0)) {
              root.focusDelegateAt(currentIndex, 0)
            }
            return
          }

          if (currentIndex === 0)
            return

          if (currentText.length === 0) {
            if (root.model.removeBlock(delegateRoot.blockId)) {
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
        y: Math.max(0, Math.round((renderer.firstLineHeight - height) / 2))
        z: 20

        // Purely mouse-hover driven: show only on the block the mouse is hovering over
        property bool shouldShow: {
          if (root.isDraggingBlock) return false
          if (handleArea.pressed) return true
          return delegateRoot.isHovered
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
        selectionToolbar.isSubscript = renderer.isFormatActive("subscript")
        selectionToolbar.isSuperscript = renderer.isFormatActive("superscript")
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

    onSubscriptToggled: {
      if (root.activeSelectionDelegate && root.activeSelectionDelegate.rendererItem) {
        root.activeSelectionDelegate.rendererItem.toggleScriptFormat("subscript")
        syncFormattingState()
      }
    }

    onSuperscriptToggled: {
      if (root.activeSelectionDelegate && root.activeSelectionDelegate.rendererItem) {
        root.activeSelectionDelegate.rendererItem.toggleScriptFormat("superscript")
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

    onAlignmentSelected: function(align) {
      if (root.activeSelectionDelegate) {
        const id = root.activeSelectionDelegate.blockId
        root.model.setBlockAlignment(id, align)
        selectionToolbar.alignment = align
        if (root.activeSelectionDelegate.rendererItem) {
          root.activeSelectionDelegate.rendererItem.notifySelection()
        }
      }
    }
  }
}
