import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl

// Floating options menu for the editor workspace.
//
// The root fills the workspace so a click anywhere outside the popup dismisses
// it. Only the trigger button and the popup itself handle input.
Item {
  id: root

  property bool menuOpen: false
  property int selectedIndex: 0

  // Placeholder entries. Replace these with the real actions and handle them
  // through optionTriggered().
  property var options: [
    { key: "option-one", label: "Option one", shortcut: "" },
    { key: "option-two", label: "Option two", shortcut: "" },
    { key: "option-three", label: "Option three", shortcut: "" },
    { key: "option-four", label: "Option four", shortcut: "" }
  ]

  // Matches CommandMenu: 240px wide, 42px rows, a 40px header area and 16px
  // below the list. The bottom room keeps the last row clear of the radius.
  readonly property int menuWidth: 180
  readonly property int itemHeight: 42
  readonly property int menuTopPadding: 40
  readonly property int menuBottomPadding: 16
  readonly property int menuMaxHeight: 312

  signal optionTriggered(string key)

  property var previousFocus: null

  function open() {
    previousFocus = Window.window ? Window.window.activeFocusItem : null
    root.selectedIndex = 0
    root.menuOpen = true
    popup.forceActiveFocus()
  }

  function close() {
    if (!root.menuOpen)
      return

    root.menuOpen = false

    if (root.previousFocus) {
      root.previousFocus.forceActiveFocus()
      root.previousFocus = null
    }
  }

  function moveSelection(direction) {
    if (root.options.length === 0)
      return

    root.selectedIndex = (root.selectedIndex + direction + root.options.length)
                         % root.options.length
    optionList.positionViewAtIndex(root.selectedIndex, ListView.Contain)
  }

  function activateSelection() {
    if (root.selectedIndex < 0 || root.selectedIndex >= root.options.length)
      return

    const option = root.options[root.selectedIndex]
    root.optionTriggered(option.key)
    root.close()
  }

  // Dismiss when clicking anywhere outside the trigger or the popup.
  MouseArea {
    anchors.fill: parent
    visible: root.menuOpen
    enabled: root.menuOpen
    acceptedButtons: Qt.AllButtons
    onClicked: root.close()
  }

  Rectangle {
    id: trigger

    anchors {
      top: parent.top
      right: parent.right
      // Matches the document header's 10px top offset. The trigger is the
      // same height as the header's title row so both lines stay centered.
      topMargin: 10
      rightMargin: 20
    }

    width: 28
    height: 28
    radius: 8
    color: triggerMouse.containsMouse || root.menuOpen
           ? Theme.surfaceHover
           : "transparent"

    IconImage {
      anchors.centerIn: parent
      width: 20
      height: 20
      source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_line_horizontal_3_20_regular.svg"
      sourceSize.width: 20
      sourceSize.height: 20
      color: root.menuOpen ? Theme.accent : Theme.textMuted
    }

    MouseArea {
      id: triggerMouse

      anchors.fill: parent
      hoverEnabled: true
      cursorShape: Qt.PointingHandCursor

      onClicked: {
        if (root.menuOpen)
          root.close()
        else
          root.open()
      }
    }

    ToolTip.visible: triggerMouse.containsMouse && !root.menuOpen
    ToolTip.text: "Options"
    ToolTip.delay: 500
  }

  Rectangle {
    id: popup

    anchors {
      top: trigger.bottom
      right: trigger.right
      topMargin: 6
    }

    width: root.menuWidth
    height: Math.min(root.menuMaxHeight,
                     root.menuTopPadding + root.options.length * root.itemHeight
                     + root.menuBottomPadding)
    radius: 8
    color: Theme.surface
    border.width: 1
    border.color: Theme.border
    visible: root.menuOpen
    focus: visible
    z: 1

    onVisibleChanged: {
      if (visible)
        forceActiveFocus()
    }

    Keys.onUpPressed: function(event) {
      root.moveSelection(-1)
      event.accepted = true
    }

    Keys.onDownPressed: function(event) {
      root.moveSelection(1)
      event.accepted = true
    }

    Keys.onReturnPressed: function(event) {
      root.activateSelection()
      event.accepted = true
    }

    Keys.onEnterPressed: function(event) {
      root.activateSelection()
      event.accepted = true
    }

    Keys.onEscapePressed: function(event) {
      root.close()
      event.accepted = true
    }

    Text {
      x: 16
      y: 12
      text: "Options"
      color: Theme.textMuted
      font.bold: true
    }

    ListView {
      id: optionList

      anchors {
        top: parent.top
        topMargin: root.menuTopPadding
        left: parent.left
        right: parent.right
        bottom: parent.bottom
      }

      clip: true
      model: root.options

      delegate: Rectangle {
        required property int index
        required property var modelData

        width: ListView.view.width
        height: root.itemHeight
        color: index === root.selectedIndex ? Theme.surfaceHover : "transparent"

        Text {
          x: 16
          anchors.verticalCenter: parent.verticalCenter
          text: modelData.label
          color: Theme.text
          font.pixelSize: 13
        }

        Text {
          x: 150
          anchors.verticalCenter: parent.verticalCenter
          visible: text.length > 0
          text: modelData.shortcut
          color: Theme.textMuted
          font.pixelSize: 12
        }

        MouseArea {
          anchors.fill: parent
          hoverEnabled: true
          cursorShape: Qt.PointingHandCursor

          onEntered: root.selectedIndex = index
          onClicked: {
            root.selectedIndex = index
            root.activateSelection()
          }
        }
      }
    }
  }
}
