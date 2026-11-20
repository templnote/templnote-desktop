import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Templnote

Item {
  id: root

  property string blockId: ""
  property int blockType: 11
  property string content: ""
  property int headingLevel: 0
  property int numberIndex: 1
  property string alignment: "left"
  property bool isChecked: false
  property var blockMetadata: null
  property bool isBlockHovered: false
  property bool showPlaceholder: false
  property var model: null
  property var searchHighlights: []
  property bool searchNavigationActive: false

  readonly property int cursorPosition: 0
  readonly property string editorText: root.content
  readonly property bool isFocused: hasCellActiveFocus || root.activeFocus
  readonly property real firstLineHeight: 28

  signal enterPressed()
  signal backspacePressed()
  signal undoRequested()
  signal redoRequested()
  signal moveFocusRequested(int direction, real xPosition)
  signal caretMoved(int position, real cursorGlobalY, real cursorHeight)
  signal commandMenuRequested(string query, real xPosition, real yPosition, real cursorHeight)
  signal commandMenuDismissed()
  signal commandMenuAccepted()
  signal commandMenuNavigated(int direction)
  signal selectionChanged(bool hasSelection, real xPosition, real yPosition, real cursorHeight)
  signal focusDelegateRequested(int row, int position)
  signal focusRequested(int position)
  signal searchNavigated(int direction)
  signal searchDismissed()
  signal searchEditStarted()

  implicitWidth: parent ? parent.width : 600
  // Always reserve the add-row strip's space so hovering doesn't resize the block
  implicitHeight: tableWrapper.height + addRowBar.height + 6 + 12

  // Track if any cell has active focus
  property bool hasCellActiveFocus: false
  property int focusedRow: -1
  property int focusedCol: -1
  property int hoveredRowsCount: 0
  readonly property bool anyRowHovered: hoveredRowsCount > 0
  // Mouse x within the block, supplied by the Editor (the block-level hover
  // area sits above nested handlers). 54px offset = 30px gutter + 24px row-handle gutter.
  property real hoverX: -1
  readonly property int hoveredColIndex: {
    if (!isBlockHovered || hoverX < 0)
      return -1
    var x = hoverX - 54 + (tableFlickable ? tableFlickable.contentX : 0)
    var acc = 0
    for (var i = 0; i < columnsData.length; ++i) {
      if (x >= acc && x < acc + columnsData[i].width)
        return i
      acc += columnsData[i].width
    }
    return -1
  }

  // Mouse y within the block, used to light the hovered row's drag pill.
  // Row grid starts 22px down (4px top margin + 18px column header bar).
  property real hoverY: -1
  readonly property int hoveredRowIndex: {
    if (!isBlockHovered || hoverY < 0)
      return -1
    var y = hoverY - 22
    var acc = 0
    for (var i = 0; i < rowsData.length; ++i) {
      var item = rowsRepeater.itemAt(i)
      var h = item ? item.height : 34
      if (y >= acc && y < acc + h)
        return i
      acc += h
    }
    return -1
  }

  // Hover states & interactivity
  property bool menuOpen: cellActionMenu.visible || rowActionMenu.visible || colActionMenu.visible
  readonly property bool isTableHovered: root.isBlockHovered
                                         || (globalTableHoverHandler && globalTableHoverHandler.hovered)
                                         || (globalTableMouseArea && globalTableMouseArea.containsMouse)
                                         || anyRowHovered
                                         || (addColHover && addColHover.hovered)
                                         || (addRowHover && addRowHover.hovered)
                                         || menuOpen
                                         || isResizingCol
                                         || isDraggingRow
                                         || isDraggingCol
  // Resizing state
  property bool isResizingCol: false
  property int resizingColIndex: -1
  property real resizeStartX: 0
  property real resizeStartWidth: 0

  // Drag reordering state
  property bool isDraggingRow: false
  property int draggingRowIndex: -1
  property int dropRowTargetIndex: -1

  property bool isDraggingCol: false
  property int draggingColIndex: -1
  property int dropColTargetIndex: -1

  // Table Data Structures
  property var columnsData: []
  property var rowsData: []
  property bool headerRowEnabled: false

  // Default Column width
  readonly property real minColWidth: 70
  readonly property real defaultColWidth: 160

  // Load from blockMetadata
  function reloadFromMetadata() {
    if (isResizingCol || isDraggingRow || isDraggingCol)
      return

    var meta = root.blockMetadata
    var tbl = (meta && meta.table) ? meta.table : null

    if (tbl && tbl.columns && tbl.rows && tbl.columns.length > 0) {
      headerRowEnabled = tbl.headerRow === true
      var newCols = []
      for (var c = 0; c < tbl.columns.length; ++c) {
        var col = tbl.columns[c]
        newCols.push({
          id: col.id || ("c" + (c + 1)),
          width: Number(col.width) || defaultColWidth
        })
      }
      var newRows = []
      for (var r = 0; r < tbl.rows.length; ++r) {
        var rowObj = tbl.rows[r]
        var cells = rowObj.cells || {}
        var rowCells = {}
        for (var c2 = 0; c2 < newCols.length; ++c2) {
          var cid = newCols[c2].id
          rowCells[cid] = (cells[cid] !== undefined) ? String(cells[cid]) : ""
        }
        newRows.push({
          id: rowObj.id || ("r" + (r + 1)),
          cells: rowCells
        })
      }
      columnsData = newCols
      rowsData = newRows
    } else {
      // Default 3x3 table initialization
      headerRowEnabled = false
      var initialCols = [
        { id: "c1", width: defaultColWidth },
        { id: "c2", width: defaultColWidth },
        { id: "c3", width: defaultColWidth }
      ]
      var initialRows = [
        { id: "r1", cells: { "c1": "", "c2": "", "c3": "" } },
        { id: "r2", cells: { "c1": "", "c2": "", "c3": "" } },
        { id: "r3", cells: { "c1": "", "c2": "", "c3": "" } }
      ]
      columnsData = initialCols
      rowsData = initialRows
      saveTableData()
    }
  }

  onBlockMetadataChanged: reloadFromMetadata()
  Component.onCompleted: reloadFromMetadata()

  // Persist table changes to BlockModel
  function saveTableData() {
    if (!root.model || !root.blockId)
      return

    var colsOut = []
    for (var c = 0; c < columnsData.length; ++c) {
      colsOut.push({
        id: columnsData[c].id,
        width: columnsData[c].width
      })
    }

    var rowsOut = []
    var plainTextParts = []
    for (var r = 0; r < rowsData.length; ++r) {
      var rowCells = {}
      var rowTextParts = []
      for (var c2 = 0; c2 < columnsData.length; ++c2) {
        var cid = columnsData[c2].id
        var text = (rowsData[r].cells && rowsData[r].cells[cid] !== undefined) ? String(rowsData[r].cells[cid]) : ""
        rowCells[cid] = text
        if (text.trim().length > 0)
          rowTextParts.push(text.trim())
      }
      rowsOut.push({
        id: rowsData[r].id,
        cells: rowCells
      })
      if (rowTextParts.length > 0)
        plainTextParts.push(rowTextParts.join(" | "))
    }

    var currentMeta = root.blockMetadata ? Object.assign({}, root.blockMetadata) : {}
    currentMeta.table = {
      columns: colsOut,
      rows: rowsOut,
      headerRow: headerRowEnabled
    }
    currentMeta.plainText = plainTextParts.join("\n")

    root.model.setBlockMetadata(root.blockId, currentMeta)
  }

  // Cell text change handler
  function updateCellText(rowIndex, colId, text) {
    if (rowIndex < 0 || rowIndex >= rowsData.length)
      return
    var r = rowsData[rowIndex]
    if (!r.cells)
      r.cells = {}
    if (r.cells[colId] === text)
      return
    r.cells[colId] = text
    // Debounce save or save immediately
    cellSaveTimer.restart()
  }

  Timer {
    id: cellSaveTimer
    interval: 350
    repeat: false
    onTriggered: root.saveTableData()
  }

  // Row and Column Mutations
  function addColumn(afterIndex) {
    var newId = "c" + Date.now().toString(36) + Math.random().toString(36).substring(2, 5)
    var newCol = { id: newId, width: defaultColWidth }
    var newCols = columnsData.slice()
    if (afterIndex === undefined || afterIndex < 0 || afterIndex >= newCols.length) {
      newCols.push(newCol)
    } else {
      newCols.splice(afterIndex + 1, 0, newCol)
    }

    var newRows = rowsData.slice()
    for (var r = 0; r < newRows.length; ++r) {
      var cells = Object.assign({}, newRows[r].cells)
      cells[newId] = ""
      newRows[r].cells = cells
    }

    columnsData = newCols
    rowsData = newRows
    saveTableData()
  }

  function deleteColumn(colIndex) {
    if (columnsData.length <= 1) {
      deleteTable()
      return
    }
    if (colIndex < 0 || colIndex >= columnsData.length)
      return

    var colId = columnsData[colIndex].id
    var newCols = columnsData.slice()
    newCols.splice(colIndex, 1)

    var newRows = rowsData.slice()
    for (var r = 0; r < newRows.length; ++r) {
      var cells = Object.assign({}, newRows[r].cells)
      delete cells[colId]
      newRows[r].cells = cells
    }

    columnsData = newCols
    rowsData = newRows
    saveTableData()
  }

  function addRow(afterIndex) {
    var newId = "r" + Date.now().toString(36) + Math.random().toString(36).substring(2, 5)
    var cells = {}
    for (var c = 0; c < columnsData.length; ++c) {
      cells[columnsData[c].id] = ""
    }
    var newRow = { id: newId, cells: cells }
    var newRows = rowsData.slice()
    if (afterIndex === undefined || afterIndex < 0 || afterIndex >= newRows.length) {
      newRows.push(newRow)
    } else {
      newRows.splice(afterIndex + 1, 0, newRow)
    }

    rowsData = newRows
    saveTableData()
  }

  function deleteRow(rowIndex) {
    if (rowsData.length <= 1) {
      deleteTable()
      return
    }
    if (rowIndex < 0 || rowIndex >= rowsData.length)
      return

    var newRows = rowsData.slice()
    newRows.splice(rowIndex, 1)
    rowsData = newRows
    saveTableData()
  }

  function deleteTable() {
    if (root.model && root.blockId) {
      root.model.removeBlock(root.blockId)
    }
  }

  function clearCell(rowIndex, colId) {
    if (rowIndex >= 0 && rowIndex < rowsData.length) {
      rowsData[rowIndex].cells[colId] = ""
      // Force trigger row update
      rowsData = rowsData.slice()
      saveTableData()
    }
  }

  function toggleHeaderRow() {
    headerRowEnabled = !headerRowEnabled
    saveTableData()
  }

  // Maps the block's plainText (the string the search index sees) back to
  // individual cell ranges, so search highlights can be painted per cell.
  function tablePlainTextMap() {
    var map = []
    var plain = ""
    for (var r = 0; r < rowsData.length; ++r) {
      var parts = []
      var infos = []
      for (var c = 0; c < columnsData.length; ++c) {
        var cid = columnsData[c].id
        var raw = (rowsData[r].cells && rowsData[r].cells[cid] !== undefined) ? String(rowsData[r].cells[cid]) : ""
        var trimmed = raw.trim()
        if (trimmed.length > 0) {
          parts.push(trimmed)
          infos.push({ row: r, colId: cid, raw: raw, trimmed: trimmed })
        }
      }
      var offset = 0
      for (var p = 0; p < infos.length; ++p) {
        var info = infos[p]
        var rawOffset = info.raw.indexOf(info.trimmed)
        map.push({
          row: info.row,
          colId: info.colId,
          blockStart: plain.length + offset,
          blockEnd: plain.length + offset + info.trimmed.length,
          cellStart: rawOffset < 0 ? 0 : rawOffset
        })
        offset += info.trimmed.length + 3 // " | "
      }
      plain += parts.join(" | ")
      if (r < rowsData.length - 1)
        plain += "\n"
    }
    return map
  }

  function moveRow(fromIndex, toIndex) {
    if (fromIndex === toIndex || fromIndex < 0 || fromIndex >= rowsData.length || toIndex < 0 || toIndex >= rowsData.length)
      return
    var newRows = rowsData.slice()
    var moved = newRows.splice(fromIndex, 1)[0]
    newRows.splice(toIndex, 0, moved)
    rowsData = newRows
    saveTableData()
  }

  function moveColumn(fromIndex, toIndex) {
    if (fromIndex === toIndex || fromIndex < 0 || fromIndex >= columnsData.length || toIndex < 0 || toIndex >= columnsData.length)
      return
    var newCols = columnsData.slice()
    var moved = newCols.splice(fromIndex, 1)[0]
    newCols.splice(toIndex, 0, moved)
    columnsData = newCols
    saveTableData()
  }

  function focusEditor(position) {
    focusCell(0, 0)
  }

  function focusEditorAtX(xPosition) {
    focusCell(0, 0)
  }

  function focusEditorRange(start, end) {
    focusCell(0, 0)
  }

  function focusCell(r, c) {
    if (r < 0 || r >= rowsData.length || c < 0 || c >= columnsData.length)
      return
    focusedRow = r
    focusedCol = c
    var rowItem = rowsRepeater.itemAt(r)
    if (rowItem) {
      var cellItem = rowItem.findCell(c)
      if (cellItem)
        cellItem.editor.forceActiveFocus()
    }
  }

  function refreshFromModel() {
    reloadFromMetadata()
  }

  // ==========================================
  // TABLE CONTAINER & LAYOUT
  // ==========================================
  Item {
    id: tableWrapper
    anchors.left: parent.left
    anchors.top: parent.top
    anchors.topMargin: 4
    anchors.leftMargin: 24 // Leave space for row handles
    // Reserve a gutter on the right for the always-visible add-column button
    property real addColStripSpace: 26
    width: Math.min(root.width - 64, tableContentWidth + addColStripSpace)
    height: tableColumnView.height

    property real tableContentWidth: {
      var total = 0
      for (var i = 0; i < root.columnsData.length; ++i) {
        total += root.columnsData[i].width
      }
      return Math.max(120, total)
    }

    // Comprehensive hover zone spanning the table and its action controls
    Item {
      id: tableHoverContainer
      anchors.left: tableWrapper.left
      anchors.leftMargin: -32
      anchors.top: tableWrapper.top
      anchors.topMargin: -8
      anchors.right: tableWrapper.right
      anchors.rightMargin: 8
      anchors.bottom: addRowBar.bottom
      anchors.bottomMargin: -8
      z: -1

      HoverHandler {
        id: globalTableHoverHandler
      }

      MouseArea {
        id: globalTableMouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        propagateComposedEvents: true
      }
    }

    // Clip horizontal overflow so the table stays beneath the editor width
    Item {
      id: tableClipArea
      anchors.fill: parent
      anchors.leftMargin: -24 // keep the row-handle gutter inside the clip
      clip: true

      // Scrollable container for tables wider than the editor
      Flickable {
        id: tableFlickable
        anchors.fill: parent
        anchors.leftMargin: 24 // gutter for row handles
        contentWidth: tableWrapper.tableContentWidth + tableWrapper.addColStripSpace
        contentHeight: tableColumnView.height
        clip: false
        interactive: !root.menuOpen && !root.isResizingCol && !root.isDraggingRow && !root.isDraggingCol && contentWidth > width
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds

        // Main Table Structure
        Column {
          id: tableColumnView
          width: tableWrapper.tableContentWidth
          spacing: 0

          // Column Top Drag Handles & Header Border
          Item {
            id: colHeaderBar
            width: parent.width
            height: 18
            // Keep the 18px reserved so rows never jump when the handle pills fade in
            opacity: (root.isTableHovered || root.isDraggingCol) ? 1.0 : 0.0
            scale: (root.isTableHovered || root.isDraggingCol) ? 1.0 : 0.94
            transformOrigin: Item.Top
            Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

            Repeater {
              model: root.columnsData.length
              delegate: Item {
                required property int index
                property real colX: {
                  var pos = 0
                  for (var i = 0; i < index; ++i) pos += root.columnsData[i].width
                  return pos
                }
                property real colW: root.columnsData[index].width

                x: colX
                width: colW
                height: 18

                // Column Top Pill Handle (Notion style)
                Rectangle {
                  id: colPill
                  anchors.centerIn: parent
                  width: 32
                  height: 5
                  radius: 2.5
                  color: (colDragArea.containsMouse || root.hoveredColIndex === index
                          || (root.isDraggingCol && root.draggingColIndex === index))
                         ? Theme.accent
                         : Theme.tint(Theme.text, Theme.isDark ? 0.3 : 0.25)
                  // Only the hovered/dragged column's pill is shown, not every column at once
                  opacity: (colDragArea.containsMouse || root.hoveredColIndex === index
                            || (root.isDraggingCol && (root.draggingColIndex === index || root.dropColTargetIndex === index))) ? 1.0 : 0.0

                  Behavior on color { ColorAnimation { duration: 120 } }
                  Behavior on opacity { NumberAnimation { duration: 120 } }
                }

                MouseArea {
                  id: colDragArea
                  anchors.fill: parent
                  hoverEnabled: true
                  cursorShape: Qt.PointingHandCursor
                  acceptedButtons: Qt.LeftButton | Qt.RightButton

                  onPressed: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                      colActionMenu.targetColIndex = index
                      colActionMenu.popup(colDragArea, mouse.x, mouse.y)
                      return
                    }
                    root.isDraggingCol = true
                    root.draggingColIndex = index
                    root.dropColTargetIndex = index
                  }

                  onPositionChanged: function(mouse) {
                    if (root.isDraggingCol && pressed) {
                      var globalX = mapToItem(tableColumnView, mouse.x, mouse.y).x
                      var runningX = 0
                      var target = 0
                      for (var i = 0; i < root.columnsData.length; ++i) {
                        var mid = runningX + root.columnsData[i].width / 2
                        if (globalX > mid) target = i
                        runningX += root.columnsData[i].width
                      }
                      root.dropColTargetIndex = target
                    }
                  }

                  onReleased: function(mouse) {
                    if (root.isDraggingCol) {
                      if (root.draggingColIndex >= 0 && root.dropColTargetIndex >= 0 && root.draggingColIndex !== root.dropColTargetIndex) {
                        root.moveColumn(root.draggingColIndex, root.dropColTargetIndex)
                      }
                      root.isDraggingCol = false
                      root.draggingColIndex = -1
                      root.dropColTargetIndex = -1
                    }
                  }

                  onCanceled: {
                    root.isDraggingCol = false
                    root.draggingColIndex = -1
                    root.dropColTargetIndex = -1
                  }
                }
              }
            }
          }

          // Table Rows
          Repeater {
            id: rowsRepeater
            model: root.rowsData.length

            delegate: Item {
              id: rowDelegate
              required property int index
              readonly property var rowModel: root.rowsData[index]
              readonly property bool isRowHovered: rowHoverHandler.hovered || (rowHandleHover && rowHandleHover.hovered)
                                                || root.hoveredRowIndex === index
                                                || (root.isDraggingRow && root.draggingRowIndex === index)

              HoverHandler {
                id: rowHoverHandler
                onHoveredChanged: {
                  if (hovered)
                    root.hoveredRowsCount++
                  else
                    root.hoveredRowsCount = Math.max(0, root.hoveredRowsCount - 1)
                }
              }

              width: tableWrapper.tableContentWidth
              height: rowContentRow.height

              function findCell(cIndex) {
                return cellsRepeater.itemAt(cIndex)
              }

              // Row drag pill — the vertical counterpart of the column pill
              Item {
                id: rowHandleContainer
                x: -24
                anchors.verticalCenter: parent.verticalCenter
                width: 20
                height: 24
                visible: opacity > 0.001
                opacity: (rowDelegate.isRowHovered || root.isDraggingRow) ? 1.0 : 0.0
                scale: (rowDelegate.isRowHovered || root.isDraggingRow) ? 1.0 : 0.90
                transformOrigin: Item.Center
                Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

                HoverHandler {
                  id: rowHandleHover
                }

                Rectangle {
                  anchors.centerIn: parent
                  width: 5
                  height: 32
                  radius: 2.5
                  color: (rowHandleMouseArea.pressed || rowDelegate.isRowHovered
                          || (root.isDraggingRow && root.draggingRowIndex === index))
                         ? Theme.accent
                         : Theme.tint(Theme.text, Theme.isDark ? 0.3 : 0.25)
                  Behavior on color { ColorAnimation { duration: 120 } }
                }

                MouseArea {
                  id: rowHandleMouseArea
                  anchors.fill: parent
                  cursorShape: Qt.PointingHandCursor
                  acceptedButtons: Qt.LeftButton | Qt.RightButton

                  onPressed: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                      rowActionMenu.targetRowIndex = index
                      rowActionMenu.popup(rowHandleContainer, mouse.x, mouse.y)
                      return
                    }
                    root.isDraggingRow = true
                    root.draggingRowIndex = index
                    root.dropRowTargetIndex = index
                  }

                  onPositionChanged: function(mouse) {
                    if (root.isDraggingRow && pressed) {
                      var globalY = mapToItem(tableColumnView, mouse.x, mouse.y).y
                      var target = Math.max(0, Math.min(root.rowsData.length - 1, Math.floor(globalY / (rowDelegate.height > 0 ? rowDelegate.height : 32))))
                      root.dropRowTargetIndex = target
                    }
                  }

                  onReleased: function(mouse) {
                    if (root.isDraggingRow) {
                      if (root.draggingRowIndex >= 0 && root.dropRowTargetIndex >= 0 && root.draggingRowIndex !== root.dropRowTargetIndex) {
                        root.moveRow(root.draggingRowIndex, root.dropRowTargetIndex)
                      }
                      root.isDraggingRow = false
                      root.draggingRowIndex = -1
                      root.dropRowTargetIndex = -1
                    }
                  }

                  onCanceled: {
                    root.isDraggingRow = false
                    root.draggingRowIndex = -1
                    root.dropRowTargetIndex = -1
                  }
                }
              }

              // Subtle row hover tint instead of a heavy full-row outline
              Rectangle {
                id: rowHighlightFill
                anchors.fill: parent
                anchors.margins: -1
                radius: 2
                color: Theme.surfaceHover
                visible: opacity > 0.001
                opacity: (rowDelegate.isRowHovered && !root.isDraggingRow) ? 0.55 : 0.0
                Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
              }

              // Row insertion indicator line during drag
              Rectangle {
                visible: root.isDraggingRow && root.dropRowTargetIndex === index
                width: parent.width
                height: 3
                radius: 1.5
                color: Theme.accent
                y: -1
                z: 20
              }

              // Row content containing cells
              Row {
                id: rowContentRow
                width: parent.width

                Repeater {
                  id: cellsRepeater
                  model: root.columnsData.length

                  delegate: Rectangle {
                    id: cellRoot
                    required property int index
                    readonly property var colMeta: root.columnsData[index]
                    readonly property string colId: colMeta ? colMeta.id : ""
                    readonly property real targetWidth: colMeta ? colMeta.width : root.defaultColWidth
                    readonly property bool isHeaderCell: root.headerRowEnabled && rowDelegate.index === 0

                    property alias editor: cellTextEdit

                    // Re-read this cell's value after its column id or row data changes.
                    function refreshCellText() {
                      if (cellTextEdit.activeFocus || !rowModel || !rowModel.cells || !colId)
                        return
                      var newVal = rowModel.cells[colId] || ""
                      if (cellTextEdit.text !== newVal)
                        cellTextEdit.text = newVal
                    }

                    function highlightRanges() {
                      var ranges = []
                      var map = root.tablePlainTextMap()
                      for (var i = 0; i < map.length; ++i) {
                        var entry = map[i]
                        if (entry.row !== rowDelegate.index || entry.colId !== cellRoot.colId)
                          continue
                        for (var h = 0; h < root.searchHighlights.length; ++h) {
                          var hl = root.searchHighlights[h]
                          var start = Math.max(hl.start, entry.blockStart)
                          var end = Math.min(hl.start + hl.length, entry.blockEnd)
                          if (end > start) {
                            ranges.push({
                              start: entry.cellStart + (start - entry.blockStart),
                              end: entry.cellStart + (end - entry.blockStart),
                              active: hl.active === true
                            })
                          }
                        }
                      }
                      return ranges
                    }

                    width: targetWidth
                    height: Math.max(34, cellTextEdit.contentHeight + 14)

                    // Smooth sliding animation for column resizing
                    Behavior on width {
                      enabled: !root.isResizingCol
                      NumberAnimation {
                        duration: 220
                        easing.type: Easing.OutCubic
                      }
                    }

                    color: isHeaderCell
                          ? Theme.tint(Theme.text, Theme.isDark ? 0.09 : 0.07)
                          : Theme.tint(Theme.text, Theme.isDark ? 0.04 : 0.03)

                    // Single, non-doubled grid lines: right/bottom on every cell,
                    // top on the first row and left on the first column only.
                    Rectangle {
                      anchors.right: parent.right
                      anchors.top: parent.top
                      anchors.bottom: parent.bottom
                      width: 1
                      color: Theme.border
                    }
                    Rectangle {
                      anchors.left: parent.left
                      anchors.right: parent.right
                      anchors.bottom: parent.bottom
                      height: 1
                      color: isHeaderCell ? Theme.tint(Theme.accent, 0.35) : Theme.border
                    }
                    Rectangle {
                      visible: rowDelegate.index === 0
                      anchors.left: parent.left
                      anchors.right: parent.right
                      anchors.top: parent.top
                      height: 1
                      color: Theme.border
                    }
                    Rectangle {
                      visible: cellRoot.index === 0
                      anchors.left: parent.left
                      anchors.top: parent.top
                      anchors.bottom: parent.bottom
                      width: 1
                      color: Theme.border
                    }

                    // Active-cell focus ring — the caret alone is easy to lose in wide tables
                    Rectangle {
                      z: 2
                      anchors.fill: parent
                      radius: 2
                      color: Theme.tint(Theme.accent, 0.06)
                      border.color: Theme.accent
                      border.width: 1.5
                      visible: opacity > 0.001
                      opacity: (root.hasCellActiveFocus && root.focusedRow === rowDelegate.index && root.focusedCol === cellRoot.index) ? 1.0 : 0.0
                      Behavior on opacity { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                    }

                    // Search match highlights, painted behind the text like TextBlock does
                    Canvas {
                      id: highlightCanvas
                      anchors.fill: parent
                      z: 1
                      visible: root.searchHighlights.length > 0
                      onWidthChanged: requestPaint()
                      onHeightChanged: requestPaint()
                      onVisibleChanged: if (visible) requestPaint()

                      Connections {
                        target: root
                        function onSearchHighlightsChanged() { if (highlightCanvas.visible) highlightCanvas.requestPaint() }
                        function onRowsDataChanged() { if (highlightCanvas.visible) highlightCanvas.requestPaint() }
                      }
                      Connections {
                        target: cellTextEdit
                        function onTextChanged() { if (highlightCanvas.visible) highlightCanvas.requestPaint() }
                      }

                      onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        var ranges = cellRoot.highlightRanges()
                        for (var i = 0; i < ranges.length; ++i) {
                          var range = ranges[i]
                          ctx.fillStyle = range.active ? Theme.tint(Theme.accent, 0.40)
                                                       : Theme.tint(Theme.accent, 0.16)
                          var from = Math.max(0, Math.min(range.start, cellTextEdit.length))
                          var to = Math.max(from, Math.min(range.end, cellTextEdit.length))
                          for (var p = from; p < to; ++p) {
                            var rect = cellTextEdit.positionToRectangle(p)
                            ctx.fillRect(rect.x + cellTextEdit.x, rect.y + cellTextEdit.y,
                                         Math.max(1, rect.width), Math.max(2, rect.height))
                          }
                        }
                      }
                    }

                    // Cell text input
                    TextEdit {
                      id: cellTextEdit
                      anchors.fill: parent
                      anchors.leftMargin: 10
                      // Constant margin reserves room for the action button so text never reflows on hover
                      anchors.rightMargin: 24
                      anchors.topMargin: 7
                      anchors.bottomMargin: 7
                      wrapMode: TextEdit.Wrap
                      font.pixelSize: 14
                      font.bold: isHeaderCell
                      color: Theme.text
                      selectionColor: Theme.accent
                      selectedTextColor: Theme.accentText
                      selectByMouse: true

                      function isOnFirstLine() {
                        return text.substring(0, cursorPosition).indexOf("\n") < 0
                      }

                      function isOnLastLine() {
                        return text.substring(cursorPosition).indexOf("\n") < 0
                      }

                      function updateCaretGeometry() {
                        if (!activeFocus)
                          return
                        var cRect = cursorRectangle
                        var mapped = mapToItem(null, cRect.x, cRect.y)
                        root.caretMoved(cursorPosition, mapped ? mapped.y : 0, Math.max(14, cRect.height))
                      }

                      function notifySelection() {
                        if (selectionStart === selectionEnd) {
                          root.selectionChanged(false, 0, 0, 0)
                          return
                        }
                        if (!activeFocus)
                          return
                        var cRect = cursorRectangle
                        var mapped = mapToItem(root, cRect.x + cRect.width / 2, cRect.y)
                        root.selectionChanged(true, mapped ? mapped.x : 0, mapped ? mapped.y : 0,
                                              Math.max(14, cRect.height))
                      }

                      onCursorPositionChanged: {
                        updateCaretGeometry()
                        notifySelection()
                      }

                      onSelectionStartChanged: notifySelection()
                      onSelectionEndChanged: notifySelection()

                      Component.onCompleted: {
                        if (rowModel && rowModel.cells && colId) {
                          text = rowModel.cells[colId] || ""
                        }
                      }

                      Connections {
                        target: root
                        function onRowsDataChanged() { cellRoot.refreshCellText() }
                        // Defer so colId has been re-evaluated for the new column order
                        function onColumnsDataChanged() {
                          Qt.callLater(function() { cellRoot.refreshCellText() })
                        }
                      }

                      onTextChanged: {
                        if (activeFocus && rowModel && colId) {
                          root.updateCellText(rowDelegate.index, colId, text)
                        }
                      }

                      onActiveFocusChanged: {
                        if (activeFocus) {
                          root.hasCellActiveFocus = true
                          root.focusedRow = rowDelegate.index
                          root.focusedCol = cellRoot.index
                          updateCaretGeometry()
                          notifySelection()
                        } else {
                          if (root.focusedRow === rowDelegate.index && root.focusedCol === cellRoot.index) {
                            root.hasCellActiveFocus = false
                            root.focusedRow = -1
                            root.focusedCol = -1
                          }
                          root.selectionChanged(false, 0, 0, 0)
                        }
                      }

                      Keys.onTabPressed: function(event) {
                        event.accepted = true
                        if (cellRoot.index + 1 < root.columnsData.length) {
                          root.focusCell(rowDelegate.index, cellRoot.index + 1)
                        } else if (rowDelegate.index + 1 < root.rowsData.length) {
                          root.focusCell(rowDelegate.index + 1, 0)
                        } else {
                          // At the last cell of the table: automatically append a new row!
                          root.addRow(rowDelegate.index)
                          Qt.callLater(function() {
                            root.focusCell(rowDelegate.index + 1, 0)
                          })
                        }
                      }

                      Keys.onBacktabPressed: function(event) {
                        event.accepted = true
                        if (cellRoot.index > 0) {
                          root.focusCell(rowDelegate.index, cellRoot.index - 1)
                        } else if (rowDelegate.index > 0) {
                          root.focusCell(rowDelegate.index - 1, root.columnsData.length - 1)
                        }
                      }

                      Keys.onEscapePressed: function(event) {
                        event.accepted = true
                        cellTextEdit.focus = false
                        root.forceActiveFocus()
                      }

                      Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Up && isOnFirstLine()) {
                          event.accepted = true
                          if (rowDelegate.index > 0) {
                            root.focusCell(rowDelegate.index - 1, cellRoot.index)
                          } else {
                            root.moveFocusRequested(-1, mapToItem(root, cursorRectangle.x, 0).x)
                          }
                          return
                        }

                        if (event.key === Qt.Key_Down && isOnLastLine()) {
                          event.accepted = true
                          if (rowDelegate.index + 1 < root.rowsData.length) {
                            root.focusCell(rowDelegate.index + 1, cellRoot.index)
                          } else {
                            root.enterPressed()
                          }
                          return
                        }

                        if (event.key === Qt.Key_Enter || event.key === Qt.Key_Return) {
                          event.accepted = true
                          if (event.modifiers & Qt.ShiftModifier) {
                            insert(cursorPosition, "\n")
                          } else if (rowDelegate.index + 1 < root.rowsData.length) {
                            root.focusCell(rowDelegate.index + 1, cellRoot.index)
                          } else {
                            root.addRow(rowDelegate.index)
                            Qt.callLater(function() {
                              root.focusCell(rowDelegate.index + 1, cellRoot.index)
                            })
                          }
                          return
                        }

                        if (event.key === Qt.Key_Backspace && cellTextEdit.text.length === 0 && root.rowsData.length === 1 && root.columnsData.length === 1) {
                          event.accepted = true
                          root.deleteTable()
                        }
                      }
                    }

                    HoverHandler {
                      id: cellHoverHandler
                    }

                    // Cell Context Menu (Right Click)
                    MouseArea {
                      id: cellHoverArea
                      anchors.fill: parent
                      acceptedButtons: Qt.RightButton
                      onClicked: function(mouse) {
                        if (mouse.button === Qt.RightButton) {
                          cellActionMenu.targetRow = rowDelegate.index
                          cellActionMenu.targetCol = cellRoot.index
                          cellActionMenu.targetColId = cellRoot.colId
                          cellActionMenu.popup(cellRoot, mouse.x, mouse.y)
                        }
                      }
                    }

                    // Each cell with its own action button (3-dots hover action button)
                    Item {
                      id: cellActionButton
                      anchors.right: parent.right
                      anchors.top: parent.top
                      anchors.margins: 4
                      width: 18
                      height: 18
                      visible: opacity > 0.001
                      opacity: (cellHoverHandler.hovered || cellActionBtnMouse.containsMouse
                                || (root.hasCellActiveFocus && root.focusedRow === rowDelegate.index && root.focusedCol === cellRoot.index)) ? 1.0 : 0.0
                      scale: (cellHoverHandler.hovered || cellActionBtnMouse.containsMouse) ? 1.0 : 0.9
                      transformOrigin: Item.TopRight
                      Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                      Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

                      Rectangle {
                        anchors.fill: parent
                        radius: 3
                        color: cellActionBtnMouse.pressed ? Theme.accent : (cellActionBtnMouse.containsMouse ? Theme.surfaceHover : "transparent")

                        Row {
                          anchors.centerIn: parent
                          spacing: 2
                          Repeater {
                            model: 3
                            Rectangle {
                              width: 2.5
                              height: 2.5
                              radius: 1.25
                              color: Theme.textMuted
                            }
                          }
                        }
                      }

                      MouseArea {
                        id: cellActionBtnMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: function(mouse) {
                          cellActionMenu.targetRow = rowDelegate.index
                          cellActionMenu.targetCol = cellRoot.index
                          cellActionMenu.targetColId = cellRoot.colId
                          cellActionMenu.popup(cellActionButton, 0, cellActionButton.height + 4)
                        }
                      }
                    }

                    // Column Resizer Divider (Vertical blue highlight line on hover/drag, matching Screenshot 1!)
                    Item {
                      id: colDividerResizer
                      anchors.right: parent.right
                      anchors.rightMargin: -5
                      anchors.top: parent.top
                      anchors.bottom: parent.bottom
                      width: 10
                      z: 15

                      HoverHandler {
                        id: dividerHover
                      }

                      // Blue vertical highlight indicator line (Screenshot 1)
                      Rectangle {
                        anchors.centerIn: parent
                        width: 2
                        height: parent.height
                        color: Theme.accent
                        visible: opacity > 0.001
                        opacity: (dividerHover.hovered || (root.isResizingCol && root.resizingColIndex === cellRoot.index)) ? 1.0 : 0.0
                        scale: (dividerHover.hovered || (root.isResizingCol && root.resizingColIndex === cellRoot.index)) ? 1.0 : 0.85
                        transformOrigin: Item.Center
                        Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                        Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                      }

                      MouseArea {
                        id: dividerMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.SplitHCursor

                        onPressed: function(mouse) {
                          root.isResizingCol = true
                          root.resizingColIndex = cellRoot.index
                          var g = mapToItem(null, mouse.x, mouse.y)
                          root.resizeStartX = g.x
                          root.resizeStartWidth = cellRoot.targetWidth
                        }

                        onPositionChanged: function(mouse) {
                          if (pressed && root.isResizingCol) {
                            var g = mapToItem(null, mouse.x, mouse.y)
                            var delta = g.x - root.resizeStartX
                            var newW = Math.max(root.minColWidth, root.resizeStartWidth + delta)
                            var newCols = root.columnsData.slice()
                            newCols[cellRoot.index] = {
                              id: newCols[cellRoot.index].id,
                              width: newW
                            }
                            root.columnsData = newCols
                          }
                        }

                        onReleased: {
                          if (root.isResizingCol) {
                            root.isResizingCol = false
                            root.resizingColIndex = -1
                            root.saveTableData()
                          }
                        }

                        onCanceled: {
                          if (root.isResizingCol) {
                            root.isResizingCol = false
                            root.resizingColIndex = -1
                            root.saveTableData()
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
          // Add-column button at the end of the table content
        Item {
          id: addColStrip
          x: tableWrapper.tableContentWidth + 4
          y: colHeaderBar.height
          height: tableColumnView.height - colHeaderBar.height
          width: 22
          visible: opacity > 0.001
          opacity: root.isTableHovered ? 1.0 : 0.0
          scale: root.isTableHovered ? 1.0 : 0.92
          transformOrigin: Item.Left
          Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
          Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

          HoverHandler {
            id: addColHover
          }

          Rectangle {
            anchors.fill: parent
            radius: 4
            color: addColHover.hovered ? Theme.surfaceHover : Theme.surfaceAlt
            border.color: addColHover.hovered ? Theme.accent : Theme.border
            border.width: 1

            IconImage {
              anchors.centerIn: parent
              width: 14
              height: 14
              source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_add_20_regular.svg"
              color: addColHover.hovered ? Theme.accent : Theme.textMuted
            }
          }

          MouseArea {
            id: addColMouse
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.addColumn()
          }
        }
      }

      // Shift + wheel scrolls the table horizontally (normal wheel still scrolls the editor)
      WheelHandler {
        acceptedModifiers: Qt.ShiftModifier
        blocking: true
        onWheel: function(event) {
          var maxX = Math.max(0, tableFlickable.contentWidth - tableFlickable.width)
          if (maxX <= 0)
            return
          // Prefer the notch delta: some platforms report a tiny pixelDelta
          // alongside it, which would otherwise scroll imperceptibly.
          // Qt reports a wheel rolled down as a NEGATIVE y delta, so negate it
          // to make Shift + wheel-down reveal the columns to the right.
          var dx = event.angleDelta.x
          if (dx === 0)
            dx = -event.angleDelta.y
          if (dx === 0)
            dx = event.pixelDelta.x
          if (dx === 0)
            dx = -event.pixelDelta.y
          tableFlickable.contentX = Math.max(0, Math.min(tableFlickable.contentX + dx, maxX))
          event.accepted = true
        }
      }

    }

    // Live width readout while dragging a column divider
    Rectangle {
      id: resizeWidthTooltip
      z: 40
      visible: root.isResizingCol && root.resizingColIndex >= 0 && root.resizingColIndex < root.columnsData.length
      width: resizeWidthLabel.implicitWidth + 14
      height: 22
      radius: 4
      color: Theme.surfaceAlt
      border.color: Theme.border
      border.width: 1

      x: {
        var end = 0
        for (var i = 0; i <= root.resizingColIndex && i < root.columnsData.length; ++i)
          end += root.columnsData[i].width
        return end - tableFlickable.contentX - width / 2
      }
      y: colHeaderBar.height + 6

      Text {
        id: resizeWidthLabel
        anchors.centerIn: parent
        text: root.resizingColIndex >= 0 && root.resizingColIndex < root.columnsData.length
              ? Math.round(root.columnsData[root.resizingColIndex].width) + " px" : ""
        color: Theme.text
        font.pixelSize: 11
      }
    }

    // Column insertion vertical indicator during drag
    Rectangle {
      visible: root.isDraggingCol && root.dropColTargetIndex >= 0
      width: 3
      height: tableColumnView.height
      radius: 1.5
      color: Theme.accent
      x: {
        var pos = 0
        for (var i = 0; i <= root.dropColTargetIndex && i < root.columnsData.length; ++i) {
          pos += root.columnsData[i].width
        }
        return pos - 1 - tableFlickable.contentX
      }
      z: 30
    }

  }

  // Add Row Button (+) Strip at the bottom of the table (Matching Screenshot 2!)
  Item {
    id: addRowBar
    anchors.top: tableWrapper.bottom
    anchors.topMargin: 4
    anchors.left: tableWrapper.left
    width: tableWrapper.width
    height: 24
    visible: opacity > 0.001
    opacity: root.isTableHovered ? 1.0 : 0.0
    scale: root.isTableHovered ? 1.0 : 0.94
    transformOrigin: Item.Top
    Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

    HoverHandler {
      id: addRowHover
    }

    Rectangle {
      anchors.fill: parent
      radius: 4
      color: addRowHover.hovered ? Theme.surfaceHover : Theme.surfaceAlt
      border.color: addRowHover.hovered ? Theme.accent : Theme.border
      border.width: 1

      Row {
        anchors.centerIn: parent
        spacing: 6

        IconImage {
          width: 14
          height: 14
          source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_add_20_regular.svg"
          color: addRowHover.hovered ? Theme.accent : Theme.textMuted
          anchors.verticalCenter: parent.verticalCenter
        }

        Text {
          text: "New row"
          font.pixelSize: 12
          color: addRowHover.hovered ? Theme.accent : Theme.textMuted
          anchors.verticalCenter: parent.verticalCenter
        }
      }
    }

    MouseArea {
      id: addRowMouse
      anchors.fill: parent
      cursorShape: Qt.PointingHandCursor
      onClicked: root.addRow()
    }
  }

  // ==========================================
  // CONTEXT MENUS (Cell, Row, Column)
  // ==========================================
  Menu {
    id: cellActionMenu
    property int targetRow: -1
    property int targetCol: -1
    property string targetColId: ""

    MenuItem {
      text: "Insert row above"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_add_20_regular.svg"
      onTriggered: {
        if (cellActionMenu.targetRow >= 0)
          root.addRow(cellActionMenu.targetRow - 1)
      }
    }

    MenuItem {
      text: "Insert row below"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_add_20_regular.svg"
      onTriggered: {
        if (cellActionMenu.targetRow >= 0)
          root.addRow(cellActionMenu.targetRow)
      }
    }

    MenuSeparator {}

    MenuItem {
      text: "Insert column left"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_add_20_regular.svg"
      onTriggered: {
        if (cellActionMenu.targetCol >= 0)
          root.addColumn(cellActionMenu.targetCol - 1)
      }
    }

    MenuItem {
      text: "Insert column right"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_add_20_regular.svg"
      onTriggered: {
        if (cellActionMenu.targetCol >= 0)
          root.addColumn(cellActionMenu.targetCol)
      }
    }

    MenuItem {
      text: "Header row"
      checkable: true
      checked: root.headerRowEnabled
      onTriggered: root.toggleHeaderRow()
    }

    MenuSeparator {}

    MenuItem {
      text: "Clear cell"
      onTriggered: {
        if (cellActionMenu.targetRow >= 0 && cellActionMenu.targetColId.length > 0)
          root.clearCell(cellActionMenu.targetRow, cellActionMenu.targetColId)
      }
    }

    MenuItem {
      text: root.rowsData.length === 1 ? "Delete row (delete table)" : "Delete row"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_delete_20_regular.svg"
      onTriggered: {
        if (cellActionMenu.targetRow >= 0)
          root.deleteRow(cellActionMenu.targetRow)
      }
    }

    MenuItem {
      text: root.columnsData.length === 1 ? "Delete column (delete table)" : "Delete column"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_delete_20_regular.svg"
      onTriggered: {
        if (cellActionMenu.targetCol >= 0)
          root.deleteColumn(cellActionMenu.targetCol)
      }
    }

    MenuSeparator {}

    MenuItem {
      text: "Delete table"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_delete_20_regular.svg"
      onTriggered: root.deleteTable()
    }
  }

  Menu {
    id: rowActionMenu
    property int targetRowIndex: -1

    MenuItem {
      text: "Insert row above"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_add_20_regular.svg"
      onTriggered: {
        if (rowActionMenu.targetRowIndex >= 0)
          root.addRow(rowActionMenu.targetRowIndex - 1)
      }
    }

    MenuItem {
      text: "Insert row below"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_add_20_regular.svg"
      onTriggered: {
        if (rowActionMenu.targetRowIndex >= 0)
          root.addRow(rowActionMenu.targetRowIndex)
      }
    }

    MenuItem {
      text: root.rowsData.length === 1 ? "Delete row (delete table)" : "Delete row"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_delete_20_regular.svg"
      onTriggered: {
        if (rowActionMenu.targetRowIndex >= 0)
          root.deleteRow(rowActionMenu.targetRowIndex)
      }
    }

    MenuItem {
      text: "Header row"
      checkable: true
      checked: root.headerRowEnabled
      onTriggered: root.toggleHeaderRow()
    }

    MenuSeparator {}

    MenuItem {
      text: "Delete table"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_delete_20_regular.svg"
      onTriggered: root.deleteTable()
    }
  }

  Menu {
    id: colActionMenu
    property int targetColIndex: -1

    MenuItem {
      text: "Insert column left"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_add_20_regular.svg"
      onTriggered: {
        if (colActionMenu.targetColIndex >= 0)
          root.addColumn(colActionMenu.targetColIndex - 1)
      }
    }

    MenuItem {
      text: "Insert column right"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_add_20_regular.svg"
      onTriggered: {
        if (colActionMenu.targetColIndex >= 0)
          root.addColumn(colActionMenu.targetColIndex)
      }
    }

    MenuItem {
      text: root.columnsData.length === 1 ? "Delete column (delete table)" : "Delete column"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_delete_20_regular.svg"
      onTriggered: {
        if (colActionMenu.targetColIndex >= 0)
          root.deleteColumn(colActionMenu.targetColIndex)
      }
    }

    MenuItem {
      text: "Header row"
      checkable: true
      checked: root.headerRowEnabled
      onTriggered: root.toggleHeaderRow()
    }

    MenuSeparator {}

    MenuItem {
      text: "Delete table"
      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_delete_20_regular.svg"
      onTriggered: root.deleteTable()
    }
  }
}
