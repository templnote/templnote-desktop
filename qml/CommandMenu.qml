import QtQuick
import QtQuick.Controls

Rectangle {
  id: root
  property string query: ""
  property int selectedIndex: 0
  readonly property var selectedCommand: filtered.count > 0 ? filtered.get(selectedIndex) : null
  signal commandSelected(var command)
  readonly property int maxVisibleItems: 6
  readonly property int itemRowHeight: 30
  readonly property int itemRowSpacing: 1
  readonly property int headerHeight: 40
  readonly property int bottomPadding: 8

  width: 240
  height: {
    const visibleCount = Math.min(filtered.count, root.maxVisibleItems)
    const listHeight = visibleCount > 0
        ? (visibleCount * root.itemRowHeight) + ((visibleCount - 1) * root.itemRowSpacing)
        : 0
    return root.headerHeight + listHeight + root.bottomPadding
  }
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
    if (commandList)
      commandList.positionViewAtIndex(0, ListView.Beginning)
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
      {label: "Numbered list", detail: "Ordered list", shortcut: "1. Space", type: 3, level: 0},
      {label: "To-do list", detail: "Checklist", shortcut: "[] Space", type: 4, level: 0},
      {label: "Quote", detail: "Capture a quote", shortcut: "> Space", type: 5, level: 0},
      {label: "Code block", detail: "Code with syntax highlighting", shortcut: "``` Space", type: 6, level: 0},
      {label: "Divider", detail: "Visual divider line", shortcut: "---", type: 7, level: 0},
      {label: "Image", detail: "Upload or embed with a link", shortcut: "/image", type: 8, level: 0},
      {label: "Callout", detail: "Make writing stand out", shortcut: "[!NOTE]", type: 10, level: 0},
      {label: "Table", detail: "Add a flexible table", shortcut: "/table", type: 11, level: 0}
    ]
    for (let i = 0; i < commands.length; ++i) {
      const command = commands[i]
      if (q.length === 0 || command.label.toLowerCase().indexOf(q) >= 0 ||
          command.detail.toLowerCase().indexOf(q) >= 0)
        filtered.append(command)
    }
    const visibleCount = Math.min(filtered.count, root.maxVisibleItems)
    const listHeight = visibleCount > 0
        ? (visibleCount * root.itemRowHeight) + ((visibleCount - 1) * root.itemRowSpacing)
        : 0
    root.height = root.headerHeight + listHeight + root.bottomPadding
  }

  function selectFirst() {
    if (filtered.count > 0)
      root.commandSelected(filtered.get(root.selectedIndex))
  }

  function moveSelection(direction) {
    if (filtered.count === 0)
      return
    root.selectedIndex = (root.selectedIndex + direction + filtered.count) % filtered.count
    commandList.positionViewAtIndex(root.selectedIndex, ListView.Contain)
  }

  Text { x: 16; y: 12; text: "Insert formatting"; color: Theme.textMuted; font.bold: true }

  ListView {
    id: commandList
    anchors {
      top: parent.top
      topMargin: root.headerHeight
      left: parent.left
      leftMargin: 8
      right: parent.right
      rightMargin: 8
      bottom: parent.bottom
      bottomMargin: root.bottomPadding
    }
    spacing: root.itemRowSpacing
    model: filtered
    clip: true
    delegate: Rectangle {
      id: commandRow

      readonly property bool selected: index === root.selectedIndex

      width: ListView.view.width
      height: root.itemRowHeight
      radius: 8
      color: selected
             ? Theme.tint(Theme.accent, 0.16)
             : (mouse.containsMouse ? Theme.surfaceHover : "transparent")

      Behavior on color {
        ColorAnimation {
          duration: 150
          easing.type: Easing.OutQuad
        }
      }

      Rectangle {
        visible: commandRow.selected

        anchors {
          left: parent.left
          verticalCenter: parent.verticalCenter
        }

        width: 3
        height: 18
        radius: 2
        color: Theme.accent
      }

      Text {
        anchors {
          left: parent.left
          leftMargin: 14
          verticalCenter: parent.verticalCenter
        }
        text: model.label
        color: commandRow.selected ? Theme.text : Theme.text
        font.pixelSize: 13
      }

      Text {
        anchors {
          right: parent.right
          rightMargin: 14
          verticalCenter: parent.verticalCenter
        }
        text: model.shortcut
        color: Theme.textMuted
        font.pixelSize: 12
      }

      MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.commandSelected(filtered.get(index))
      }
    }
  }
}
