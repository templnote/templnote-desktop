import QtQuick
import QtQuick.Controls

Rectangle {
  id: root
  objectName: "commandMenu"
  property string query: ""
  property int selectedIndex: 0
  readonly property var selectedCommand: filtered.count > 0 ? filtered.get(selectedIndex) : null
  signal commandSelected(var command)
  readonly property int maxVisibleItems: 6
  readonly property int itemRowHeight: 30
  readonly property int itemRowSpacing: 1
  readonly property int headerHeight: 40
  readonly property int bottomPadding: 8
  readonly property int pickerHeight: 132

  // The 17 commands are created once. rebuild() only rewrites the ListModel
  // when the set of matching commands actually changes, so typing a character
  // that leaves the same matches visible causes no model churn.
  readonly property var allCommands: [
    {label: "Paragraph", detail: "Plain text", shortcut: "Text", type: 0, level: 0},
    {label: "Large header", detail: "Heading 1", shortcut: "# Space", type: 1, level: 1},
    {label: "Medium header", detail: "Heading 2", shortcut: "## Space", type: 1, level: 2},
    {label: "Small header", detail: "Heading 3", shortcut: "### Space", type: 1, level: 3},
    {label: "Tiny header", detail: "Heading 4", shortcut: "#### Space", type: 1, level: 4},
    {label: "Minor header", detail: "Heading 5", shortcut: "##### Space", type: 1, level: 5},
    {label: "Micro header", detail: "Heading 6", shortcut: "###### Space", type: 1, level: 6},
    {label: "Bulleted list", detail: "Unordered list", shortcut: "- Space", type: 2, level: 0},
    {label: "Numbered list", detail: "Ordered list", shortcut: "1. Space", type: 3, level: 0},
    {label: "To-do list", detail: "Checklist", shortcut: "[] Space", type: 4, level: 0},
    {label: "Quote", detail: "Capture a quote", shortcut: "> Space", type: 5, level: 0},
    {label: "Code block", detail: "Code with syntax highlighting", shortcut: "``` Space", type: 6, level: 0},
    {label: "Divider", detail: "Visual divider line", shortcut: "---", type: 7, level: 0},
    {label: "Image", detail: "Upload or embed with a link", shortcut: "/image", type: 8, level: 0},
    {label: "Callout", detail: "Make writing stand out", shortcut: "[!NOTE]", type: 10, level: 0},
    {label: "Table", detail: "Add a flexible table", shortcut: "/table", type: 11, level: 0},
    {label: "Columns", detail: "Side-by-side columns", shortcut: "Layout", type: 12, level: 0}
  ]
  property string lastMatchKey: ""

  // 0 = formatting list, 1 = column count stepper. Column layouts are only
  // created here; there is deliberately no markdown/typed trigger.
  property int mode: 0
  property int columnCount: 2
  readonly property int minColumns: 2
  readonly property int maxColumns: 5

  signal columnCountConfirmed(int count)
  signal pickerCancelled()

  width: 240
  height: {
    if (root.mode === 1)
      return root.pickerHeight

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
    root.mode = 0
    rebuild()
    root.selectedIndex = 0
    if (commandList)
      commandList.positionViewAtIndex(0, ListView.Beginning)
    root.visible = true
  }

  function openCountPicker() {
    root.mode = 1
    root.columnCount = Math.max(root.minColumns,
                                Math.min(root.maxColumns,
                                         Preferences.columnCount | 0))
    root.visible = true
  }

  function moveCountSelection(direction) {
    if (root.mode !== 1)
      return
    root.columnCount = Math.max(root.minColumns,
                                Math.min(root.maxColumns,
                                         root.columnCount + direction))
  }

  function confirmCount() {
    if (root.mode !== 1)
      return
    root.visible = false
    root.mode = 0
    root.columnCountConfirmed(root.columnCount)
  }

  function cancelCount() {
    if (root.mode !== 1)
      return
    root.visible = false
    root.mode = 0
    root.pickerCancelled()
  }

  function rebuild() {
    const q = root.query.toLowerCase()
    const matches = []

    for (let i = 0; i < root.allCommands.length; ++i) {
      const command = root.allCommands[i]
      if (q.length === 0 || command.label.toLowerCase().indexOf(q) >= 0 ||
          command.detail.toLowerCase().indexOf(q) >= 0)
        matches.push(i)
    }

    // Skip the model rewrite when the query still matches the same commands.
    // Height is a binding on mode/filtered.count, so no manual assignment is
    // needed (assigning it here used to overlap the picker's controls).
    const key = matches.join(",")
    if (key === root.lastMatchKey)
      return
    root.lastMatchKey = key

    filtered.clear()
    for (let m = 0; m < matches.length; ++m)
      filtered.append(root.allCommands[matches[m]])
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

  Text {
    x: 16; y: 12
    text: "Insert formatting"
    color: Theme.textMuted
    font.bold: true
    visible: root.mode === 0
  }

  ListView {
    id: commandList
    visible: root.mode === 0
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
        color: Theme.text
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

  // Second step: choose how many columns to create.
  Item {
    id: countPanel
    anchors.fill: parent
    visible: root.mode === 1

    Text {
      x: 16
      y: 10
      text: "Number of columns"
      color: Theme.textMuted
      font.bold: true
      font.pixelSize: 12
    }

    Item {
      anchors {
        top: parent.top
        topMargin: 38
        horizontalCenter: parent.horizontalCenter
      }
      width: 180
      height: 40

      Rectangle {
        anchors.fill: parent
        radius: 8
        color: Theme.surfaceHover
        border.color: Theme.border
        border.width: 1
      }

      Rectangle {
        id: minusButton
        anchors {
          left: parent.left
          leftMargin: 4
          verticalCenter: parent.verticalCenter
        }
        width: 30
        height: 30
        radius: 6
        color: minusMouse.containsMouse ? Theme.tint(Theme.accent, 0.16)
                                        : "transparent"

        Text {
          anchors.centerIn: parent
          text: "−"
          font.pixelSize: 20
          color: Theme.text
        }

        MouseArea {
          id: minusMouse
          anchors.fill: parent
          hoverEnabled: true
          cursorShape: Qt.PointingHandCursor
          onClicked: root.moveCountSelection(-1)
        }
      }

      Text {
        anchors.centerIn: parent
        text: root.columnCount + " columns"
        color: Theme.text
        font.pixelSize: 14
        font.bold: true
      }

      Rectangle {
        id: plusButton
        anchors {
          right: parent.right
          rightMargin: 4
          verticalCenter: parent.verticalCenter
        }
        width: 30
        height: 30
        radius: 6
        color: plusMouse.containsMouse ? Theme.tint(Theme.accent, 0.16)
                                       : "transparent"

        Text {
          anchors.centerIn: parent
          text: "+"
          font.pixelSize: 18
          color: Theme.text
        }

        MouseArea {
          id: plusMouse
          anchors.fill: parent
          hoverEnabled: true
          cursorShape: Qt.PointingHandCursor
          onClicked: root.moveCountSelection(1)
        }
      }
    }

    Row {
      anchors {
        bottom: parent.bottom
        bottomMargin: 10
        horizontalCenter: parent.horizontalCenter
      }
      spacing: 8

      Rectangle {
        width: 88
        height: 28
        radius: 6
        color: backMouse.containsMouse ? Theme.surfaceHover : "transparent"
        border.color: Theme.border
        border.width: 1

        Text {
          anchors.centerIn: parent
          text: "Back"
          color: Theme.textMuted
          font.pixelSize: 12
        }

        MouseArea {
          id: backMouse
          anchors.fill: parent
          hoverEnabled: true
          cursorShape: Qt.PointingHandCursor
          onClicked: root.cancelCount()
        }
      }

      Rectangle {
        width: 88
        height: 28
        radius: 6
        color: insertMouse.containsMouse
               ? Theme.tint(Theme.accent, 0.16)
               : Theme.tint(Theme.accent, 0.08)
        border.color: Theme.accent
        border.width: 1

        Text {
          anchors.centerIn: parent
          text: "Insert"
          color: Theme.accent
          font.pixelSize: 12
          font.bold: true
        }

        MouseArea {
          id: insertMouse
          anchors.fill: parent
          hoverEnabled: true
          cursorShape: Qt.PointingHandCursor
          onClicked: root.confirmCount()
        }
      }
    }
  }
}
