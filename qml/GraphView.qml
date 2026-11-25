import QtQuick
import QtQuick.Controls
import Templnote

// Knowledge graph over [[wiki links]] between local documents.
Item {
  id: root

  property var model: null
  signal documentRequested(string path)
  signal closeRequested()

  anchors.fill: parent
  visible: false
  z: 1700

  readonly property var graph: {
    const data = (root.model && typeof root.model.linkGraph === "function")
                 ? root.model.linkGraph() : []
    return data.length > 0 ? data[0] : {nodes: [], edges: []}
  }
  property var positions: ({})

  function relayout() {
    const nodes = root.graph.nodes || []
    const result = {}
    const cx = width / 2
    const cy = height / 2 + 10
    const radius = Math.max(80, Math.min(width, height) * 0.35)
    for (let i = 0; i < nodes.length; ++i) {
      const angle = (2 * Math.PI * i / Math.max(1, nodes.length)) - Math.PI / 2
      result[nodes[i].path] = {
        x: cx + Math.cos(angle) * radius,
        y: cy + Math.sin(angle) * radius
      }
    }
    positions = result
    canvas.requestPaint()
  }

  function open() {
    visible = true
    relayout()
  }

  function close() {
    visible = false
    closeRequested()
  }

  Rectangle {
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0.86)
  }

  Text {
    anchors { top: parent.top; left: parent.left; margins: 18 }
    text: "Knowledge graph"
    color: Theme.text
    font.pixelSize: 18
    font.bold: true
  }

  Text {
    anchors { top: parent.top; right: closeButton.left; topMargin: 22; rightMargin: 12 }
    text: (root.graph.nodes || []).length + " documents · "
          + (root.graph.edges || []).length + " links"
    color: Theme.textMuted
    font.pixelSize: 12
  }

  Button {
    id: closeButton
    anchors { top: parent.top; right: parent.right; margins: 14 }
    width: 32
    height: 28
    flat: true
    text: "×"
    font.pixelSize: 16
    Accessible.name: "Close graph"
    onClicked: root.close()
  }

  Text {
    anchors.centerIn: parent
    visible: (root.graph.nodes || []).length === 0
    text: "No documents yet. Create a note and link it with [[Another Note]]."
    color: Theme.textMuted
    font.pixelSize: 13
  }

  Canvas {
    id: canvas
    anchors.fill: parent
    onPaint: {
      const ctx = getContext("2d")
      ctx.reset()
      ctx.clearRect(0, 0, width, height)
      ctx.lineWidth = 1.5
      ctx.font = "12px \"" + Qt.application.font.family + "\""
      ctx.textAlign = "center"
      ctx.textBaseline = "middle"

      const edges = root.graph.edges || []
      edges.forEach(function(edge) {
        const a = positions[edge.source]
        const b = positions[edge.target]
        if (!a || !b)
          return
        ctx.strokeStyle = Theme.accent
        ctx.globalAlpha = 0.55
        ctx.beginPath()
        ctx.moveTo(a.x, a.y)
        ctx.lineTo(b.x, b.y)
        ctx.stroke()
        ctx.globalAlpha = 1
      })

      const nodes = root.graph.nodes || []
      nodes.forEach(function(node) {
        const p = positions[node.path]
        if (!p)
          return
        ctx.fillStyle = Theme.surface
        ctx.strokeStyle = Theme.accent
        ctx.beginPath()
        ctx.arc(p.x, p.y, 42, 0, Math.PI * 2)
        ctx.fill()
        ctx.stroke()
        ctx.fillStyle = Theme.text
        let label = node.title
        if (label.length > 18)
          label = label.substring(0, 17) + "…"
        ctx.fillText(label, p.x, p.y)
      })
    }
    onWidthChanged: relayout()
    onHeightChanged: relayout()
  }

  MouseArea {
    anchors.fill: parent
    onClicked: {
      const nodes = root.graph.nodes || []
      for (let i = 0; i < nodes.length; ++i) {
        const p = positions[nodes[i].path]
        if (!p)
          continue
        const dx = mouseX - p.x
        const dy = mouseY - p.y
        if (dx * dx + dy * dy <= 42 * 42) {
          root.documentRequested(nodes[i].path)
          return
        }
      }
    }
  }

  Connections {
    target: root
    function onGraphChanged() { relayout() }
  }
}
