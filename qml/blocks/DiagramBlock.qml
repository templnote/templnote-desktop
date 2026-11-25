import QtQuick
import QtQuick.Controls
import Templnote

// Native renderer for a small Mermaid-like flowchart subset:
//   graph TD | graph LR
//   A[Label] --> B[Label]
//   C{Decision} -- yes --> D(Done)
Item {
  id: root

  property string blockId: ""
  property int blockType: 15
  property string content: ""
  property int headingLevel: 0
  property int numberIndex: 1
  property string alignment: "left"
  property bool isChecked: false
  property bool showPlaceholder: false
  property var blockMetadata: null
  property bool isBlockHovered: false
  property var model: null
  property var searchHighlights: []
  property bool searchNavigationActive: false

  readonly property string editorText: root.content
  readonly property bool isFocused: sourceArea.activeFocus || root.activeFocus
  readonly property real firstLineHeight: 32

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
  signal focusRequested(int position)
  signal searchNavigated(int direction)
  signal searchDismissed()
  signal searchEditStarted()

  implicitWidth: parent ? parent.width : 600
  implicitHeight: editing ? sourceFlickable.height + 34 : Math.max(120, layout.height + 28)

  property bool editing: false

  function focusEditor(position) {
    editing = true
    sourceArea.text = root.content
    sourceArea.cursorPosition = position >= 0 ? Math.min(position, sourceArea.length) : sourceArea.length
    sourceArea.forceActiveFocus()
  }

  function refreshFromModel() {
    if (!editing)
      sourceArea.text = root.content
  }

  function commit() {
    const value = sourceArea.text
    if (value !== root.content && root.model)
      root.model.updateBlockText(blockId, value)
    editing = false
  }

  function nodeFromToken(id, square, round, curly) {
    return {
      id: id,
      label: square || round || curly || id,
      shape: curly ? "diamond" : (round ? "round" : "rect")
    }
  }

  function buildLayout(source) {
    const lines = String(source || "").split("\n").map(function(l) { return l.trim() })
                   .filter(function(l) { return l.length > 0 && !/^graph\b/i.test(l) })
    const horizontal = /graph\s+LR/i.test(String(source || ""))
    const nodes = {}
    const edges = []

    const re = /^([A-Za-z0-9_]+)(?:\[([^\]]*)\]|\(([^)]*)\)|\{([^}]*)\})?\s*(?:--\s*([^-]*?)\s*)?-->\s*([A-Za-z0-9_]+)(?:\[([^\]]*)\]|\(([^)]*)\)|\{([^}]*)\})?/
    lines.forEach(function(line) {
      const m = re.exec(line.replace(/;/g, ""))
      if (!m)
        return
      const a = nodeFromToken(m[1], m[2], m[3], m[4])
      const b = nodeFromToken(m[6], m[7], m[8], m[9])
      nodes[a.id] = a
      nodes[b.id] = b
      edges.push({ from: a.id, to: b.id, label: (m[5] || "").trim() })
    })

    const ids = Object.keys(nodes)
    if (ids.length === 0) {
      nodes["A"] = nodeFromToken("A", "No diagram yet", "", "")
      ids.push("A")
    }

    const layerOf = {}
    const indegree = {}
    ids.forEach(function(id) { indegree[id] = 0 })
    edges.forEach(function(e) { indegree[e.to]++ })
    const queue = ids.filter(function(id) { return indegree[id] === 0 })
    queue.forEach(function(id) { layerOf[id] = 0 })
    while (queue.length > 0) {
      const id = queue.shift()
      edges.filter(function(e) { return e.from === id }).forEach(function(e) {
        layerOf[e.to] = Math.max(layerOf[e.to] === undefined ? 0 : layerOf[e.to],
                                 (layerOf[id] || 0) + 1)
        indegree[e.to]--
        if (indegree[e.to] === 0)
          queue.push(e.to)
      })
    }

    const groups = {}
    ids.forEach(function(id) {
      const layer = layerOf[id] === undefined ? 0 : layerOf[id]
      if (!groups[layer])
        groups[layer] = []
      groups[layer].push(id)
    })

    const nodeW = 120
    const nodeH = 46
    const gapX = 60
    const gapY = 40
    const maxPerLayer = Math.max.apply(null, Object.keys(groups).map(function(k) { return groups[k].length }))
    const mainSpan = maxPerLayer * nodeW + (maxPerLayer - 1) * gapX
    const layerCount = Object.keys(groups).length
    const crossSpan = layerCount * nodeW + (layerCount - 1) * gapX
    const width = Math.max(200, horizontal ? crossSpan : mainSpan)
    const height = Math.max(120, horizontal ? mainSpan : crossSpan) + gapY

    const box = width / 2 - mainSpan / 2
    Object.keys(groups).forEach(function(key) {
      const layer = Number(key)
      const row = groups[key]
      row.forEach(function(id, index) {
        const x = horizontal ? 20 + layer * (nodeW + gapX) : box + index * (nodeW + gapX)
        const y = horizontal ? 20 + index * (nodeH + gapY) : 20 + layer * (nodeH + gapY)
        nodes[id].x = x
        nodes[id].y = y
        nodes[id].w = nodeW
        nodes[id].h = nodeH
      })
    })

    return { nodes: nodes, edges: edges, width: width, height: height }
  }

  readonly property var layout: buildLayout(root.content)

  Rectangle {
    anchors.fill: parent
    radius: 8
    color: Theme.surfaceAlt
    border.width: 1
    border.color: Theme.border
  }

  Row {
    anchors { top: parent.top; left: parent.left; margins: 8 }
    spacing: 8
    Text {
      anchors.verticalCenter: parent.verticalCenter
      text: "DIAGRAM"
      color: Theme.textFaint
      font.pixelSize: 10
      font.bold: true
      font.letterSpacing: 1.2
    }
    Button {
      width: 24
      height: 20
      padding: 0
      flat: true
      text: editing ? "✓" : "✎"
      font.pixelSize: 12
      Accessible.name: editing ? "Apply diagram" : "Edit diagram"
      onClicked: editing ? root.commit() : root.focusEditor(-1)
    }
  }

  Canvas {
    id: canvas
    anchors {
      top: parent.top
      left: parent.left
      right: parent.right
      topMargin: 34
    }
    width: parent.width
    height: root.layout.height
    visible: !editing
    onPaint: {
      const ctx = getContext("2d")
      ctx.reset()
      ctx.clearRect(0, 0, width, height)
      ctx.lineWidth = 1.5
      ctx.font = "11px \"" + Qt.application.font.family + "\""
      ctx.textAlign = "center"
      ctx.textBaseline = "middle"

      root.layout.edges.forEach(function(edge) {
        const a = root.layout.nodes[edge.from]
        const b = root.layout.nodes[edge.to]
        if (!a || !b)
          return
        const ax = a.x + a.w / 2
        const ay = a.y + a.h / 2
        const bx = b.x + b.w / 2
        const by = b.y + b.h / 2
        ctx.strokeStyle = Theme.textMuted
        ctx.beginPath()
        ctx.moveTo(ax, ay)
        ctx.lineTo(bx, by)
        ctx.stroke()
        const angle = Math.atan2(by - ay, bx - ax)
        const tipX = bx - Math.cos(angle) * (b.w / 2 + 4)
        const tipY = by - Math.sin(angle) * (b.h / 2 + 4)
        ctx.fillStyle = Theme.textMuted
        ctx.beginPath()
        ctx.moveTo(tipX, tipY)
        ctx.lineTo(tipX - Math.cos(angle - 0.5) * 10, tipY - Math.sin(angle - 0.5) * 10)
        ctx.lineTo(tipX - Math.cos(angle + 0.5) * 10, tipY - Math.sin(angle + 0.5) * 10)
        ctx.closePath()
        ctx.fill()
        if (edge.label) {
          ctx.fillStyle = Theme.textFaint
          ctx.fillText(edge.label, (ax + bx) / 2, (ay + by) / 2 - 8)
        }
      })

      Object.keys(root.layout.nodes).forEach(function(id) {
        const node = root.layout.nodes[id]
        ctx.fillStyle = Theme.surface
        ctx.strokeStyle = Theme.accent
        ctx.beginPath()
        if (node.shape === "diamond") {
          ctx.moveTo(node.x + node.w / 2, node.y)
          ctx.lineTo(node.x + node.w, node.y + node.h / 2)
          ctx.lineTo(node.x + node.w / 2, node.y + node.h)
          ctx.lineTo(node.x, node.y + node.h / 2)
          ctx.closePath()
        } else {
          const r = node.shape === "round" ? node.h / 2 : 8
          ctx.moveTo(node.x + r, node.y)
          ctx.arcTo(node.x + node.w, node.y, node.x + node.w, node.y + node.h, r)
          ctx.arcTo(node.x + node.w, node.y + node.h, node.x, node.y + node.h, r)
          ctx.arcTo(node.x, node.y + node.h, node.x, node.y, r)
          ctx.arcTo(node.x, node.y, node.x + node.w, node.y, r)
          ctx.closePath()
        }
        ctx.fill()
        ctx.stroke()
        ctx.fillStyle = Theme.text
        ctx.fillText(node.label, node.x + node.w / 2, node.y + node.h / 2)
      })
    }
    onWidthChanged: requestPaint()
    Component.onCompleted: requestPaint()
  }

  Connections {
    target: root
    function onContentChanged() { canvas.requestPaint() }
  }

  Flickable {
    id: sourceFlickable
    anchors {
      top: parent.top
      left: parent.left
      right: parent.right
      bottom: parent.bottom
      margins: 12
      topMargin: 34
    }
    visible: editing
    contentHeight: Math.max(60, sourceArea.implicitHeight)
    boundsBehavior: Flickable.StopAtBounds
    TextArea {
      id: sourceArea
      width: parent.width
      font.pixelSize: 13
      font.family: "monospace"
      color: Theme.text
      wrapMode: TextEdit.Wrap
      selectByMouse: true
      onEditingFinished: root.commit()
      Keys.onEscapePressed: editing = false
    }
  }
}
