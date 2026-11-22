import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl

// A Notion-style column layout. The layout itself is a single top-level block;
// each Column block is rendered as a pane and its children are rendered with
// the very same delegate component used by the main document list.
Item {
  id: root

  property string blockId: ""
  property int blockType: 12
  property string content: ""
  property int headingLevel: 0
  property int numberIndex: 1
  property string alignment: "left"
  property bool isChecked: false
  property string calloutType: "note"
  property string codeLanguage: ""
  property string imageSource: ""
  property real imageWidthRatio: 1.0
  property var blockMetadata: null
  property bool isBlockHovered: false
  // Mouse x within the block, supplied by the Editor. The block-level hover
  // area sits above nested handlers, so this is used to light one column pill.
  property real hoverX: -1
  property real hoverY: -1
  property bool showPlaceholder: false
  property var model: null
  property var delegateComponent: null
  property var searchHighlights: []
  property bool searchNavigationActive: false
  property bool commandMenuActive: false

  readonly property string editorText: ""
  readonly property int cursorPosition: 0
  readonly property bool isFocused: false
  readonly property real firstLineHeight: 28

  signal enterPressed()
  signal backspacePressed()
  signal focusRequested(int position)
  signal undoRequested()
  signal redoRequested()
  signal moveFocusRequested(int direction, real xPosition)
  signal caretMoved(int position, real cursorGlobalY, real cursorHeight)
  signal commandMenuRequested(string query, real xPosition, real yPosition, real cursorHeight)
  signal commandMenuDismissed()
  signal commandMenuAccepted()
  signal commandMenuNavigated(int direction)
  signal selectionChanged(bool hasSelection, real xPosition, real yPosition, real cursorHeight)
  signal searchNavigated(int direction)
  signal searchDismissed()
  signal searchEditStarted()
  signal focusDelegateRequested(string blockId, int position)

  property var columnIds: []
  property var pendingChildFocusQueue: []
  property int pendingAttempts: 0

  // A menu popup grabs the mouse, so the hover areas never see a leave event
  // and would keep pills lit after a click. Ignore hover until the pointer
  // actually moves again.
  property bool hoverSuppressed: false
  property bool hoverLeftSinceMenu: false
  property bool childMenuOpenAtPress: false
  property bool columnMenuOpenAtPress: false
  property real childMenuClosedAt: 0
  property real columnMenuClosedAt: 0
  property real suppressUntilMoveAt: 0
  function suppressHoverUntilMove() {
    root.hoverSuppressed = true
    root.hoverLeftSinceMenu = false
    root.suppressUntilMoveAt = Date.now()
  }
  // A click must not leave a pill lit. Keep it suppressed until the pointer
  // has genuinely left the layout and then hovered back over a block.
  function maybeClearHoverSuppression() {
    if (!root.hoverSuppressed)
      return

    if (root.hoverX < 0 || root.hoverY < 0) {
      root.hoverLeftSinceMenu = true
      return
    }

    if (!root.hoverLeftSinceMenu)
      return

    if (Date.now() - root.suppressUntilMoveAt < 250)
      return

    root.hoverSuppressed = false
    root.hoverLeftSinceMenu = false
  }
  onHoverXChanged: root.maybeClearHoverSuppression()
  onHoverYChanged: root.maybeClearHoverSuppression()

  // Converts a block-local hover X into scrollable content coordinates.
  function contentXAt(x) {
    return x - 30 - root.gutter
           + (columnsFlickable ? columnsFlickable.contentX : 0)
  }

  // Returns -1 outside the panes so hovering the add strip cannot light a
  // pill in the last column.
  function columnIndexAtContentX(x) {
    let acc = 0
    for (let i = 0; i < root.columnIds.length; ++i) {
      const item = columnRepeater.itemAt(i)
      const width = item ? item.paneWidth : 0
      if (x >= acc && x < acc + width)
        return i
      acc += width + root.columnSpacing
    }
    return -1
  }

  // Insertion slot for a global point, so a drag resolved by the editor keeps
  // the same half-of-pane precision as a purely local drag.
  function insertionSlotAtPoint(globalX, globalY) {
    const relativeX = globalX - columnsContent.mapToItem(null, 0, 0).x
    return root.columnInsertionSlot(relativeX)
  }

  function columnStartX(index) {
    let offset = 0

    for (let i = 0; i < index; ++i) {
      const item = columnRepeater.itemAt(i)
      offset += (item ? item.paneWidth : 0) + root.columnSpacing
    }

    return offset
  }

  // Number of pane midpoints to the left of x. That is the insertion slot
  // for a column reorder: the line is drawn before that pane (or at the end
  // when the slot equals the column count).
  function columnInsertionSlot(relativeX) {
    let running = 0
    let slot = 0

    for (let i = 0; i < root.columnIds.length; ++i) {
      const item = columnRepeater.itemAt(i)
      const width = item ? item.paneWidth : 0

      if (relativeX > running + width / 2)
        slot = i + 1

      running += width + root.columnSpacing
    }

    return slot
  }

  // Pane that actually contains x. In the gaps and past the last pane, fall
  // back to the closest pane by midpoint.
  function columnIndexForDrop(relativeX) {
    const index = root.columnIndexAtContentX(relativeX)

    return index >= 0 ? index
                      : Math.min(root.columnInsertionSlot(relativeX),
                                 root.columnIds.length - 1)
  }

  // A drop between panes yields an insertion slot; moveColumnToIndex takes the
  // final index after the source column has been removed.
  function finalColumnIndex(sourceIndex, slot) {
    return slot > sourceIndex ? slot - 1 : slot
  }

  // Hover for nested blocks cannot rely on their own MouseAreas: the layout's
  // top-level hover area sits above them. Use the forwarded block hover point
  // instead, exactly like the column pill and divider.
  readonly property string hoveredChildBlockId: {
    if (root.hoverSuppressed || !root.isBlockHovered || root.hoverX < 0
        || root.hoverY < 0 || !root.model || root.columnIds.length === 0)
      return ""

    const contentX = root.contentXAt(root.hoverX)
    const columnIndex = root.columnIndexAtContentX(contentX)

    if (columnIndex < 0)
      return ""

    const columnId = root.columnIds[columnIndex]
    const paneItem = columnRepeater.itemAt(columnIndex)

    if (!paneItem || !paneItem.blockList)
      return ""

    const globalY = root.mapToItem(null, 0, 0).y + root.hoverY
    const listTop = paneItem.blockList.mapToItem(null, 0, 0).y
    const ids = root.model.blocksOfColumn(columnId)
    let acc = 0

    for (let i = 0; i < ids.length; ++i) {
      const delegate = paneItem.blockList.itemAtIndex(i)
      const height = delegate ? delegate.height : 24

      if (globalY >= listTop + acc && globalY < listTop + acc + height)
        return ids[i]

      acc += height
    }

    return ""
  }

  // 30px block gutter; columns no longer add an outer indent.
  readonly property int hoveredColumnIndex: {
    if (root.hoverSuppressed || !root.isBlockHovered || root.hoverX < 0)
      return -1

    const x = root.contentXAt(root.hoverX)
    let acc = 0

    for (let i = 0; i < root.columnIds.length; ++i) {
      const item = columnRepeater.itemAt(i)
      const width = item ? item.paneWidth : 0

      if (x >= acc && x < acc + width)
        return i

      acc += width + root.columnSpacing
    }

    return -1
  }

  // Pointer near a column's right edge reveals its resize divider.
  readonly property int hoveredDividerIndex: {
    if (root.hoverSuppressed || !root.isBlockHovered || root.hoverX < 0)
      return -1

    const x = root.contentXAt(root.hoverX)
    let acc = 0

    for (let i = 0; i < root.columnIds.length; ++i) {
      const item = columnRepeater.itemAt(i)
      const width = item ? item.paneWidth : 0
      const edge = acc + width

      // Matches the divider's actual 12px-wide hit area (edge +/- 6).
      if (x >= edge - 6 && x <= edge + 6)
        return i

      acc += width + root.columnSpacing
    }

    return -1
  }

  // The editor's block hover area sits above every nested MouseArea, so their
  // containsMouse never turns true. Derive the add-strip hover from the
  // forwarded block hover point instead.
  readonly property bool addColumnHovered: {
    if (root.hoverSuppressed || !root.isBlockHovered || root.hoverX < 0)
      return false

    const x = root.contentXAt(root.hoverX)
    return x >= totalColumnsWidth - addStripWidth && x <= totalColumnsWidth
  }

  // The outer block hover area owns the cursor, so expose the shape implied
  // by the forwarded hover position for the editor to apply.
  readonly property int hoverCursorShape: {
    if (root.hoverSuppressed || !root.isBlockHovered || root.hoverX < 0)
      return Qt.ArrowCursor

    if (root.addColumnHovered)
      return Qt.PointingHandCursor

    if (root.hoveredColumnIndex >= 0 && root.hoverY >= 0
        && root.hoverY < root.headerHeight)
      return Qt.PointingHandCursor

    if (root.hoveredDividerIndex >= 0)
      return Qt.SplitHCursor

    if (root.hoveredChildBlockId.length > 0) {
      const contentX = root.contentXAt(root.hoverX)

      if (contentX < root.columnStartX(root.hoveredColumnIndex)
                     + root.paneGutter)
        return Qt.PointingHandCursor
    }

    return Qt.ArrowCursor
  }

  // Column drag state (press starts a drag, like table columns).
  property bool isDraggingColumn: false
  property int draggingColumnIndex: -1
  property int dropColumnIndex: -1
  property real columnDragStartX: 0
  property real columnDragStartY: 0
  property real columnDragVisualX: 0
  property real columnDragVisualY: 0
  property real columnDragGlobalX: 0
  property real columnDragGlobalY: 0
  property var columnDropTarget: null

  // Drag state for blocks inside columns. Dragging can cross column boundaries;
  // the model's moveBlockToColumn() performs the final reparent.
  property bool childDragActive: false
  property string dragChildId: ""
  property string dropColumnId: ""
  property int dropChildRow: -1
  property int dropChildColumnIndex: -1

  // Animation state consumed by the shared block delegate, mirroring the main
  // editor's drag lift and displaced-sibling translate.
  readonly property string childDraggingBlockId: root.dragChildId
  // Which column owns the block being dragged. Other columns' delegates must
  // not react to this drag's displacement animation.
  readonly property string childDragColumnId: root.model
                                              && root.dragChildId.length > 0
                                              ? root.model.columnIdOfBlock(
                                                    root.dragChildId)
                                              : ""
  property real childDraggingVisualX: 0
  property real childDraggingVisualY: 0
  property real childDraggingItemHeight: 0
  property int childDragSourceRow: -1
  property int childDragSourceColumn: -1
  readonly property int childDragDropRow: root.dropChildRow
  readonly property int childDragDropColumn: root.dropChildColumnIndex
  property real childDragStartX: 0
  property real childDragStartY: 0
  property real childDragStartContentX: 0
  // Last pointer position during a child drag. Cached because holding the
  // pointer still at an edge emits no further positionChanged events, which is
  // exactly when the auto-scroll timer needs to keep scrolling.
  property real childDragGlobalX: 0
  property real childDragGlobalY: 0

  function reload() {
    if (!root.model || root.blockId.length === 0)
      return

    const ids = root.model.columnIdsOf(root.blockId)

    if (ids.length === root.columnIds.length) {
      for (let i = 0; i < ids.length; ++i) {
        if (ids[i] !== root.columnIds[i]) {
          root.columnIds = ids
          return
        }
      }

      return
    }

    root.columnIds = ids
  }

  property int docRevision: root.model ? root.model.contentRevision : -1
  onDocRevisionChanged: root.reload()

  onBlockIdChanged: root.reload()
  onModelChanged: root.reload()
  Component.onCompleted: root.reload()

  Connections {
    target: root.model
    function onCountChanged() { root.reload() }
    function onRowsMoved() { root.reload() }
    // Only width changes need to re-evaluate the pane geometry. Reacting to
    // every dataChanged re-ran the width bindings on each keystroke.
    function onColumnWidthsChanged() { ++root.widthRevision }
  }

  Menu {
    id: childOptionsMenu
    objectName: "childOptionsMenu"
    property string blockId: ""
    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onAboutToShow: {
      root.suppressHoverUntilMove()
      root.notifyMenusOpen()
      // A click that started a hold-drag must not leave the pill marked as
      // the active drag once the menu takes over.
      root.childDragActive = false
      root.dragChildId = ""
      root.childDraggingVisualX = 0
      root.childDraggingVisualY = 0
      if (root.editorApi)
        root.editorApi.nestedDragChildId = ""
    }
    onClosed: {
      root.childMenuClosedAt = Date.now()
      root.suppressHoverUntilMove()
      root.notifyMenusOpen()
    }

    MenuItem {
      text: "Duplicate"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_copy_20_regular.svg"
      onTriggered: {
        const newId = root.model ? root.model.duplicateBlock(childOptionsMenu.blockId) : ""
        if (newId.length > 0)
          root.focusChildBlock(newId, 0)
      }
    }

    MenuSeparator {}

    MenuItem {
      text: "Delete block"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_delete_20_regular.svg"
      onTriggered: {
        if (root.model)
          root.model.removeBlock(childOptionsMenu.blockId)
      }
    }
  }

  Menu {
    id: columnOptionsMenu
    objectName: "columnOptionsMenu"
    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onAboutToShow: {
      root.suppressHoverUntilMove()
      root.notifyMenusOpen()
      root.isDraggingColumn = false
      root.draggingColumnIndex = -1
      root.dropColumnIndex = -1
      root.columnDragVisualX = 0
      root.columnDragVisualY = 0
      if (root.editorApi) {
        root.editorApi.isColumnDragActive = false
        root.editorApi.activeColumnDragRenderer = null
      }
    }
    onClosed: {
      root.columnMenuClosedAt = Date.now()
      root.suppressHoverUntilMove()
      root.notifyMenusOpen()
    }
    // Keep the column id; indices shift if columns are added or reordered
    // while the menu is open.
    property string targetColumnId: ""
    readonly property int targetIndex: root.columnIds.indexOf(targetColumnId)

    MenuItem {
      text: "Insert column left"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_add_20_regular.svg"
      onTriggered: {
        const index = columnOptionsMenu.targetIndex
        if (index < 0 || !root.model) return
        const before = index > 0 ? root.columnIds[index - 1] : ""
        root.model.insertColumn(root.blockId, before)
      }
    }

    MenuItem {
      text: "Insert column right"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_add_20_regular.svg"
      onTriggered: {
        const index = columnOptionsMenu.targetIndex
        if (index < 0 || index >= root.columnIds.length || !root.model) return
        root.model.insertColumn(root.blockId, root.columnIds[index])
      }
    }

    MenuSeparator {}

    MenuItem {
      text: "Move column left"
      enabled: columnOptionsMenu.targetIndex > 0
      onTriggered: {
        const index = columnOptionsMenu.targetIndex
        if (index > 0 && root.model)
          root.model.moveColumn(root.columnIds[index], -1)
      }
    }

    MenuItem {
      text: "Move column right"
      enabled: columnOptionsMenu.targetIndex >= 0
               && columnOptionsMenu.targetIndex < root.columnIds.length - 1
      onTriggered: {
        const index = columnOptionsMenu.targetIndex
        if (index >= 0 && index < root.columnIds.length - 1 && root.model)
          root.model.moveColumn(root.columnIds[index], 1)
      }
    }

    MenuItem {
      text: "Equalize widths"
      onTriggered: {
        if (root.model) root.model.equalizeColumns(root.blockId)
      }
    }

    MenuSeparator {}

    MenuItem {
      text: "Delete column"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_delete_20_regular.svg"
      onTriggered: {
        const index = columnOptionsMenu.targetIndex
        if (index >= 0 && index < root.columnIds.length && root.model)
          root.model.removeColumn(root.columnIds[index])
      }
    }
  }

  function notifyMenusOpen() {
    if (root.editorApi)
      root.editorApi.columnMenusOpen =
        childOptionsMenu.visible || columnOptionsMenu.visible
  }

  function openMenuAt(menu, globalX, globalY) {
    const p = root.mapFromItem(null, globalX, globalY)

    // Offset from the icon so it stays uncovered and clickable, letting the
    // outside-press policy close the menu.
    menu.popup(p.x + 16, p.y)
  }

  function toggleChildMenu(blockId, globalX, globalY, wasOpenAtPress) {
    childOptionsMenu.blockId = blockId

    // CloseOnPressOutside may already have closed it on this press. In that
    // case do not open it again from the release handler.
    if (wasOpenAtPress || childOptionsMenu.visible)
      childOptionsMenu.close()
    else
      openMenuAt(childOptionsMenu, globalX, globalY)
  }

  function toggleColumnMenu(columnId, globalX, globalY, wasOpenAtPress) {
    columnOptionsMenu.targetColumnId = columnId

    if (wasOpenAtPress || columnOptionsMenu.visible)
      columnOptionsMenu.close()
    else
      openMenuAt(columnOptionsMenu, globalX, globalY)
  }

  function firstEditableChildId() {
    if (!root.model || root.columnIds.length === 0)
      return ""

    const ids = root.model.blocksOfColumn(root.columnIds[0])

    if (ids.length === 0)
      return root.model.appendBlockToColumn(root.columnIds[0])

    return ids[0]
  }

  function focusEditor(position) {
    const id = root.firstEditableChildId()

    if (id.length > 0)
      root.focusChildBlock(id, position >= 0 ? position : 0)
  }

  function focusEditorAtX(xPosition) {
    if (!root.model || root.columnIds.length === 0)
      return

    let columnIndex = 0

    if (xPosition >= 0) {
      const index = root.columnIndexAtContentX(root.contentXAt(xPosition))

      if (index >= 0)
        columnIndex = index
    }

    const ids = root.model.blocksOfColumn(root.columnIds[columnIndex])
    const id = ids.length > 0
                ? ids[0]
                : root.model.appendBlockToColumn(root.columnIds[columnIndex])

    if (id.length > 0)
      root.focusChildBlock(id, 0)
  }

  function focusEditorRange(start, end) {
    const id = root.firstEditableChildId()

    if (id.length > 0)
      root.focusChildBlock(id, 0, start, end)
  }
  function toggleFormat(type) {}
  function toggleScriptFormat(type) {}
  function isFormatActive(type) { return false }
  function notifySelection() {}
  function refreshFromModel() { root.reload() }

  // Routes a focus intent from the editor to a child delegate inside the
  // correct column. The child may not be materialized yet, so retry briefly.
  function focusChildBlock(blockId, position, rangeStart, rangeEnd, xPosition) {
    root.pendingChildFocusQueue.push({
      blockId: blockId,
      position: position === undefined ? 0 : position,
      rangeStart: rangeStart === undefined ? -1 : rangeStart,
      rangeEnd: rangeEnd === undefined ? -1 : rangeEnd,
      x: xPosition === undefined ? -1 : xPosition
    })
    root.pendingAttempts = 0

    if (!focusRetryTimer.running)
      focusRetryTimer.restart()

    return true
  }

  Timer {
    id: focusRetryTimer
    interval: 16
    repeat: false
    onTriggered: root.applyPendingChildFocus()
  }

  // Horizontal auto-scroll while dragging a child near the layout's edges,
  // mirroring the editor's vertical dragAutoScrollTimer. Scrolling shifts the
  // columns under a stationary pointer, so the drop target is recomputed after
  // every step.
  Timer {
    id: childDragAutoScrollTimer
    interval: 16
    repeat: true
    running: root.childDragActive
             && columnsFlickable.contentWidth > columnsFlickable.width
    onTriggered: {
      if (!root.childDragActive)
        return

      const viewX = columnsFlickable.mapFromItem(
        null, root.childDragGlobalX, 0).x
      const edge = 40
      const maxContentX = columnsFlickable.contentWidth - columnsFlickable.width
      const contentX = columnsFlickable.contentX
      let nextX = contentX

      if (viewX < edge) {
        const factor = Math.max(0, Math.min(1, (edge - viewX) / edge))
        const step = (factor * 12) + 2
        nextX = Math.max(0, contentX - step)
      } else if (viewX > columnsFlickable.width - edge) {
        const factor = Math.max(0, Math.min(1,
          (viewX - (columnsFlickable.width - edge)) / edge))
        const step = (factor * 12) + 2
        nextX = Math.min(maxContentX, contentX + step)
      } else {
        return
      }

      if (Math.abs(nextX - contentX) < 0.01)
        return

      columnsFlickable.contentX = nextX
      root.updateChildDrag(root.childDragGlobalX, root.childDragGlobalY)
    }
  }

  // Horizontal auto-scroll while resizing a column past the editor edge. It
  // reuses the cached pointer X so scrolling continues even when the pointer
  // is held still at the edge, and keeps the pane under it via updateColumnResize.
  Timer {
    id: columnResizeAutoScrollTimer
    interval: 16
    repeat: true
    running: root.isResizingColumn
             && columnsFlickable.contentWidth > columnsFlickable.width
    onTriggered: {
      if (!root.isResizingColumn)
        return

      const viewX = columnsFlickable.mapFromItem(
        null, root.columnResizeGlobalX, 0).x
      const edge = 40
      const maxContentX = columnsFlickable.contentWidth - columnsFlickable.width
      const contentX = columnsFlickable.contentX
      let nextX = contentX

      if (viewX < edge) {
        const factor = Math.max(0, Math.min(1, (edge - viewX) / edge))
        const step = (factor * 12) + 2
        nextX = Math.max(0, contentX - step)
      } else if (viewX > columnsFlickable.width - edge) {
        const factor = Math.max(0, Math.min(1,
          (viewX - (columnsFlickable.width - edge)) / edge))
        const step = (factor * 12) + 2
        nextX = Math.min(maxContentX, contentX + step)
      } else {
        return
      }

      if (Math.abs(nextX - contentX) < 0.01)
        return

      columnsFlickable.contentX = nextX
      root.updateColumnResize(root.columnResizeGlobalX)
    }
  }

  function applyPendingChildFocus() {
    const intent = root.pendingChildFocusQueue[0]

    if (!intent)
      return

    const columnId = root.model ? root.model.columnIdOfBlock(intent.blockId) : ""

    if (columnId.length === 0) {
      root.finishChildFocus()
      return
    }

    const columnIndex = root.columnIds.indexOf(columnId)
    const pane = columnIndex >= 0 ? columnRepeater.itemAt(columnIndex) : null

    if (!pane || !pane.blockList) {
      root.retryChildFocus()
      return
    }

    const row = root.model.blocksOfColumn(columnId).indexOf(intent.blockId)
    const delegate = row >= 0 ? pane.blockList.itemAtIndex(row) : null

    if (!delegate || delegate.blockId !== intent.blockId) {
      root.retryChildFocus()
      return
    }

    root.finishChildFocus()
    delegate.refreshBlock()

    if (intent.x >= 0)
      delegate.focusBlockAtX(intent.x)
    else if (intent.rangeStart >= 0)
      delegate.focusBlockAtRange(intent.rangeStart, intent.rangeEnd)
    else
      delegate.focusBlock(intent.position)
  }

  function finishChildFocus() {
    root.pendingChildFocusQueue.shift()
    root.pendingAttempts = 0

    if (root.pendingChildFocusQueue.length > 0)
      focusRetryTimer.restart()
  }

  function retryChildFocus() {
    // Give slow-to-materialize delegates up to ~1s instead of 480ms.
    if (root.pendingAttempts < 60) {
      ++root.pendingAttempts
      focusRetryTimer.restart()
    } else {
      root.finishChildFocus()
    }
  }

  function beginChildDrag(blockId, globalX, globalY) {
    if (!root.model)
      return false

    const columnId = root.model.columnIdOfBlock(blockId)
    const columnIndex = root.columnIds.indexOf(columnId)

    if (columnIndex < 0)
      return false

    const row = root.model.blocksOfColumn(columnId).indexOf(blockId)
    const paneItem = columnRepeater.itemAt(columnIndex)
    const draggedDelegate = paneItem && paneItem.blockList
                            ? paneItem.blockList.itemAtIndex(row) : null

    root.dragChildId = blockId
    root.dropColumnId = columnId
    root.dropChildColumnIndex = columnIndex
    root.dropChildRow = row
    root.childDragSourceRow = row
    root.childDragSourceColumn = columnIndex
    root.childDragStartX = globalX
    root.childDragStartY = globalY
    root.childDragStartContentX = columnsFlickable.contentX
    root.childDragGlobalX = globalX
    root.childDragGlobalY = globalY
    root.childDraggingVisualX = 0
    root.childDraggingVisualY = 0
    root.childDraggingItemHeight = draggedDelegate ? draggedDelegate.height : 0
    root.childDragActive = true
    return true
  }

  function updateChildDrag(globalX, globalY) {
    if (!root.childDragActive || !root.model)
      return

    root.childDragGlobalX = globalX
    root.childDragGlobalY = globalY

    // Move the lifted delegate by the full pointer delta so it tracks the
    // cursor in both axes and can be dropped anywhere in the X/Y plane. The
    // contentX term cancels the Flickable's scroll so auto-scrolling shifts
    // the columns under the pointer without dragging the block off it.
    root.childDraggingVisualX = (globalX - root.childDragStartX)
                                + (columnsFlickable.contentX
                                   - root.childDragStartContentX)
    root.childDraggingVisualY = globalY - root.childDragStartY

    const relativeX = globalX - columnsContent.mapToItem(null, 0, 0).x
    const columnIndex = root.columnIndexForDrop(relativeX)
    const columnId = root.columnIds[columnIndex]
    const paneItem = columnRepeater.itemAt(columnIndex)

    root.dropColumnId = columnId
    root.dropChildColumnIndex = columnIndex

    if (!paneItem || !paneItem.blockList) {
      root.dropChildRow = 0
      return
    }

    const ids = root.model.blocksOfColumn(columnId)
    const listTop = paneItem.blockList.mapToItem(null, 0, 0).y
    let acc = 0
    let row = ids.length

    for (let i = 0; i < ids.length; ++i) {
      const delegate = paneItem.blockList.itemAtIndex(i)
      const height = delegate ? delegate.height : 24
      if (globalY < listTop + acc + height / 2) {
        row = i
        break
      }
      acc += height
    }

    root.dropChildRow = row
  }

  // Keeps the lifted column under the pointer and recomputes the drop target.
  // Also called after the editor scrolls the document vertically while the
  // pointer is held at an edge.
  function updateColumnDrag(globalX, globalY) {
    if (!root.isDraggingColumn)
      return

    root.columnDragGlobalX = globalX
    root.columnDragGlobalY = globalY
    root.columnDragVisualX = globalX - root.columnDragStartX
    root.columnDragVisualY = globalY - root.columnDragStartY

    const relativeX = globalX - columnsContent.mapToItem(null, 0, 0).x
    root.dropColumnIndex = root.columnInsertionSlot(relativeX)

    if (root.columnDropResolver) {
      root.columnDropTarget = root.columnDropResolver(globalX, globalY)

      if (root.columnDropTarget && root.columnDropTarget.kind === "column") {
        const index = root.columnIds.indexOf(root.columnDropTarget.columnId)
        const slot = root.columnDropTarget.slot

        root.dropColumnIndex = index >= 0 ? (slot >= 0 ? slot : index) : -1
      } else {
        root.dropColumnIndex = -1
      }
    }
  }

  function endChildDrag() {
    if (!root.childDragActive)
      return

    const blockId = root.dragChildId
    const sourceColumn = root.model ? root.model.columnIdOfBlock(blockId) : ""
    const sourceRow = sourceColumn.length > 0
                      ? root.model.blocksOfColumn(sourceColumn).indexOf(blockId)
                      : -1

    // dropChildRow is an insertion slot in the list that still contains the
    // dragged block. moveBlockToColumn() removes it first, so a downward move
    // within the same column needs the slot shifted back by one. The editor's
    // top-level drag path applies the same correction.
    let targetRow = root.dropChildRow

    if (root.dropColumnId === sourceColumn && sourceRow >= 0
        && targetRow > sourceRow)
      --targetRow

    if (root.dropColumnId.length > 0 && targetRow >= 0
        && (root.dropColumnId !== sourceColumn || targetRow !== sourceRow)) {
      root.model.moveBlockToColumn(blockId, root.dropColumnId, targetRow)
    }

    root.childDragActive = false
    root.dragChildId = ""
    root.dropColumnId = ""
    root.dropChildRow = -1
    root.dropChildColumnIndex = -1
    root.childDragSourceRow = -1
    root.childDragSourceColumn = -1
    root.childDraggingVisualX = 0
    root.childDraggingVisualY = 0
    root.childDraggingItemHeight = 0
    root.childDragStartX = 0
    root.childDragStartY = 0
    root.childDragStartContentX = 0
    root.childDragGlobalX = 0
    root.childDragGlobalY = 0
  }

  // Set by the editor so column drags can resolve a target anywhere in the
  // document, not just among this layout's panes.
  property var columnDropResolver: null
  // Editor API for handing a whole-layout drag off to the top-level drag code.
  property var editorApi: null

  // Returns the column id under a global point, or "" if the point is outside
  // every pane. Used by the editor to route drops onto a specific column.
  function columnAtPoint(globalX, globalY) {
    if (!root.model)
      return ""

    for (let i = 0; i < root.columnIds.length; ++i) {
      const pane = columnRepeater.itemAt(i)

      if (!pane)
        continue

      const pos = pane.mapFromItem(null, globalX, globalY)

      // Use the layout's height, not this pane's own content height: a short
      // column still owns the blank space beside a taller neighbour.
      if (pos.x >= 0 && pos.x <= pane.width
          && pos.y >= 0 && pos.y <= columnsFlickable.height)
        return root.columnIds[i]
    }

    return ""
  }

  // Row within a column at a global point, for dropping editor blocks into it.
  function childRowAtPoint(globalX, globalY) {
    const columnId = root.columnAtPoint(globalX, globalY)

    if (columnId.length === 0)
      return -1

    const pane = columnRepeater.itemAt(root.columnIds.indexOf(columnId))

    if (!pane || !pane.blockList)
      return 0

    const ids = root.model.blocksOfColumn(columnId)
    const listTop = pane.blockList.mapToItem(null, 0, 0).y
    let acc = 0

    for (let i = 0; i < ids.length; ++i) {
      const delegate = pane.blockList.itemAtIndex(i)
      const height = delegate ? delegate.height : 24

      if (globalY < listTop + acc + height / 2)
        return i

      acc += height
    }

    return ids.length
  }

  // Column gutter handles can overlap the editor margin when the layout is
  // scrolled or the sheet is narrow. Resolve the handle under a global point
  // by checking every pane's gutter strip, not just the first column.
  function childIdAtGutterPoint(globalX, globalY) {
    if (!root.model || root.columnIds.length === 0)
      return ""

    for (let i = 0; i < root.columnIds.length; ++i) {
      const pane = columnRepeater.itemAt(i)

      if (!pane || !pane.blockList)
        continue

      const pos = pane.mapFromItem(null, globalX, globalY)

      if (pos.x < 0 || pos.x > root.paneGutter)
        continue

      const listTop = pane.blockList.mapToItem(null, 0, 0).y
      const ids = root.model.blocksOfColumn(pane.columnId)
      let acc = 0

      for (let j = 0; j < ids.length; ++j) {
        const delegate = pane.blockList.itemAtIndex(j)
        const height = delegate ? delegate.height : 24

        if (globalY >= listTop + acc && globalY < listTop + acc + height)
          return ids[j]

        acc += height
      }

      return ids.length > 0 ? ids[ids.length - 1] : ""
    }

    return ""
  }

  function updateColumnResize(globalX) {
    if (!root.isResizingColumn || !root.model
        || root.columnResizeColumnId.length === 0)
      return

    root.columnResizeGlobalX = globalX

    // The divider is anchored to the pane's right edge inside the scrollable
    // content. When the Flickable scrolls, the content moves under the pointer,
    // so add the contentX delta to keep the divider on the cursor and let the
    // pane grow past the visible width.
    const delta = (globalX - root.columnResizeStartX)
                  + (columnsFlickable.contentX - root.columnResizeStartContentX)

    // Clamp here as well as in the model so the divider stops exactly where
    // the pane stops shrinking instead of detaching from the cursor.
    const width = Math.max(root.minColumnWidth,
                           root.columnResizeStartWidth + delta)
    root.model.setColumnWidth(root.columnResizeColumnId, width)
  }

  readonly property real minColumnWidth: 80
  readonly property real columnSpacing: 4
  // The Flickable starts one gutter-width left of the writing edge so the
  // first column's handle sits in the editor margin (where the editor owns
  // its gestures) while its text still starts exactly at the writing edge.
  readonly property real paneGutter: 20
  readonly property real gutter: -paneGutter
  readonly property real addStripWidth: 18
  // Enough room for the 14px pill hot area without overlapping text.
  readonly property real headerHeight: 16
  // Blank space under the last block of a column; clicking it appends a block
  // to that column, mirroring the editor footer behaviour.
  readonly property real trailingSpace: 40
  property bool isResizingColumn: false
  property string columnResizeColumnId: ""
  property real columnResizeStartX: 0
  property real columnResizeStartWidth: 0
  property real columnResizeStartContentX: 0
  property real columnResizeGlobalX: 0

  // QML bindings that call a model method are not automatically re-evaluated
  // when the model changes. Touching this revision makes column widths live.
  property int widthRevision: 0

  readonly property real totalColumnsWidth: {
    root.widthRevision
    let total = root.addStripWidth
    for (let i = 0; i < root.columnIds.length; ++i)
      total += root.model ? root.model.columnWidth(root.columnIds[i]) : 0
    total += Math.max(0, root.columnIds.length - 1) * root.columnSpacing
    return total
  }

  implicitWidth: parent ? parent.width : 600
  implicitHeight: {
    let tallest = 0
    for (let i = 0; i < columnRepeater.count; ++i) {
      const pane = columnRepeater.itemAt(i)
      if (pane)
        tallest = Math.max(tallest, pane.totalHeight)
    }
    return tallest
  }

  Flickable {
    id: columnsFlickable
    objectName: "columnsFlickable"
    anchors {
      left: parent.left
      leftMargin: root.gutter
      right: parent.right
      top: parent.top
    }
    height: root.implicitHeight
    contentWidth: root.totalColumnsWidth
    contentHeight: height
    clip: true
    interactive: contentWidth > width
                  && !root.isDraggingColumn
                  && !root.isResizingColumn
    flickableDirection: Flickable.HorizontalFlick
    boundsBehavior: Flickable.StopAtBounds
    maximumFlickVelocity: 4000

    Item {
      id: columnsContent
      width: root.totalColumnsWidth
      height: columnsFlickable.height

      Repeater {
        id: columnRepeater
        model: root.columnIds.length

        delegate: Item {
          id: pane
          required property int index
          property alias blockList: blockList

          // Lift the source pane while a child is being dragged so the
          // translated delegate paints above the neighbouring panes it
          // travels across (panes are siblings with no stacking by default).
          z: root.childDragActive && root.childDragSourceColumn === pane.index
             ? 50 : 1

          readonly property string columnId: root.columnIds[index]
          property var childIds: []
          property int childRevision: blockList ? blockList.count : 0
          onChildRevisionChanged: pane.refreshChildIds()

          function refreshChildIds() {
            const ids = root.model ? root.model.blocksOfColumn(columnId) : []

            // Reassigning the array resets the handle Repeater even when the
            // child set is unchanged (e.g. on every text edit).
            if (ids.length === pane.childIds.length) {
              let same = true

              for (let i = 0; i < ids.length; ++i) {
                if (ids[i] !== pane.childIds[i]) {
                  same = false
                  break
                }
              }

              if (same)
                return
            }

            pane.childIds = ids
          }

          Connections {
            target: root.model
            function onCountChanged() { pane.refreshChildIds() }
            function onDataChanged() { pane.refreshChildIds() }
            function onRowsMoved() { pane.refreshChildIds() }
          }

          Component.onCompleted: pane.refreshChildIds()

          readonly property real paneWidth: {
            root.widthRevision
            return root.model ? root.model.columnWidth(columnId) : 0
          }
          readonly property int totalHeight: root.headerHeight + root.trailingSpace
                                             + (blockList ? blockList.contentHeight : 0)

          x: {
            let offset = 0
            for (let i = 0; i < index; ++i) {
              const previous = columnRepeater.itemAt(i)
              offset += (previous ? previous.paneWidth : 0) + root.columnSpacing
            }
            return offset
          }
          width: paneWidth
          height: totalHeight

          Behavior on width {
            enabled: !root.isResizingColumn && !root.isDraggingColumn
            NumberAnimation { duration: 220; easing.type: Easing.OutCubic }
          }

          // Column handle: only the pill under the pointer is shown, with the
          // same fade/color animation and drag feedback as table columns.
          Item {
            id: columnHandle
            anchors {
              top: parent.top
              topMargin: Math.round((root.headerHeight - 14) / 2)
              horizontalCenter: parent.horizontalCenter
            }
            width: 36
            height: 14
            z: 5
            transform: Translate {
              x: root.isDraggingColumn && root.draggingColumnIndex === pane.index
                 ? root.columnDragVisualX : 0
              y: root.isDraggingColumn && root.draggingColumnIndex === pane.index
                 ? root.columnDragVisualY : 0
            }
            IconImage {
              id: columnPill
              objectName: "columnDragPill"
              anchors.centerIn: parent
              width: 20
              height: 20
              sourceSize.width: 20
              sourceSize.height: 20
              source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_re_order_dots_horizontal_20_regular.svg"
              color: (!root.hoverSuppressed && handleArea.containsMouse)
                     || root.hoveredColumnIndex === pane.index
                     || (root.isDraggingColumn
                         && root.draggingColumnIndex === pane.index)
                     ? Theme.accent
                     : Theme.tint(Theme.text, Theme.isDark ? 0.3 : 0.25)
              opacity: childOptionsMenu.visible || columnOptionsMenu.visible
                       ? 0.0
                       : ((!root.hoverSuppressed && handleArea.containsMouse)
                          || root.hoveredColumnIndex === pane.index
                          || (root.isDraggingColumn
                              && (root.draggingColumnIndex === pane.index
                                  || root.dropColumnIndex === pane.index))
                          ? 1.0 : 0.0)

              Behavior on color { ColorAnimation { duration: 120 } }
              Behavior on opacity { NumberAnimation { duration: 120 } }
            }

            MouseArea {
              id: handleArea
              anchors.fill: parent
              hoverEnabled: true
              cursorShape: Qt.PointingHandCursor
              acceptedButtons: Qt.LeftButton | Qt.RightButton

              property int pressedButton: Qt.NoButton
              property bool movedWhilePressed: false

              onPressed: function(mouse) {
                pressedButton = mouse.button
                movedWhilePressed = false
                // CloseOnPressOutside can close the menu before this handler
                // runs, so a very recent close still counts as "was open".
                root.columnMenuOpenAtPress = columnOptionsMenu.visible
                                             || (Date.now()
                                                 - root.columnMenuClosedAt < 100)

                // Press starts a column drag immediately, like TableBlock.
                if (mouse.button === Qt.LeftButton) {
                  const global = mapToItem(null, mouse.x, mouse.y)
                  root.columnDragStartX = global.x
                  root.columnDragStartY = global.y
                  root.columnDragVisualX = 0
                  root.columnDragVisualY = 0
                  root.columnDropTarget = null
                  root.isDraggingColumn = true
                  root.draggingColumnIndex = pane.index
                  root.dropColumnIndex = pane.index
                  root.columnDragGlobalX = global.x
                  root.columnDragGlobalY = global.y

                  if (root.editorApi) {
                    root.editorApi.isColumnDragActive = true
                    root.editorApi.activeColumnDragRenderer = root
                    root.editorApi.columnDragGlobalX = global.x
                    root.editorApi.columnDragGlobalY = global.y
                    root.editorApi.dragGlobalX = global.x
                    root.editorApi.dragGlobalY = global.y
                  }

                  handleArea.cursorShape = Qt.ClosedHandCursor
                }
              }

              onPositionChanged: function(mouse) {
                if (!root.isDraggingColumn || pressedButton !== Qt.LeftButton)
                  return

                const global = mapToItem(null, mouse.x, mouse.y)
                root.updateColumnDrag(global.x, global.y)

                if (Math.abs(root.columnDragVisualX)
                    + Math.abs(root.columnDragVisualY) > 4)
                  movedWhilePressed = true

                if (root.editorApi) {
                  root.editorApi.columnDragGlobalX = global.x
                  root.editorApi.columnDragGlobalY = global.y
                  root.editorApi.dragGlobalX = global.x
                  root.editorApi.dragGlobalY = global.y
                }
              }

              onReleased: function(mouse) {
                handleArea.cursorShape = Qt.PointingHandCursor
                const releaseGlobal = mapToItem(null, mouse.x, mouse.y)

                if (mouse.button === Qt.RightButton) {
                  root.toggleColumnMenu(pane.columnId, releaseGlobal.x,
                                        releaseGlobal.y,
                                        root.columnMenuOpenAtPress)
                  pressedButton = Qt.NoButton
                  return
                }

                if (root.isDraggingColumn) {
                  const sourceIndex = root.draggingColumnIndex
                  const sourceId = sourceIndex >= 0
                                   ? root.columnIds[sourceIndex] : ""
                  const target = root.columnDropTarget
                  const localTarget = root.dropColumnIndex

                  root.isDraggingColumn = false
                  root.draggingColumnIndex = -1
                  root.dropColumnIndex = -1
                  root.columnDropTarget = null
                  root.columnDragVisualX = 0
                  root.columnDragVisualY = 0

                  if (root.editorApi) {
                    root.editorApi.isColumnDragActive = false
                    root.editorApi.activeColumnDragRenderer = null
                  }

                  if (sourceId.length > 0 && root.model) {
                    if (target && target.kind === "column") {
                      const targetLayout =
                        root.model.parentBlockId(target.columnId)

                      const slot = target.slot >= 0 ? target.slot : -1

                      if (targetLayout === root.blockId) {
                        const index = root.columnIds.indexOf(target.columnId)

                        if (index >= 0) {
                          const insertSlot = slot >= 0 ? slot : index
                          const finalIndex =
                            root.finalColumnIndex(sourceIndex, insertSlot)

                          if (finalIndex !== sourceIndex)
                            root.model.moveColumnToIndex(sourceId, finalIndex)
                          else if (!movedWhilePressed) {
                            root.toggleColumnMenu(pane.columnId,
                                                  releaseGlobal.x,
                                                  releaseGlobal.y,
                                                  root.columnMenuOpenAtPress)
                          }
                        }
                      } else {
                        const ids = root.model.columnIdsOf(targetLayout)
                        root.model.moveColumnToLayout(
                          sourceId, targetLayout,
                          slot >= 0 ? slot : ids.indexOf(target.columnId))
                      }
                    } else if (target && target.kind === "editor") {
                      root.model.extractColumnToEditor(sourceId,
                                                       target.visibleRow)
                    } else if (localTarget >= 0
                               && root.finalColumnIndex(sourceIndex,
                                                        localTarget) !== sourceIndex) {
                      root.model.moveColumnToIndex(
                        sourceId, root.finalColumnIndex(sourceIndex, localTarget))
                    } else if (!movedWhilePressed) {
                      // A click without movement opens the column menu.
                      root.toggleColumnMenu(pane.columnId, releaseGlobal.x,
                                            releaseGlobal.y,
                                            root.columnMenuOpenAtPress)
                    }
                  } else if (!movedWhilePressed) {
                    root.toggleColumnMenu(pane.columnId, releaseGlobal.x,
                                          releaseGlobal.y,
                                          root.columnMenuOpenAtPress)
                  }
                }

                pressedButton = Qt.NoButton
              }

              onCanceled: {
                handleArea.cursorShape = Qt.PointingHandCursor
                root.isDraggingColumn = false
                root.draggingColumnIndex = -1
                root.dropColumnIndex = -1
                root.columnDropTarget = null
                root.columnDragVisualX = 0
                root.columnDragVisualY = 0

                if (root.editorApi) {
                  root.editorApi.isColumnDragActive = false
                  root.editorApi.activeColumnDragRenderer = null
                }

                pressedButton = Qt.NoButton
              }
            }
          }

          // Gutter handle for each block in this column. The Flickable starts
          // 20px left of the writing edge, so x=0 here puts the icon at the
          // same -20px offset as the main editor's gutter icons.
          Repeater {
            model: pane.childIds

            delegate: Item {
              id: childHandle
              required property int index
              required property var modelData

              x: 0
              width: 20
              z: 8

              readonly property bool pillHovered: !root.hoverSuppressed
                                                  && handleArea.containsMouse

              function alignedBlock() {
                const list = pane.blockList

                if (!list)
                  return null

                // The handles Repeater and the block list share the same
                // order, so the ListView can resolve the delegate directly
                // instead of scanning every child for a matching id.
                return list.itemAtIndex(index)
              }

              height: {
                pane.blockList ? pane.blockList.contentHeight : 0
                const block = childHandle.alignedBlock()
                return block ? (block.handleHeight || block.height) : 24
              }

              y: {
                pane.blockList ? pane.blockList.contentHeight : 0
                const block = childHandle.alignedBlock()
                return block ? root.headerHeight + block.y : root.headerHeight
              }

              IconImage {
                id: childPill
                objectName: "nestedChildPill"
                anchors.centerIn: parent
                width: 20
                height: 20
                sourceSize.width: 20
                sourceSize.height: 20
                source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_re_order_dots_vertical_20_regular.svg"
                color: childHandle.pillHovered
                       || (root.childDragActive
                           && root.dragChildId === modelData)
                       ? Theme.accent
                       : Theme.tint(Theme.text, Theme.isDark ? 0.3 : 0.25)
                opacity: childOptionsMenu.visible || columnOptionsMenu.visible
                         ? 0.0
                         : (childHandle.pillHovered
                            || root.hoveredChildBlockId === modelData
                            || (root.childDragActive
                                && root.dragChildId === modelData)
                            ? 1.0 : 0.0)

                Behavior on color { ColorAnimation { duration: 120 } }
                Behavior on opacity { NumberAnimation { duration: 120 } }
              }

              MouseArea {
                id: handleArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                preventStealing: true

                // The first child of the leftmost column owns the layout's
                // gutter: Ctrl/Shift + drag moves the whole layout, while a
                // plain drag still moves that child block.
                readonly property bool ownsLayoutGutter: pane.index === 0
                                                          && pane.childIds.length > 0
                                                          && modelData === pane.childIds[0]
                property bool layoutDragModifier: false
                property bool layoutDragStarted: false

                Timer {
                  id: holdTimer
                  interval: 200
                  repeat: false
                  onTriggered: {
                    if (!handleArea.pressed)
                      return
                    // Seed the drag with the actual pointer position, not the
                    // handle centre, so the block does not jump on first move.
                    const point = handleArea.mapToItem(null,
                                                       handleArea.mouseX,
                                                       handleArea.mouseY)
                    if (root.beginChildDrag(modelData,
                                            point.x, point.y)) {
                      handleArea.cursorShape = Qt.ClosedHandCursor
                      childHandle.z = 20
                    }
                  }
                }

                onPressed: function(mouse) {
                  root.childMenuOpenAtPress = childOptionsMenu.visible
                                              || (Date.now()
                                                  - root.childMenuClosedAt < 100)
                  layoutDragModifier = !!root.editorApi && ownsLayoutGutter
                                       && (mouse.modifiers
                                           & (Qt.ControlModifier | Qt.ShiftModifier))
                                       !== 0
                  layoutDragStarted = false

                  if (!layoutDragModifier)
                    holdTimer.restart()
                }

                onPositionChanged: function(mouse) {
                  const global = handleArea.mapToItem(null, mouse.x, mouse.y)

                  if (layoutDragModifier && pressed) {
                    if (!layoutDragStarted) {
                      if (root.editorApi.beginLayoutDrag(root.blockId,
                                                          global.x, global.y))
                        layoutDragStarted = true
                    } else {
                      root.editorApi.updateLayoutDrag(global.x, global.y)
                    }
                    return
                  }

                  if (!root.childDragActive)
                    return
                  root.updateChildDrag(global.x, global.y)
                }

                onReleased: function(mouse) {
                  holdTimer.stop()
                  handleArea.cursorShape = Qt.PointingHandCursor
                  childHandle.z = 8
                  const releaseGlobal = mapToItem(null, mouse.x, mouse.y)

                  if (layoutDragModifier) {
                    if (layoutDragStarted)
                      root.editorApi.endLayoutDrag()
                    layoutDragModifier = false
                    layoutDragStarted = false
                    return
                  }

                  if (root.childDragActive) {
                    root.endChildDrag()
                  } else if (mouse.button === Qt.LeftButton) {
                    root.toggleChildMenu(modelData, releaseGlobal.x,
                                         releaseGlobal.y,
                                         root.childMenuOpenAtPress)
                  }
                }

                onCanceled: {
                  holdTimer.stop()
                  handleArea.cursorShape = Qt.PointingHandCursor
                  childHandle.z = 8

                  if (layoutDragModifier) {
                    if (layoutDragStarted)
                      root.editorApi.cancelLayoutDrag()
                    layoutDragModifier = false
                    layoutDragStarted = false
                    return
                  }

                  root.childDragActive = false
                  root.dragChildId = ""
                  root.dropColumnId = ""
                  root.dropChildRow = -1
                  root.dropChildColumnIndex = -1
                  root.childDragSourceRow = -1
                  root.childDragSourceColumn = -1
                  root.childDraggingVisualX = 0
                  root.childDraggingVisualY = 0
                  root.childDraggingItemHeight = 0
                  root.childDragStartX = 0
                  root.childDragStartY = 0
                  root.childDragStartContentX = 0
                  root.childDragGlobalX = 0
                  root.childDragGlobalY = 0
                }
              }
            }
          }

          // Resize divider between columns.
          Item {
            id: divider
            objectName: "columnResizeDivider"
            anchors {
              right: parent.right
              rightMargin: -6
              top: parent.top
              topMargin: root.headerHeight
              bottom: parent.bottom
            }
            width: 12
            z: 6
            // Every pane owns its own slider at its right edge, including the
            // last one. Resizing is independent of the neighbouring panes.
            visible: true

            Rectangle {
              anchors.centerIn: parent
              width: 2
              height: parent.height
              radius: 1
              color: Theme.accent
              opacity: dividerArea.containsMouse || resizeMouse.pressed
                       || root.hoveredDividerIndex === pane.index ? 1.0 : 0.0
              Behavior on opacity { NumberAnimation { duration: 150 } }
            }

            MouseArea {
              id: dividerArea
              anchors.fill: parent
              hoverEnabled: true
              cursorShape: Qt.SplitHCursor
            }

            MouseArea {
              id: resizeMouse
              anchors.fill: parent
              cursorShape: Qt.SplitHCursor

              onPressed: function(mouse) {
                const globalX = mapToItem(null, mouse.x, mouse.y).x
                root.columnResizeStartX = globalX
                root.columnResizeGlobalX = globalX
                root.columnResizeStartWidth = pane.paneWidth
                root.columnResizeStartContentX = columnsFlickable.contentX
                root.columnResizeColumnId = pane.columnId
                root.isResizingColumn = true
                if (root.model)
                  root.model.beginColumnResize(root.blockId)
              }

              onPositionChanged: function(mouse) {
                if (!pressed || !root.model) return
                // Each pane has its own slider; only this pane's width changes,
                // so neighbours keep the size the user gave them.
                root.updateColumnResize(mapToItem(null, mouse.x, mouse.y).x)
              }

              onReleased: {
                root.isResizingColumn = false
                root.columnResizeColumnId = ""
                if (root.model)
                  root.model.endColumnResize()
              }

              onCanceled: {
                root.isResizingColumn = false
                root.columnResizeColumnId = ""
                if (root.model)
                  root.model.endColumnResize()
              }
            }
          }

          ListView {
            id: blockList
            anchors {
              left: parent.left
              leftMargin: root.paneGutter
              right: parent.right
              top: parent.top
              topMargin: root.headerHeight
            }
            height: contentHeight
            model: root.model ? root.model.blocksModelForColumn(pane.columnId) : null
            delegate: root.delegateComponent
            interactive: false
            // The reorder handle sits in the gutter to the left of the block,
            // outside this list's content bounds.
            clip: false
            spacing: 0
            reuseItems: true
            cacheBuffer: 200
          }

          // Insertion line for dragging a block inside or between columns.
          Rectangle {
            objectName: "columnChildDropLine"
            visible: root.childDragActive
                     && root.dropChildColumnIndex === pane.index
                     && root.dropChildRow >= 0
            anchors {
              left: parent.left
              leftMargin: root.paneGutter
              right: parent.right
            }
            height: 2
            radius: 1
            color: Theme.accent
            z: 10
            y: {
              let position = root.headerHeight
              const list = pane.blockList

              if (list) {
                for (let i = 0; i < root.dropChildRow; ++i) {
                  const delegate = list.itemAtIndex(i)
                  position += delegate ? delegate.height : 24
                }
              }

              return position
            }
          }

          // Clicking the blank tail of a column appends a block there.
          MouseArea {
            anchors {
              left: parent.left
              leftMargin: root.paneGutter
              right: parent.right
              top: parent.top
              topMargin: root.headerHeight
                         + (blockList ? blockList.contentHeight : 0)
              bottom: parent.bottom
            }
            z: 2

            onClicked: {
              if (!root.model)
                return

              const id = root.model.appendBlockToColumn(pane.columnId)
              if (id.length > 0)
                root.focusChildBlock(id, 0)
            }
          }
        }
      }

      // Insertion indicator for hold-drag column reordering.
      Rectangle {
        objectName: "columnInsertionLine"
        visible: root.isDraggingColumn && root.dropColumnIndex >= 0
        width: 3
        height: columnsFlickable.height
        radius: 1.5
        color: Theme.accent
        z: 30
        x: {
          let position = 0
          for (let i = 0; i < root.dropColumnIndex && i < root.columnIds.length; ++i) {
            const item = columnRepeater.itemAt(i)
            position += (item ? item.paneWidth : 0) + root.columnSpacing
          }

          // The last slot is the right edge of the final pane, before the add
          // strip, not past the end of the scrollable content.
          position = Math.min(position, root.totalColumnsWidth - root.addStripWidth)
          return position - 2
        }
      }

      // Add-column strip, placed after the last column inside the scrollable
      // content so it stays reachable when the layout overflows the editor.
      Item {
        id: addColumnStrip
        anchors {
          right: parent.right
          top: parent.top
        }
        width: root.addStripWidth
        height: 28
        // The last divider extends 6px into the strip; the strip wins there.
        z: 7
        visible: opacity > 0.001
        opacity: root.isBlockHovered || root.addColumnHovered ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 180 } }

        Rectangle {
          anchors.fill: parent
          radius: 5
          color: root.addColumnHovered
                 ? Theme.surfaceHover
                 : "transparent"
          border.color: root.addColumnHovered ? Theme.accent : Theme.border
          border.width: 1

          IconImage {
            anchors.centerIn: parent
            width: 14
            height: 14
            source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_add_20_regular.svg"
            color: root.addColumnHovered ? Theme.accent : Theme.textMuted
          }
        }

        MouseArea {
          anchors.fill: parent
          cursorShape: Qt.PointingHandCursor
          onClicked: {
            if (!root.model || root.columnIds.length === 0) return
            root.model.insertColumn(root.blockId, root.columnIds[root.columnIds.length - 1])
          }
        }
      }
    }
  }


}
