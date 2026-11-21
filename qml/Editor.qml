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
  // Block currently under the pointer in the left gutter. Drives gutter-handle
  // visibility and the editor-level gutter menu, independent of delegate-local
  // hover handlers (which the block hover area can swallow).
  property string gutterHoverBlockId: ""

  // Paper sheet width always stays at standard 800px column (+ 64px padding) so
  // full width block mode does not alter paper/wallpaper sheet boundaries.
  readonly property real sheetWidth: 800

  // Focus is applied once the target delegate exists (or is reused), instead
  // of polling/forcing synchronous layouts.
  property string pendingFocusBlockId: ""
  property int pendingFocusPosition: 0
  property int pendingFocusRangeStart: -1
  property int pendingFocusRangeEnd: -1
  property real pendingFocusX: -1
  property int pendingFocusAttempts: 0

  Timer {
    id: pendingFocusTimer
    interval: 0
    repeat: false
    onTriggered: root.applyPendingFocus()
  }

  // ListView may still report the pre-insert order for a frame after a block
  // is inserted, so a focus request can land on a stale delegate. Retry until
  // the row at the requested id is the delegate that is actually focused.
  Timer {
    id: pendingFocusRetryTimer
    interval: 16
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
  // Set while a top-level block is dragged over a column pane.
  property var blockDropIntoColumn: null

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

      if (viewPos.y < topEdge) {
        const factor = Math.max(0, Math.min(1, (topEdge - viewPos.y) / topEdge))
        const step = (factor * 12) + 2
        documentView.contentY = documentView.clampY(documentView.contentY - step)
        root.updateDragTarget()
      } else if (viewPos.y > bottomEdge) {
        const factor = Math.max(0, Math.min(1, (viewPos.y - bottomEdge) / 40))
        const step = (factor * 12) + 2
        documentView.contentY = documentView.clampY(documentView.contentY + step)
        root.updateDragTarget()
      }
    }
  }

  function updateDragTarget() {
    if (!root.isDraggingBlock) return

    const count = root.model.visibleCount()
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

  // Set by the host while a full-window surface (settings, global search,
  // shortcuts, confirmations) covers the editor. Background scrolling must
  // stop so wheel events cannot move the document underneath those surfaces.
  property bool scrollingEnabled: true

  // Stop any native flick when a full-window surface covers the editor.
  onScrollingEnabledChanged: {
    if (!scrollingEnabled && documentView)
      documentView.cancelFlick()
  }

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
                ? root.model.visibleRowOf(root.caretBlockId)
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
    const row = root.model.visibleRowOf(match.blockId)

    if (row < 0)
      return

    documentView.positionViewAtIndex(row, ListView.Contain)
  }

  // Enter on the find bar: move the caret into the matched block and select
  // the matched range so it stays visible after the highlights are cleared.
  function focusSearchMatch(match) {
    if (root.model.visibleRowOf(match.blockId) < 0)
      return

    root.requestFocusBlock(match.blockId, match.start,
                           match.start, match.start + match.length)
  }

  // Sets a focus intent. The delegate applies it when it exists, so a model
  // reset or delegate reuse never leaves the caret in the wrong block.
  function requestFocusBlock(blockId, position, rangeStart, rangeEnd, xPosition) {
    if (!blockId || blockId.length === 0)
      return

    root.pendingFocusBlockId = blockId
    root.pendingFocusPosition = position
    root.pendingFocusRangeStart = rangeStart === undefined ? -1 : rangeStart
    root.pendingFocusRangeEnd = rangeEnd === undefined ? -1 : rangeEnd
    root.pendingFocusX = xPosition === undefined ? -1 : xPosition
    root.pendingFocusAttempts = 0

    const row = root.model.visibleRowOf(blockId)

    if (row >= 0)
      documentView.positionViewAtIndex(row, ListView.Contain)

    pendingFocusTimer.restart()
  }

  function applyPendingFocus() {
    if (root.pendingFocusBlockId.length === 0)
      return

    const row = root.model.visibleRowOf(root.pendingFocusBlockId)

    if (row < 0) {
      root.pendingFocusBlockId = ""
      return
    }

    const delegate = documentView.itemAtIndex(row)

    // Right after an insertion the view can still hand back the delegate that
    // occupied this row before the model changed. Do not clear the intent in
    // that case: retry once the view has caught up, otherwise the caret stays
    // in the old block (for example after Ctrl+Enter in a code block).
    if (!delegate) {
      if (root.pendingFocusAttempts < 30) {
        ++root.pendingFocusAttempts
        pendingFocusRetryTimer.restart()
      }
      return
    }

    const position = root.pendingFocusPosition
    const rangeStart = root.pendingFocusRangeStart
    const rangeEnd = root.pendingFocusRangeEnd
    const focusX = root.pendingFocusX

    // A focus request for a block inside a column lands on the layout's
    // visible delegate. Let it route the intent to the right inner delegate.
    if (delegate.blockId !== root.pendingFocusBlockId) {
      if (delegate.focusChildBlock
          && delegate.focusChildBlock(root.pendingFocusBlockId, position,
                                      rangeStart, rangeEnd, focusX)) {
        root.pendingFocusBlockId = ""
        root.pendingFocusAttempts = 0
        root.pendingFocusRangeStart = -1
        root.pendingFocusRangeEnd = -1
        root.pendingFocusX = -1
      } else if (root.pendingFocusAttempts < 30) {
        ++root.pendingFocusAttempts
        pendingFocusRetryTimer.restart()
      }
      return
    }

    // The delegate's Component.onCompleted/onBlockIdChanged applies the
    // intent once it materializes.
    root.pendingFocusBlockId = ""
    root.pendingFocusAttempts = 0
    root.pendingFocusRangeStart = -1
    root.pendingFocusRangeEnd = -1
    root.pendingFocusX = -1

    delegate.refreshBlock()

    if (focusX >= 0)
      delegate.focusBlockAtX(focusX)
    else if (rangeStart >= 0)
      delegate.focusBlockAtRange(rangeStart, rangeEnd)
    else
      delegate.focusBlock(position)
  }

  // Mapping helpers between the flat document rows and the visible rows shown
  // by documentView. Column children live in the document but are rendered by
  // their ColumnsBlock, so they share the visible row of their layout.
  function moveBlockToVisibleRow(blockId, visibleRow) {
    const sourceRow = root.model.sourceRowOfVisibleRow(visibleRow)
    return sourceRow >= 0 && root.model.moveBlock(blockId, sourceRow)
  }

  function sourceRowOfBlock(blockId) {
    return root.model.sourceRowOfBlock(blockId)
  }

  // Editable blocks are the ones that host a caret. Column/Columns wrappers and
  // dividers are skipped so arrow navigation walks the flat document in visual
  // order, including across column boundaries.
  function editableBlockAtSourceRow(row) {
    if (row < 0 || row >= root.model.count)
      return ""

    const type = root.model.blockTypeAtRow(row)

    if (type === 7 || type === 12 || type === 13)
      return ""

    return root.model.blockIdAt(row)
  }

  // Previous block that can host a caret. This walks the flat document instead
  // of the ListView: the previous block may be in a different column list (or
  // not instantiated yet), in which case itemAtIndex() returns null and a
  // backspace that removes the current block would leave no caret anywhere.
  function previousEditableBlockId(blockId) {
    const from = sourceRowOfBlock(blockId)

    if (from < 0)
      return ""

    for (let row = from - 1; row >= 0; --row) {
      const id = editableBlockAtSourceRow(row)

      if (id.length > 0)
        return id
    }

    return ""
  }

  function focusAdjacentEditable(blockId, direction, xPosition) {
    const from = sourceRowOfBlock(blockId)

    if (from < 0)
      return false

    let row = from + direction

    while (row >= 0 && row < root.model.count) {
      const id = editableBlockAtSourceRow(row)

      if (id.length > 0) {
        root.requestFocusBlock(id, -1, -1, -1, xPosition)
        return true
      }

      row += direction
    }

    return false
  }

  // Inserts an empty paragraph directly after a block and returns its id. The
  // new block inherits the source block's parentId, so inserting inside a
  // column keeps the paragraph in that column.
  function insertParagraphAfter(blockId) {
    return root.model.insertBlockAfter(blockId, 0, "")
  }

  // The drag API lives on the parent ColumnsBlock's renderer, not on the
  // nested block's own renderer. Resolve the visible layout delegate for a
  // child id and forward the gesture to it.
  property string nestedDragChildId: ""

  function nestedDragRenderer(childId) {
    if (!root.model || childId.length === 0)
      return null

    const columnId = root.model.parentBlockId(childId)
    const layoutId = root.model.parentBlockId(columnId)
    const row = root.model.visibleRowOf(layoutId)

    if (row < 0)
      return null

    const delegate = documentView.itemAtIndex(row)
    return delegate ? delegate.rendererItem : null
  }

  function beginNestedChildDrag(childId, globalX, globalY) {
    const rendererItem = nestedDragRenderer(childId)

    if (!rendererItem || typeof rendererItem.beginChildDrag !== "function")
      return false

    root.nestedDragChildId = childId
    return rendererItem.beginChildDrag(childId, globalX, globalY)
  }

  function updateNestedChildDrag(globalX, globalY) {
    const rendererItem = nestedDragRenderer(root.nestedDragChildId)

    if (rendererItem && typeof rendererItem.updateChildDrag === "function")
      rendererItem.updateChildDrag(globalX, globalY)
  }

  function endNestedChildDrag() {
    const rendererItem = nestedDragRenderer(root.nestedDragChildId)

    if (rendererItem && typeof rendererItem.endChildDrag === "function")
      rendererItem.endChildDrag()

    root.nestedDragChildId = ""
  }

  function cancelNestedChildDrag() {
    const rendererItem = nestedDragRenderer(root.nestedDragChildId)

    if (rendererItem && typeof rendererItem.cancelChildDrag === "function")
      rendererItem.cancelChildDrag()

    root.nestedDragChildId = ""
  }

  // Whole-layout drag, started from a column's gutter with Ctrl/Shift held.
  // It reuses the normal top-level block drag machinery.
  property real layoutDragStartY: 0

  function beginLayoutDrag(layoutId, globalX, globalY) {
    if (!root.model || layoutId.length === 0)
      return false

    const row = root.model.visibleRowOf(layoutId)

    if (row < 0)
      return false

    const delegate = documentView.itemAtIndex(row)
    root.draggingBlockId = layoutId
    root.draggingSourceIndex = row
    root.dropTargetIndex = row
    root.draggingVisualY = 0
    root.draggingItemHeight = delegate ? delegate.height : 0
    root.layoutDragStartY = globalY
    root.dragGlobalY = globalY
    root.updateDragTarget()
    return true
  }

  function updateLayoutDrag(globalX, globalY) {
    if (!root.isDraggingBlock)
      return

    root.dragGlobalY = globalY
    root.draggingVisualY = globalY - root.layoutDragStartY
    root.updateDragTarget()
  }

  function endLayoutDrag() {
    if (!root.isDraggingBlock)
      return

    const blockId = root.draggingBlockId
    const source = root.draggingSourceIndex
    const target = root.dropTargetIndex

    root.draggingBlockId = ""
    root.draggingSourceIndex = -1
    root.dropTargetIndex = -1
    root.draggingVisualY = 0

    if (source !== target && source >= 0 && target >= 0)
      root.moveBlockToVisibleRow(blockId, target)
  }

  function cancelLayoutDrag() {
    root.draggingBlockId = ""
    root.draggingSourceIndex = -1
    root.dropTargetIndex = -1
    root.draggingVisualY = 0
  }

  // Resolve which block renderer sits under a global point. Column layouts
  // occupy one visible row but span the whole writing width, so hit-testing
  // starts from the editor row and then asks that renderer about its panes.
  function rendererAtGlobalPoint(globalX, globalY) {
    if (!root.model)
      return null

    const viewPos = documentView.mapFromItem(null, globalX, globalY)

    if (viewPos.y < 0 || viewPos.y > documentView.height)
      return null

    const contentY = viewPos.y + documentView.contentY
    const row = documentView.indexAt(0, contentY)

    if (row < 0 || row >= root.model.visibleCount())
      return null

    const delegate = documentView.itemAtIndex(row)
    return delegate ? delegate.rendererItem : null
  }

  // Target for a dragged column: another column pane, or a position among the
  // editor's top-level blocks.
  function resolveColumnDrop(globalX, globalY) {
    if (!root.model)
      return { kind: "none" }

    const viewPos = documentView.mapFromItem(null, globalX, globalY)
    const rendererItem = rendererAtGlobalPoint(globalX, globalY)

    if (rendererItem && typeof rendererItem.columnAtPoint === "function") {
      const columnId = rendererItem.columnAtPoint(globalX, globalY)

      if (columnId.length > 0)
        return { kind: "column", columnId: columnId }
    }

    if (viewPos.y < 0 || viewPos.y > documentView.height)
      return { kind: "none" }

    const contentY = viewPos.y + documentView.contentY
    const row = documentView.indexAt(0, contentY)

    if (row < 0 || row >= root.model.visibleCount())
      return { kind: "editor", visibleRow: root.model.visibleCount() }

    return { kind: "editor", visibleRow: row }
  }

  // Target for a normal editor block being dropped into a column.
  function resolveBlockDropIntoColumn(globalX, globalY) {
    const rendererItem = rendererAtGlobalPoint(globalX, globalY)

    if (!rendererItem || typeof rendererItem.columnAtPoint !== "function")
      return null

    const columnId = rendererItem.columnAtPoint(globalX, globalY)

    if (columnId.length === 0)
      return null

    return { columnId: columnId, row: rendererItem.childRowAtPoint(globalX, globalY) }
  }

  function focusDelegateAt(row, position) {
    if (row < 0 || row >= root.model.visibleCount())
      return

    const blockId = root.model.blockIdAtVisibleRow(row)

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
    // or goes below it, jump immediately so the cursor is centered.
    const triggerThreshold = viewHeight * 0.65
    if (localCursorY + cursorHeight > triggerThreshold) {
      documentView.contentY = documentView.clampY(
        documentView.contentY + localCursorY - (viewHeight / 2))
    } else if (localCursorY < 0) {
      // If cursor is scrolled above the view (e.g. arrow up), scroll up to keep it visible
      documentView.contentY = documentView.clampY(
        documentView.contentY + localCursorY - (viewHeight * 0.2))
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
    let row = root.model.visibleRowOf(blockId)

    // The undo/redo may have removed the focused block (for example when
    // undoing a split). Move to the nearest surviving block in that case.
    if (row < 0) {
      const fallback = undo ? oldRow - 1 : oldRow
      row = Math.max(0, Math.min(fallback, root.model.visibleCount() - 1))
    }

    if (row < 0)
      return

    const targetId = root.model.blockIdAtVisibleRow(row)

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

    const row = root.model.visibleRowOf(root.caretBlockId)

    if (row < 0)
      return

    const target = Math.max(0, Math.min(row + direction,
                                        root.model.visibleCount() - 1))

    if (target === row)
      return

    const position = root.caretPosition

    // The view's move transitions are enabled only for this short window so
    // drag-and-drop keeps its existing behaviour.
    root.keyboardMoveAnimating = true

    if (root.moveBlockToVisibleRow(root.caretBlockId, target)) {
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
    width: fullWorkspace ? parent.width : Math.min(root.sheetWidth + 64, parent.width - 24)

    Behavior on radius {
      NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }
  }

  // Document content

  // Clicks in the margin left of the document are still visually part of the
  // block gutter. Focus the block at that height so clicking beside a block
  // does not leave the caret in the previous one. The area extends all the way
  // to the editor's left edge because delegate-local MouseAreas (including the
  // drag handle) sit outside the ListView's hit-test bounds and never see
  // clicks in this strip.
  // Hold-to-drag from the gutter handle. The handle is outside the ListView's
  // hit-test bounds, so the editor margin owns this gesture too.
  property string gutterPressBlockId: ""
  property real gutterPressGlobalY: 0
  property real gutterPressGlobalX: 0
  property bool gutterWasDragging: false
  property bool gutterPressIsColumns: false
  property bool gutterChildDragStarted: false
  property real gutterHoverGlobalX: -1
  property real gutterHoverGlobalY: -1

  // A Columns row has no gutter of its own in the editor margin: its
  // leftmost column's handle sits there instead.
  function isColumnsRow(row) {
    if (!root.model || row < 0 || row >= root.model.visibleCount())
      return false

    const delegate = documentView.itemAtIndex(row)
    return !!delegate && delegate.blockType === 12
  }

  function firstChildOfLayout(layoutId) {
    if (!root.model || layoutId.length === 0)
      return ""

    const columns = root.model.columnIdsOf(layoutId)

    if (columns.length === 0)
      return ""

    const children = root.model.blocksOfColumn(columns[0])
    return children.length > 0 ? children[0] : ""
  }

  Timer {
    id: gutterHoldTimer
    interval: 200
    repeat: false

    onTriggered: {
      if (!leftMarginClickArea.pressed || root.gutterPressBlockId.length === 0)
        return

      if (root.gutterPressIsColumns) {
        const childId = root.firstChildOfLayout(root.gutterPressBlockId)

        if (childId.length > 0
            && root.beginNestedChildDrag(childId,
                                         root.gutterPressGlobalX,
                                         root.gutterPressGlobalY)) {
          root.gutterChildDragStarted = true
          root.gutterWasDragging = true
        }
        return
      }

      const row = root.model.visibleRowOf(root.gutterPressBlockId)

      if (row < 0)
        return

      const item = documentView.itemAtIndex(row)
      root.draggingBlockId = root.gutterPressBlockId
      root.draggingSourceIndex = row
      root.dropTargetIndex = row
      root.draggingVisualY = 0
      root.draggingItemHeight = item ? item.height : 40
      root.dragGlobalY = root.gutterPressGlobalY
      root.gutterWasDragging = true
    }
  }

  MouseArea {
    id: leftMarginClickArea

    width: Math.max(0, documentView.x)
    anchors {
      top: documentView.top
      bottom: documentView.bottom
      left: parent.left
    }

    acceptedButtons: Qt.LeftButton
    hoverEnabled: true
    // Keep receiving motion after a gutter drag leaves the narrow margin,
    // but behave normally for plain clicks.
    preventStealing: root.gutterChildDragStarted || root.isDraggingBlock

    function rowAt(y) {
      if (!root.model)
        return -1

      const point = documentView.mapFromItem(leftMarginClickArea, 0, y)
      const contentY = point.y + documentView.contentY
      let row = documentView.indexAt(documentView.width / 2, contentY)

      if (row < 0)
        row = contentY < 0 ? 0 : root.model.visibleCount() - 1

      return row >= 0 && row < root.model.visibleCount() ? row : -1
    }

    function blockIdAt(y) {
      const row = rowAt(y)
      return row >= 0 ? root.model.blockIdAtVisibleRow(row) : ""
    }

    // The handle sits outside the ListView's hit-test bounds, so the editor
    // margin owns its hover state, menu and drag gesture instead of the delegate.
    onPositionChanged: function(mouse) {
      const hoverRow = rowAt(mouse.y)
      const global = mapToItem(null, mouse.x, mouse.y)
      root.gutterHoverGlobalX = global.x
      root.gutterHoverGlobalY = global.y
      root.gutterHoverBlockId = hoverRow >= 0
                                 ? root.model.blockIdAtVisibleRow(hoverRow) : ""

      if (root.gutterChildDragStarted) {
        root.updateNestedChildDrag(global.x, global.y)
        return
      }

      if (root.isDraggingBlock) {
        root.dragGlobalY = global.y
        root.draggingVisualY = root.dragGlobalY - root.gutterPressGlobalY
        root.updateDragTarget()
        root.blockDropIntoColumn =
          root.resolveBlockDropIntoColumn(global.x, global.y)
      }
    }

    onPressed: function(mouse) {
      root.gutterWasDragging = false
      root.gutterPressIsColumns = false

      if (mouse.x < leftMarginClickArea.width - 24)
        return

      const global = mapToItem(null, mouse.x, mouse.y)
      const hoverRow = rowAt(mouse.y)

      if (isColumnsRow(hoverRow)) {
        const layoutId = root.model.blockIdAtVisibleRow(hoverRow)

        // Ctrl/Shift on the leftmost column's gutter moves the whole layout.
        if ((mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0) {
          root.beginLayoutDrag(layoutId, global.x, global.y)
          root.gutterWasDragging = true
          return
        }

        // A plain press-and-hold drags the first child of that column.
        root.gutterPressBlockId = layoutId
        root.gutterPressIsColumns = true
        root.gutterPressGlobalX = global.x
        root.gutterPressGlobalY = global.y
        gutterHoldTimer.restart()
        return
      }

      root.gutterPressBlockId = blockIdAt(mouse.y)
      root.gutterPressGlobalY = global.y

      if (root.gutterPressBlockId.length > 0)
        gutterHoldTimer.restart()
    }

    onReleased: function(mouse) {
      gutterHoldTimer.stop()

      if (root.gutterChildDragStarted) {
        root.endNestedChildDrag()
        root.gutterChildDragStarted = false
        root.gutterPressIsColumns = false
        root.gutterPressBlockId = ""
        return
      }

      if (root.isDraggingBlock) {
        const src = root.draggingSourceIndex
        const dst = root.dropTargetIndex
        const id = root.draggingBlockId
        const columnTarget = root.blockDropIntoColumn

        root.draggingBlockId = ""
        root.draggingSourceIndex = -1
        root.dropTargetIndex = -1
        root.draggingVisualY = 0
        root.blockDropIntoColumn = null

        if (columnTarget && root.model)
          root.model.moveBlockToColumn(id, columnTarget.columnId,
                                       columnTarget.row)
        else if (src !== dst && src >= 0 && dst >= 0)
          root.moveBlockToVisibleRow(id, dst)
      }

      root.gutterPressBlockId = ""
      root.gutterPressIsColumns = false
    }

    onExited: {
      root.gutterHoverBlockId = ""
      root.gutterHoverGlobalX = -1
      root.gutterHoverGlobalY = -1
      // Do not cancel a drag here: a gutter drag immediately leaves this
      // narrow strip and continues over the document.
      gutterHoldTimer.stop()
    }

    onCanceled: {
      if (root.gutterChildDragStarted) {
        root.cancelNestedChildDrag()
        root.gutterChildDragStarted = false
      }
      root.gutterPressBlockId = ""
      root.gutterPressIsColumns = false
    }

    onClicked: function(mouse) {
      const row = rowAt(mouse.y)
      const blockId = row >= 0 ? root.model.blockIdAtVisibleRow(row) : ""

      if (row >= 0)
        root.focusDelegateAt(row, 0)

      // The right part of the margin is where the gutter handle is drawn.
      // Columns rows have no gutter; their leftmost column owns it instead.
      if (blockId.length > 0 && !isColumnsRow(row)
          && mouse.x >= leftMarginClickArea.width - 24
          && !root.gutterWasDragging) {
        gutterOptionsMenu.blockId = blockId
        gutterOptionsMenu.popup()
      }
    }
  }

  ListView {
    id: documentView

    objectName: "documentView"

    width: Math.min(root.contentWidth, parent.width - 48)

    anchors {
      top: parent.top
      bottom: parent.bottom
      horizontalCenter: parent.horizontalCenter

      topMargin: 120
      bottomMargin: 160
    }

    model: root.model.visibleModel
    spacing: 0
    clip: false

    // Mouse dragging stays disabled so text selection and block dragging work.
    // Flickable still owns the scroll bounds, deceleration and origin handling;
    // wheel input is injected as a flick from the handler below.
    interactive: false
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.VerticalFlick
    pixelAligned: true
    flickDeceleration: 32000
    maximumFlickVelocity: 6000

    // Dynamic edge fading mask (GPU shader effect)
    readonly property real fadeZonePx: 44.0
    // ListView may re-base its origin while recycling delegates, so the real
    // end has to account for originY (this was the bottom-only overscroll bug).
    readonly property real endContentY: Math.max(originY, originY + contentHeight - height)

    function clampY(y) {
      return Math.max(originY, Math.min(y, endContentY))
    }

    layer.enabled: false
    layer.effect: ShaderEffect {
      property real topFadeSize: documentView.height > 0 ? (documentView.fadeZonePx / documentView.height) : 0.0
      property real bottomFadeSize: documentView.height > 0 ? (documentView.fadeZonePx / documentView.height) : 0.0
      // Fade ramps in relative to Flickable's own origin-aware bounds.
      property real topAlpha: Math.max(0.0, 1.0 - Math.min(1.0, Math.max(0.0, documentView.contentY - documentView.originY) / documentView.fadeZonePx))
      property real bottomAlpha: Math.max(0.0, 1.0 - Math.min(1.0, Math.max(0.0, documentView.endContentY - documentView.contentY) / documentView.fadeZonePx))

      fragmentShader: "shaders/edgefade.frag.qsb"
    }

    // Lines advanced per mouse-wheel notch. Change this single value to tune
    // wheel speed; pixel distance is derived from the editor's average line height.
    readonly property int wheelScrollLines: 17
    readonly property real wheelLineHeightPx: 28
    readonly property real wheelScrollPx: wheelScrollLines * wheelLineHeightPx
    // Fixed conversion for trackpad pixel deltas. Using a measured interval
    // made upward scrolling slow after a pause/reversal; a fixed frame keeps
    // both directions identical regardless of event timing.
    readonly property real wheelFrameSeconds: 0.0025

    WheelHandler {
      id: wheelHandler
      target: null
      acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
      onWheel: (event) => {
        // Shift+wheel is reserved for horizontal scrolling inside table blocks.
        if (event.modifiers & Qt.ShiftModifier)
          return
        if (!root.scrollingEnabled || root.activeCommandDelegate !== null || root.isDraggingBlock || selectionToolbar.showToolbar)
          return

        let velocity
        if (event.pixelDelta.y !== 0) {
          // Trackpad: fixed px/s conversion (Qt's flick velocity is positive
          // when scrolling up), symmetric for both directions.
          velocity = event.pixelDelta.y / documentView.wheelFrameSeconds
        } else {
          // Mouse: each notch glides wheelScrollPx under Flickable's deceleration.
          const notches = -(event.angleDelta.y / 120.0)
          velocity = -Math.sign(notches) * Math.sqrt(
            2 * documentView.flickDeceleration * documentView.wheelScrollPx * Math.abs(notches))
        }

        velocity = Math.max(-documentView.maximumFlickVelocity,
                            Math.min(documentView.maximumFlickVelocity, velocity))
        documentView.flick(0, velocity)
        event.accepted = true
      }
    }

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

    // The delegate is a named Component so column layouts can reuse the exact
    // same block rendering and keyboard behaviour for their child blocks.
    Component {
      id: blockDelegateComponent

        Item {
        id: delegateRoot
        property Item editorRoot: root

        // These properties receive the C++ model roles.
        required property string blockId
        required property string parentId
        required property string content
        required property int blockType
        required property int headingLevel
        required property int nextBlockType
        required property int numberIndex
        required property string alignment
        required property bool checked
        required property string calloutType
        required property string codeLanguage
        required property string imageSource
        required property real imageWidthRatio
        required property var metadata
        required property int index

        readonly property real bottomGap: BlockSpacing.bottomGap(
          delegateRoot.blockType,
          delegateRoot.nextBlockType,
          delegateRoot.index === root.model.visibleCount() - 1
        )
        readonly property var rendererItem: renderer

        // Column children are rendered by ColumnsBlock with this same delegate.
        readonly property bool isColumnChild: delegateRoot.parentId.length > 0
        readonly property string columnId: isColumnChild
                                           ? root.model.columnIdOfBlock(delegateRoot.blockId)
                                           : ""
        readonly property var columnLayoutRenderer: isColumnChild
                                                    ? root.nestedDragRenderer(delegateRoot.blockId)
                                                    : null

        readonly property bool isBeingDragged: root.draggingBlockId === delegateRoot.blockId
                                               || (delegateRoot.isColumnChild
                                                   && columnLayoutRenderer
                                                   && columnLayoutRenderer.childDraggingBlockId === delegateRoot.blockId)
        readonly property bool isHovered: blockHoverArea.containsMouse
                                          || gutterHoverArea.containsMouse
                                          || handleHover.hovered
                                          || handleArea.containsMouse
                                          || root.gutterHoverBlockId === delegateRoot.blockId
                                          || (delegateRoot.isColumnChild
                                              && columnLayoutRenderer
                                              && columnLayoutRenderer.hoveredChildBlockId === delegateRoot.blockId)
        // Both hover areas share the same origin (30px left of the writing
        // edge), so fall back to the gutter area. The leftmost column's handle
        // lives in the editor margin, where the margin MouseArea is the only
        // hover handler; forward its coordinates through gutterHoverBlockId.
        readonly property real hoverX: {
          if (blockHoverArea.containsMouse) return blockHoverArea.mouseX
          if (gutterHoverArea.containsMouse) return gutterHoverArea.mouseX
          if (root.gutterHoverBlockId === delegateRoot.blockId
              && root.gutterHoverGlobalX >= 0)
            return delegateRoot.mapFromItem(null, root.gutterHoverGlobalX, 0).x + 30
          return -1
        }
        readonly property real hoverY: {
          if (blockHoverArea.containsMouse) return blockHoverArea.mouseY
          if (gutterHoverArea.containsMouse) return gutterHoverArea.mouseY
          if (root.gutterHoverBlockId === delegateRoot.blockId
              && root.gutterHoverGlobalY >= 0)
            return delegateRoot.mapFromItem(null, 0, root.gutterHoverGlobalY).y
          return -1
        }

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
          editorRoot.pendingFocusAttempts = 0
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

        // Dedicated gutter hover area. blockHoverArea above is the topmost
        // hover handler in the delegate and can swallow hover events before
        // the handle's own handlers see them, which made the handle vanish as
        // soon as the pointer left the text. This area keeps it alive while
        // the pointer is anywhere in the gutter.
        MouseArea {
          id: gutterHoverArea
          anchors {
            top: parent.top
            bottom: parent.bottom
          }
          // Top-level handles live outside the ListView (the editor margin
          // owns them). Column children have a real 24px gutter inside the pane.
          x: -30
          width: 30
          hoverEnabled: true
          acceptedButtons: Qt.NoButton
          z: 15
        }

        // MouseArea covering the block AND extending 30px into the left gutter (across the drag handle and beyond)
        MouseArea {
          id: blockHoverArea
          anchors.top: parent.top
          anchors.bottom: parent.bottom
          anchors.left: parent.left
          anchors.right: parent.right
          anchors.leftMargin: -30
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

        // Same displacement logic as the main editor, for siblings inside the
        // column the drag started in. Cross-column drags intentionally do not
        // shift panes, so only same-column drags animate.
        property real nestedDisplacement: {
          if (!delegateRoot.isColumnChild || !columnLayoutRenderer
              || !columnLayoutRenderer.childDragActive
              || delegateRoot.isBeingDragged
              || columnLayoutRenderer.childDragSourceColumn
                 !== columnLayoutRenderer.childDragDropColumn)
            return 0

          const from = columnLayoutRenderer.childDragSourceRow
          const to = columnLayoutRenderer.childDragDropRow
          const height = columnLayoutRenderer.childDraggingItemHeight

          if (from < to) {
            if (delegateRoot.index > from && delegateRoot.index <= to)
              return -height
          } else if (from > to) {
            if (delegateRoot.index >= to && delegateRoot.index < from)
              return height
          }

          return 0
        }

        // Top-level delegates are parented to the document ListView's content
        // item; column children to their pane's inner ListView. Either way the
        // parent width is the correct writing width for this block.
        width: parent ? parent.width : documentView.width
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
          // Nested child drags follow the pointer in both axes so the lifted
          // block glides horizontally between columns as well as vertically.
          x: delegateRoot.isBeingDragged && delegateRoot.isColumnChild
             ? (columnLayoutRenderer ? columnLayoutRenderer.childDraggingVisualX : 0)
             : 0
          y: delegateRoot.isBeingDragged
             ? (delegateRoot.isColumnChild
                ? (columnLayoutRenderer ? columnLayoutRenderer.childDraggingVisualY : 0)
                : root.draggingVisualY)
             : (delegateRoot.isColumnChild ? delegateRoot.nestedDisplacement
                                           : delegateRoot.visualDisplacement)
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

        function focusChildBlock(blockId, position, rangeStart, rangeEnd, xPosition) {
          return renderer.focusChildBlock(blockId, position, rangeStart, rangeEnd,
                                          xPosition)
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
              const newId = root.insertParagraphAfter(delegateRoot.blockId)
              if (newId.length > 0)
                root.requestFocusBlock(newId, 0)
            } else {
              root.requestFocusBlock(delegateRoot.blockId, 0)
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
          delegateComponent: blockDelegateComponent
          blockId: delegateRoot.blockId
          content: delegateRoot.content
          blockType: delegateRoot.blockType
          headingLevel: delegateRoot.headingLevel
          numberIndex: delegateRoot.numberIndex
          alignment: delegateRoot.alignment
          isChecked: delegateRoot.checked
          calloutType: delegateRoot.calloutType || "note"
          codeLanguage: delegateRoot.codeLanguage || ""
          imageSource: delegateRoot.imageSource || ""
          imageWidthRatio: delegateRoot.imageWidthRatio !== undefined ? delegateRoot.imageWidthRatio : 1.0
          blockMetadata: delegateRoot.metadata
          isBlockHovered: delegateRoot.isHovered
          hoverX: delegateRoot.hoverX
          hoverY: delegateRoot.hoverY
          showPlaceholder: delegateRoot.index === 0 && root.model.visibleCount() === 1
          searchHighlights: root.searchHighlightsByBlock[delegateRoot.blockId] || []
          searchNavigationActive: root.searchNavigationActive
          columnDropResolver: function(globalX, globalY) {
            return root.resolveColumnDrop(globalX, globalY)
          }
          editorApi: root

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
            root.requestFocusBlock(delegateRoot.blockId, position)
          }

          onFocusDelegateRequested: function(row, position) {
            root.focusDelegateAt(row, position)
          }

          onTabRequested: function(direction) {
            root.focusAdjacentEditable(delegateRoot.blockId, direction, -1)
          }

          onImageMenuRequested: function(blockId, globalX, globalY, globalBottom) {
            const top = root.mapFromItem(null, globalX, globalY)
            const bottom = root.mapFromItem(null, globalX, globalBottom)
            root.activeImageBlockDelegate = delegateRoot
            imageSourceMenu.openFor(blockId, top.x, top.y, bottom.y)
          }

          onMoveFocusRequested: function(direction, xPosition) {
            // Inside a column, first try the next sibling block. Navigation is
            // model-driven so the shared delegate does not depend on which
            // ListView (main list or column list) instantiated it.
            if (delegateRoot.isColumnChild) {
              const siblings = root.model.blocksOfColumn(delegateRoot.columnId)
              const next = siblings.indexOf(delegateRoot.blockId) + direction

              if (next >= 0 && next < siblings.length) {
                root.requestFocusBlock(siblings[next], -1, -1, -1, xPosition)
                return
              }
            }

            // Top-level blocks and column edges continue in document order,
            // which crosses into the next column transparently.
            root.focusAdjacentEditable(delegateRoot.blockId, direction,
                                       xPosition)
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
              if (commandMenu.mode === 1) {
                commandMenu.cancelCount()
              } else {
                renderer.commandMenuActive = false
                commandMenu.visible = false
                root.activeCommandDelegate = null
              }
            }
          }

          onCommandMenuAccepted: {
            if (root.activeCommandDelegate !== delegateRoot)
              return

            if (commandMenu.mode === 1) {
              commandMenu.confirmCount()
              return
            }

            const command = commandMenu.selectedCommand

            if (command && command.type === 12) {
              // Columns need a second step. Keep the delegate active so its
              // keyboard handling still routes Return/ arrows to the picker.
              commandMenu.visible = false
              commandMenu.openCountPicker()
              return
            }

            renderer.commandMenuActive = false
            commandMenu.visible = false
            root.activeCommandDelegate.applyCommand(command)
            root.activeCommandDelegate = null
          }

          onCommandMenuNavigated: function(direction) {
            if (root.activeCommandDelegate !== delegateRoot)
              return

            if (commandMenu.mode === 1)
              commandMenu.moveCountSelection(direction)
            else
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
              const newId = root.insertParagraphAfter(delegateRoot.blockId)
              if (newId.length > 0)
                root.requestFocusBlock(newId, 0)
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
                root.requestFocusBlock(delegateRoot.blockId, 0)
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
            const currentText = renderer.editorText
            const currentType = delegateRoot.blockType
            // Resolve the caret target from the document, not from the current
            // ListView. The previous block may be rendered by another column or
            // may not have a delegate at all; itemAtIndex() would then return
            // null and the empty block would simply disappear on backspace.
            const previousId = root.previousEditableBlockId(delegateRoot.blockId)

            if (currentType === 7 /* Divider */) {
              if (root.model.removeBlock(delegateRoot.blockId)) {
                if (previousId.length > 0)
                  root.requestFocusBlock(previousId, -1)
              }
              return
            }

            // If this is a specialized block (Quote, List, Todo, Callout), pressing backspace
            // at cursor position 0 should demote it to a Paragraph first instead of
            // immediately merging into the previous block and deleting the current block!
            if (currentType !== 0) {
              if (root.model.changeBlockType(delegateRoot.blockId, 0, 0)) {
                root.requestFocusBlock(delegateRoot.blockId, 0)
              }
              return
            }

            // Nothing to merge into or focus: keep the first editable block
            // instead of deleting it and leaving the caret nowhere.
            if (previousId.length === 0)
              return

            if (currentText.length === 0) {
              // A column always keeps at least one editable paragraph.
              if (delegateRoot.isColumnChild
                  && root.model.blocksOfColumn(delegateRoot.columnId).length === 1) {
                const newId = root.model.appendBlockToColumn(delegateRoot.columnId)
                root.model.removeBlock(delegateRoot.blockId)
                if (newId.length > 0)
                  root.requestFocusBlock(newId, 0)
                return
              }

              if (root.model.removeBlock(delegateRoot.blockId)) {
                if (previousId.length > 0)
                  root.requestFocusBlock(previousId, -1)
              }
              return
            }

            if (root.model.mergeWithPrevious(
                  delegateRoot.blockId,
                  currentText)) {
              if (previousId.length > 0)
                root.requestFocusBlock(previousId, -1)
            }
          }

          onUndoRequested: {
            root.undoFrom(delegateRoot.blockId, delegateRoot.index)
          }

          onRedoRequested: {
            root.redoFrom(delegateRoot.blockId, delegateRoot.index)
          }
        }

        Menu {
          id: blockOptionsMenu

          MenuItem {
            text: "Duplicate"
            icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_copy_20_regular.svg"
            onTriggered: {
              const newId = root.model.duplicateBlock(delegateRoot.blockId)
              if (newId.length > 0)
                root.requestFocusBlock(newId, 0)
            }
          }

          MenuSeparator {}

          MenuItem {
            text: "Delete block"
            icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_delete_20_regular.svg"
            onTriggered: root.model.removeBlock(delegateRoot.blockId)
          }
        }

        // Drag handle on the left margin
        Item {
          id: dragHandle
          visible: !delegateRoot.isColumnChild && delegateRoot.blockType !== 12
          width: 18
          height: 24
          x: -20
          y: Math.max(0, Math.round((renderer.firstLineHeight - height) / 2))
          z: 20

          // Purely mouse-hover driven: show only on the block the mouse is hovering over
          property bool shouldShow: {
            if (root.isDraggingBlock) return false
            if (delegateRoot.isColumnChild && columnLayoutRenderer
                && columnLayoutRenderer.childDraggingBlockId === delegateRoot.blockId)
              return false
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

            property real startGlobalX: 0
            property real startGlobalY: 0
            property bool wasDragging: false

            Timer {
              id: holdTimer
              interval: 200
              repeat: false
              onTriggered: {
                if (!handleArea.pressed)
                  return

                if (delegateRoot.isColumnChild) {
                  if (root.beginNestedChildDrag(delegateRoot.blockId,
                                                handleArea.startGlobalX,
                                                handleArea.startGlobalY))
                    handleArea.cursorShape = Qt.ClosedHandCursor
                  return
                }

                root.draggingBlockId = delegateRoot.blockId
                root.draggingSourceIndex = delegateRoot.index
                root.dropTargetIndex = delegateRoot.index
                root.draggingVisualY = 0
                root.draggingItemHeight = delegateRoot.height
                root.dragGlobalY = handleArea.startGlobalY
                handleArea.cursorShape = Qt.ClosedHandCursor
              }
            }

            onPressed: function(mouse) {
              startGlobalX = mapToItem(null, mouse.x, mouse.y).x
              startGlobalY = mapToItem(null, mouse.x, mouse.y).y
              root.dragGlobalY = startGlobalY
              wasDragging = false
              holdTimer.restart()
            }

            onPositionChanged: function(mouse) {
              const currentGlobalY = mapToItem(null, mouse.x, mouse.y).y
              root.dragGlobalY = currentGlobalY

              if (delegateRoot.isColumnChild) {
                if (root.nestedDragChildId.length > 0) {
                  const global = mapToItem(null, mouse.x, mouse.y)
                  root.updateNestedChildDrag(global.x, global.y)
                }
                return
              }

              if (!root.isDraggingBlock) return

              const global = mapToItem(null, mouse.x, mouse.y)
              root.draggingVisualY = currentGlobalY - startGlobalY
              root.updateDragTarget()
              root.blockDropIntoColumn =
                root.resolveBlockDropIntoColumn(global.x, global.y)
            }

            onReleased: function(mouse) {
              holdTimer.stop()
              handleArea.cursorShape = Qt.OpenHandCursor

              if (delegateRoot.isColumnChild) {
                if (root.nestedDragChildId.length > 0) {
                  wasDragging = true
                  root.endNestedChildDrag()
                }
                return
              }

              if (root.isDraggingBlock) {
                wasDragging = true
                const src = root.draggingSourceIndex
                const dst = root.dropTargetIndex
                const blockId = root.draggingBlockId
                const columnTarget = root.blockDropIntoColumn

                root.draggingBlockId = ""
                root.draggingSourceIndex = -1
                root.dropTargetIndex = -1
                root.draggingVisualY = 0
                root.blockDropIntoColumn = null

                if (columnTarget && root.model)
                  root.model.moveBlockToColumn(blockId, columnTarget.columnId,
                                               columnTarget.row)
                else if (src !== dst && src >= 0 && dst >= 0)
                  root.moveBlockToVisibleRow(blockId, dst)
              }
            }

            onClicked: function(mouse) {
              if (mouse.button === Qt.LeftButton && !wasDragging)
                blockOptionsMenu.popup()
            }

            onCanceled: {
              holdTimer.stop()
              handleArea.cursorShape = Qt.OpenHandCursor

              if (delegateRoot.isColumnChild) {
                root.cancelNestedChildDrag()
                return
              }

              root.draggingBlockId = ""
              root.draggingSourceIndex = -1
              root.dropTargetIndex = -1
              root.draggingVisualY = 0
              root.blockDropIntoColumn = null
            }
          }
        }
      }
    }

    delegate: blockDelegateComponent

    // Fixed breathing room after the final block (top stays flush).
    footer: Item {
      width: documentView.width
      height: 200

      MouseArea {
        anchors.fill: parent
        onClicked: {
          const id = root.model.appendBlock(0, "")
          if (id.length > 0)
            root.requestFocusBlock(id, 0)
        }
      }
    }
  }

  property var activeImageBlockDelegate: null

  MouseArea {
    visible: imageSourceMenu.isOpen
    anchors.fill: parent
    z: 29
    onClicked: {
      imageSourceMenu.close()
      if (root.activeImageBlockDelegate
          && root.activeImageBlockDelegate.rendererItem
          && root.activeImageBlockDelegate.rendererItem.block
          && "menuOpen" in root.activeImageBlockDelegate.rendererItem.block)
        root.activeImageBlockDelegate.rendererItem.block.menuOpen = false
      root.activeImageBlockDelegate = null
    }
  }

  ImageSourceMenu {
    id: imageSourceMenu
    model: root.model
    z: 30

    onIsOpenChanged: {
      if (!isOpen) {
        if (root.activeImageBlockDelegate
            && root.activeImageBlockDelegate.rendererItem
            && root.activeImageBlockDelegate.rendererItem.block
            && "menuOpen" in root.activeImageBlockDelegate.rendererItem.block)
          root.activeImageBlockDelegate.rendererItem.block.menuOpen = false
        root.activeImageBlockDelegate = null
      }
    }
  }

  Menu {
    id: gutterOptionsMenu
    objectName: "gutterOptionsMenu"
    property string blockId: ""

    MenuItem {
      text: "Duplicate"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_copy_20_regular.svg"
      onTriggered: {
        const id = gutterOptionsMenu.blockId
        const newId = root.model.duplicateBlock(id)
        if (newId.length > 0)
          root.requestFocusBlock(newId, 0)
      }
    }

    MenuSeparator {}

    MenuItem {
      text: "Delete block"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_delete_20_regular.svg"
      onTriggered: root.model.removeBlock(gutterOptionsMenu.blockId)
    }
  }

  CommandMenu {
    id: commandMenu

    onHeightChanged: {
      if (root.commandMenuAbove && root.activeCommandDelegate !== null)
        y = root.commandMenuAnchorY - height - root.commandMenuGap
    }

    onCommandSelected: function(command) {
      if (!root.activeCommandDelegate)
        return

      if (command && command.type === 12) {
        visible = false
        commandMenu.openCountPicker()
        return
      }

      root.activeCommandDelegate.applyCommand(command)
      root.activeCommandDelegate.closeCommandMenu()
      root.activeCommandDelegate = null
      visible = false
    }

    onColumnCountConfirmed: function(count) {
      const delegate = root.activeCommandDelegate

      if (!delegate) {
        visible = false
        return
      }

      if (delegate.rendererItem)
        delegate.rendererItem.commandMenuActive = false
      root.activeCommandDelegate = null
      visible = false
      root.model.updateBlockText(delegate.blockId, "")

      // A fresh layout should fill the editor: subtract the gaps and the add
      // column strip that ColumnsBlock places after the last pane, then let
      // the model split the remainder evenly. Individual panes stay resizable.
      const columnSpacing = 8
      const addStripWidth = 18
      const available = documentView.width - (count - 1) * columnSpacing
                        - addStripWidth
      const newId = root.model.createColumnsLayout(
        root.model.sourceRowOfVisibleRow(delegate.index), count, available)

      if (newId.length > 0)
        root.requestFocusBlock(newId, 0)
    }

    onPickerCancelled: {
      if (root.activeCommandDelegate && root.activeCommandDelegate.rendererItem)
        root.activeCommandDelegate.rendererItem.commandMenuActive = false
      root.activeCommandDelegate = null
      visible = false
    }
  }

  SelectionToolbar {
    id: selectionToolbar

    objectName: "selectionToolbar"

    onShowToolbarChanged: {
      if (showToolbar && documentView)
        documentView.cancelFlick()
    }

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
