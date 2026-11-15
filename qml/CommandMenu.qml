import QtQuick
import QtQuick.Controls

Rectangle {
  id: root
  property string query: ""
  property int selectedIndex: 0
  readonly property var selectedCommand: filtered.count > 0 ? filtered.get(selectedIndex) : null
  signal commandSelected(var command)
  width: 240
  height: Math.min(312, 56 + filtered.count * 42)
  radius: 8
  color: Theme.surface
  border.color: Theme.border
  border.width: 1
  visible: false
  z: 20

  ListModel { id: filtered }

  function openFor(text) {
    root.query = text
    rebuild()
    root.selectedIndex = 0
    root.visible = true
  }

  function rebuild() {
    filtered.clear()
    const q = root.query.toLowerCase()
    const commands = [
      {label: "Paragraph", detail: "Plain text", shortcut: "Text", type: 0, level: 0},
      {label: "Large header", detail: "Heading 1", shortcut: "# Space", type: 1, level: 1},
      {label: "Medium header", detail: "Heading 2", shortcut: "## Space", type: 1, level: 2},
      {label: "Small header", detail: "Heading 3", shortcut: "### Space", type: 1, level: 3},
      {label: "Bulleted list", detail: "Unordered list", shortcut: "- Space", type: 2, level: 0},
      {label: "Numbered list", detail: "Ordered list", shortcut: "1. Space", type: 3, level: 0}
    ]
    for (let i = 0; i < commands.length; ++i) {
      const command = commands[i]
      if (q.length === 0 || command.label.toLowerCase().indexOf(q) >= 0 ||
          command.detail.toLowerCase().indexOf(q) >= 0)
        filtered.append(command)
    }
    root.height = Math.min(312, 56 + filtered.count * 42)
  }

  function selectFirst() {
    if (filtered.count > 0)
      root.commandSelected(filtered.get(root.selectedIndex))
  }

  function moveSelection(direction) {
    if (filtered.count === 0)
      return
    root.selectedIndex = (root.selectedIndex + direction + filtered.count) % filtered.count
  }

  Text { x: 16; y: 12; text: "Insert formatting"; color: Theme.textMuted; font.bold: true }

  ListView {
    anchors { top: parent.top; topMargin: 40; left: parent.left; right: parent.right; bottom: parent.bottom }
    model: filtered
    clip: true
    delegate: Rectangle {
      width: ListView.view.width
      height: 42
      color: index === root.selectedIndex ? Theme.surfaceHover : "transparent"
      Text { x: 16; anchors.verticalCenter: parent.verticalCenter; text: model.label; color: Theme.text; font.pixelSize: 13 }
      Text { x: 150; anchors.verticalCenter: parent.verticalCenter; text: model.shortcut; color: Theme.textMuted; font.pixelSize: 12 }
      MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; onClicked: root.commandSelected(filtered.get(index)) }
    }
  }
}
