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
  signal focusDelegateRequested(int row, int position)

  property var columnIds: []
  property var pendingChildFocus: null
  property int pendingAttempts: 0

  function columnIndexAtContentX(x) {
    let acc = 0
    for (let i = 0; i < root.columnIds.length; ++i) {
      const item = columnRepeater.itemAt(i)
      const width = item ? item.paneWidth : 0
      if (x < acc + width)
        return i
      acc += width + root.columnSpacing
    }
    return root.columnIds.length - 1
  }

  // Hover for nested blocks cannot rely on their own MouseAreas: the layout's
  // top-level hover area sits above them. Use the forwarded block hover point
  // instead, exactly like the column pill and divider.
  readonly property string hoveredChildBlockId: {
    if (!root.isBlockHovered || root.hoverX < 0 || root.hoverY < 0
        || !root.model || root.columnIds.length === 0)
      return ""

    const contentX = root.hoverX - 30
                     + (columnsFlickable ? columnsFlickable.contentX : 0)
    const columnIndex = root.columnIndexAtContentX(contentX)
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
    if (!root.isBlockHovered || root.hoverX < 0)
      return -1

    const x = root.hoverX - 30
              + (columnsFlickable ? columnsFlickable.contentX : 0)
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
    if (!root.isBlockHovered || root.hoverX < 0)
      return -1

    const x = root.hoverX - 30
              + (columnsFlickable ? columnsFlickable.contentX : 0)
    let acc = 0

    for (let i = 0; i < root.columnIds.length; ++i) {
      const item = columnRepeater.itemAt(i)
      const width = item ? item.paneWidth : 0
      const edge = acc + width

      if (x >= edge - 12 && x <= edge + 12)
        return i

      acc += width + root.columnSpacing
    }

    return -1
  }

  // Column drag state (press starts a drag, like table columns).
  property bool isDraggingColumn: false
  property int draggingColumnIndex: -1
  property int dropColumnIndex: -1

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

  // Flat list of nested blocks used by the gutter-handle Repeater. Handles are
  // owned by the layout root (outside the clipped Flickable) so they can sit at
  // the same -20px offset as main-editor handles.
  property var allChildHandles: []

  function refreshHandles() {
    if (!root.model) {
      root.allChildHandles = []
      return
    }

    const list = []

    for (let c = 0; c < root.columnIds.length; ++c) {
      const columnId = root.columnIds[c]
      const ids = root.model.blocksOfColumn(columnId)

      for (let r = 0; r < ids.length; ++r) {
        list.push({ blockId: ids[r], columnIndex: c, row: r })
      }
    }

    root.allChildHandles = list
  }

  function reload() {
    if (!root.model || root.blockId.length === 0)
      return

    const ids = root.model.columnIdsOf(root.blockId)

    if (ids.length !== root.columnIds.length) {
      root.columnIds = ids
      root.refreshHandles()
      return
    }

    for (let i = 0; i < ids.length; ++i) {
      if (ids[i] !== root.columnIds[i]) {
        root.columnIds = ids
        root.refreshHandles()
        return
      }
    }

    root.refreshHandles()
  }

  property int docRevision: root.model ? root.model.contentRevision : -1
  onDocRevisionChanged: root.reload()

  onBlockIdChanged: root.reload()
  onModelChanged: root.reload()
  Component.onCompleted: root.reload()

  Connections {
    target: root.model
    function onCountChanged() {
      ++root.widthRevision
      root.reload()
    }
    function onDataChanged() {
      ++root.widthRevision
      root.reload()
    }
    function onRowsMoved() { root.reload() }
  }

  Menu {
    id: childOptionsMenu
    property string blockId: ""

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
    property int targetIndex: -1

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

  function focusEditor(position) {
    if (root.columnIds.length > 0)
      root.focusChildBlock(root.columnIds[0], 0)
  }

  function focusEditorAtX(xPosition) { root.focusEditor(0) }
  function focusEditorRange(start, end) { root.focusEditor(0) }
  function toggleFormat(type) {}
  function toggleScriptFormat(type) {}
  function isFormatActive(type) { return false }
  function notifySelection() {}
  function refreshFromModel() { root.reload() }

  // Routes a focus intent from the editor to a child delegate inside the
  // correct column. The child may not be materialized yet, so retry briefly.
  function focusChildBlock(blockId, position, rangeStart, rangeEnd, xPosition) {
    root.pendingChildFocus = {
      blockId: blockId,
      position: position === undefined ? 0 : position,
      rangeStart: rangeStart === undefined ? -1 : rangeStart,
      rangeEnd: rangeEnd === undefined ? -1 : rangeEnd,
      x: xPosition === undefined ? -1 : xPosition
    }
    root.pendingAttempts = 0
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
    const intent = root.pendingChildFocus

    if (!intent)
      return

    const columnId = root.model ? root.model.columnIdOfBlock(intent.blockId) : ""
    if (columnId.length === 0) {
      root.pendingChildFocus = null
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

    root.pendingChildFocus = null
    root.pendingAttempts = 0
    delegate.refreshBlock()

    if (intent.x >= 0)
      delegate.focusBlockAtX(intent.x)
    else if (intent.rangeStart >= 0)
      delegate.focusBlockAtRange(intent.rangeStart, intent.rangeEnd)
    else
      delegate.focusBlock(intent.position)
  }

  function retryChildFocus() {
    if (root.pendingAttempts < 30) {
      ++root.pendingAttempts
      focusRetryTimer.restart()
    } else {
      root.pendingChildFocus = null
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
    let running = 0
    let columnIndex = 0

    for (let i = 0; i < root.columnIds.length; ++i) {
      const item = columnRepeater.itemAt(i)
      const width = item ? item.paneWidth : 0
      if (relativeX > running + width / 2)
        columnIndex = i
      running += width + root.columnSpacing
    }

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

  function endChildDrag() {
    if (!root.childDragActive)
      return

    const blockId = root.dragChildId
    const sourceColumn = root.model ? root.model.columnIdOfBlock(blockId) : ""
    const sourceRow = sourceColumn.length > 0
                      ? root.model.blocksOfColumn(sourceColumn).indexOf(blockId)
                      : -1

    if (root.dropColumnId.length > 0 && root.dropChildRow >= 0
        && (root.dropColumnId !== sourceColumn
            || root.dropChildRow !== sourceRow)) {
      root.model.moveBlockToColumn(blockId, root.dropColumnId,
                                   root.dropChildRow)
    }

    root.cancelChildDrag()
  }

  function cancelChildDrag() {
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
    root.model.setColumnWidth(root.columnResizeColumnId,
                              root.columnResizeStartWidth + delta)
  }

  readonly property real columnSpacing: 8
  // No outer indent: column content starts at the normal writing edge. The
  // only gutter kept is the one inside each pane for its reorder handle.
  readonly property real gutter: 0
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
    return tallest + 4
  }

  Flickable {
    id: columnsFlickable
    objectName: "columnsFlickable"
    anchors {
      left: parent.left
      leftMargin: root.gutter
      right: parent.right
      top: parent.top
      topMargin: 2
    }
    height: root.implicitHeight - 4
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
            pane.childIds = root.model ? root.model.blocksOfColumn(columnId) : []
            root.refreshHandles()
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
            IconImage {
              id: columnPill
              anchors.centerIn: parent
              width: 20
              height: 20
              sourceSize.width: 20
              sourceSize.height: 20
              source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_re_order_dots_horizontal_20_regular.svg"
              color: handleArea.containsMouse
                     || root.hoveredColumnIndex === pane.index
                     || (root.isDraggingColumn
                         && root.draggingColumnIndex === pane.index)
                     ? Theme.accent
                     : Theme.tint(Theme.text, Theme.isDark ? 0.3 : 0.25)
              opacity: handleArea.containsMouse
                       || root.hoveredColumnIndex === pane.index
                       || (root.isDraggingColumn
                           && (root.draggingColumnIndex === pane.index
                               || root.dropColumnIndex === pane.index))
                       ? 1.0 : 0.0

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

                // Press starts a column drag immediately, like TableBlock.
                if (mouse.button === Qt.LeftButton) {
                  root.isDraggingColumn = true
                  root.draggingColumnIndex = pane.index
                  root.dropColumnIndex = pane.index
                  handleArea.cursorShape = Qt.ClosedHandCursor
                }
              }

              onPositionChanged: function(mouse) {
                if (!root.isDraggingColumn || pressedButton !== Qt.LeftButton)
                  return

                const globalX = mapToItem(null, mouse.x, mouse.y).x
                const relativeX = globalX - columnsContent.mapToItem(null, 0, 0).x
                let running = 0
                let target = 0

                for (let i = 0; i < root.columnIds.length; ++i) {
                  const item = columnRepeater.itemAt(i)
                  const width = item ? item.paneWidth : 0
                  if (relativeX > running + width / 2)
                    target = i
                  running += width + root.columnSpacing
                }

                if (target !== pane.index)
                  movedWhilePressed = true
                root.dropColumnIndex = target
              }

              onReleased: function(mouse) {
                handleArea.cursorShape = Qt.PointingHandCursor

                if (mouse.button === Qt.RightButton) {
                  columnOptionsMenu.targetIndex = pane.index
                  columnOptionsMenu.popup()
                  pressedButton = Qt.NoButton
                  return
                }

                if (root.isDraggingColumn) {
                  const source = root.draggingColumnIndex
                  const target = root.dropColumnIndex
                  root.isDraggingColumn = false
                  root.draggingColumnIndex = -1
                  root.dropColumnIndex = -1

                  if (source >= 0 && target >= 0 && source !== target && root.model)
                    root.model.moveColumnToIndex(root.columnIds[source], target)
                  else if (!movedWhilePressed) {
                    // A click without movement opens the column menu.
                    columnOptionsMenu.targetIndex = pane.index
                    columnOptionsMenu.popup()
                  }
                }

                pressedButton = Qt.NoButton
              }

              onCanceled: {
                handleArea.cursorShape = Qt.PointingHandCursor
                root.isDraggingColumn = false
                root.draggingColumnIndex = -1
                root.dropColumnIndex = -1
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

              x: 0
              width: 20
              z: 8

              function alignedBlock() {
                const list = pane.blockList

                if (!list)
                  return null

                const children = list.contentItem.children

                for (let i = 0; i < children.length; ++i) {
                  const candidate = children[i]
                  if (candidate
                      && String(candidate.blockId) === String(modelData))
                    return candidate
                }

                return null
              }

              height: {
                pane.blockList ? pane.blockList.contentHeight : 0
                const block = childHandle.alignedBlock()
                return block ? block.height : 24
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
                color: handleArea.containsMouse
                       || (root.childDragActive
                           && root.dragChildId === modelData)
                       ? Theme.accent
                       : Theme.tint(Theme.text, Theme.isDark ? 0.3 : 0.25)
                opacity: handleArea.containsMouse
                         || root.hoveredChildBlockId === modelData
                         || (root.childDragActive
                             && root.dragChildId === modelData)
                         ? 1.0 : 0.0

                Behavior on color { ColorAnimation { duration: 120 } }
                Behavior on opacity { NumberAnimation { duration: 120 } }
              }

              MouseArea {
                id: handleArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                preventStealing: true

                Timer {
                  id: holdTimer
                  interval: 200
                  repeat: false
                  onTriggered: {
                    if (!handleArea.pressed)
                      return
                    const point = handleArea.mapToItem(null,
                                                       childHandle.width / 2,
                                                       childHandle.height / 2)
                    if (root.beginChildDrag(modelData,
                                            point.x, point.y)) {
                      handleArea.cursorShape = Qt.ClosedHandCursor
                      childHandle.z = 20
                    }
                  }
                }

                onPressed: holdTimer.restart()

                onPositionChanged: function(mouse) {
                  if (!root.childDragActive)
                    return
                  const global = handleArea.mapToItem(null, mouse.x, mouse.y)
                  root.updateChildDrag(global.x, global.y)
                }

                onReleased: function(mouse) {
                  holdTimer.stop()
                  handleArea.cursorShape = Qt.PointingHandCursor
                  childHandle.z = 8

                  if (root.childDragActive) {
                    root.endChildDrag()
                  } else if (mouse.button === Qt.LeftButton) {
                    childOptionsMenu.blockId = modelData
                    childOptionsMenu.popup()
                  }
                }

                onCanceled: {
                  holdTimer.stop()
                  handleArea.cursorShape = Qt.PointingHandCursor
                  childHandle.z = 8
                  root.cancelChildDrag()
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
              leftMargin: 36
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
            visible: root.childDragActive
                     && root.dropChildColumnIndex === pane.index
                     && root.dropChildRow >= 0
            anchors {
              left: parent.left
              leftMargin: 36
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
              leftMargin: 36
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
        visible: opacity > 0.001
        opacity: root.isBlockHovered || addColumnHover.containsMouse ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 180 } }

        Rectangle {
          anchors.fill: parent
          radius: 5
          color: addColumnHover.containsMouse
                 ? Theme.surfaceHover
                 : "transparent"
          border.color: addColumnHover.containsMouse ? Theme.accent : Theme.border
          border.width: 1

          IconImage {
            anchors.centerIn: parent
            width: 14
            height: 14
            source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_add_20_regular.svg"
            color: addColumnHover.containsMouse ? Theme.accent : Theme.textMuted
          }
        }

        HoverHandler {
          id: addColumnHover
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
