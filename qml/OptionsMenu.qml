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

  // Actions offered by the floating menu. Main.qml handles the keys and may
  // override the list; these defaults keep the control useful standalone.
  property var options: [
    { key: "find", label: "Find in document", shortcut: "Ctrl+/" },
    { key: "globalSearch", label: "Search all documents", shortcut: "Ctrl+Shift+/" },
    { key: "shortcuts", label: "Keyboard shortcuts", shortcut: "F1" },
    { key: "settings", label: "Settings", shortcut: "" }
  ]

  // Matches CommandMenu: 240px wide, 42px rows, a 40px header area and 16px
  // below the list. The bottom room keeps the last row clear of the radius.
  readonly property int menuWidth: 180
  readonly property int itemHeight: 30
  readonly property int menuTopPadding: 40
  readonly property int menuBottomPadding: 16
  readonly property int menuMaxHeight: 260

  property bool searchActive: false
  property bool globalSearchActive: false
  property bool fullWidthActive: false

  signal optionTriggered(string key)
  signal searchRequested()
  signal globalSearchRequested()
  signal toggleFullWidthRequested()

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

    Behavior on color {
      ColorAnimation {
        duration: 150
        easing.type: Easing.OutQuad
      }
    }

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
    id: globalSearchButton

    anchors {
      top: trigger.top
      right: trigger.left
      rightMargin: 6
    }

    width: 28
    height: 28
    radius: 8
    color: globalSearchMouse.containsMouse || root.globalSearchActive
           ? Theme.surfaceHover
           : "transparent"

    Behavior on color {
      ColorAnimation {
        duration: 150
        easing.type: Easing.OutQuad
      }
    }

    IconImage {
      anchors.centerIn: parent
      width: 20
      height: 20
      source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_globe_search_20_regular.svg"
      sourceSize.width: 20
      sourceSize.height: 20
      color: root.globalSearchActive ? Theme.accent : Theme.textMuted
    }

    MouseArea {
      id: globalSearchMouse

      anchors.fill: parent
      hoverEnabled: true
      cursorShape: Qt.PointingHandCursor

      onClicked: {
        root.globalSearchRequested()
      }
    }

    ToolTip.visible: globalSearchMouse.containsMouse
    ToolTip.text: "Search all documents (Ctrl+Shift+/)"
    ToolTip.delay: 500
  }

  Rectangle {
    id: searchButton

    anchors {
      top: trigger.top
      right: globalSearchButton.left
      rightMargin: 6
    }

    width: 28
    height: 28
    radius: 8
    color: searchMouse.containsMouse || root.searchActive
           ? Theme.surfaceHover
           : "transparent"

    Behavior on color {
      ColorAnimation {
        duration: 150
        easing.type: Easing.OutQuad
      }
    }

    IconImage {
      anchors.centerIn: parent
      width: 20
      height: 20
      source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_search_20_regular.svg"
      sourceSize.width: 20
      sourceSize.height: 20
      color: root.searchActive ? Theme.accent : Theme.textMuted
    }

    MouseArea {
      id: searchMouse

      anchors.fill: parent
      hoverEnabled: true
      cursorShape: Qt.PointingHandCursor

      onClicked: {
        root.searchRequested()
      }
    }

    ToolTip.visible: searchMouse.containsMouse
    ToolTip.text: "Find in document (Ctrl+/)"
    ToolTip.delay: 500
  }

  Rectangle {
    id: fullWidthButton

    anchors {
      top: trigger.top
      right: searchButton.left
      rightMargin: 6
    }

    width: 28
    height: 28
    radius: 8
    color: fullWidthMouse.containsMouse
           ? Theme.surfaceHover
           : "transparent"

    Behavior on color {
      ColorAnimation {
        duration: 150
        easing.type: Easing.OutQuad
      }
    }

    IconImage {
      anchors.centerIn: parent
      width: 20
      height: 20
      source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_arrow_autofit_width_20_regular.svg"
      sourceSize.width: 20
      sourceSize.height: 20
      color: root.fullWidthActive ? Theme.accent : Theme.textMuted
    }

    MouseArea {
      id: fullWidthMouse

      anchors.fill: parent
      hoverEnabled: true
      cursorShape: Qt.PointingHandCursor

      onClicked: {
        root.toggleFullWidthRequested()
      }
    }

    ToolTip.visible: fullWidthMouse.containsMouse
    ToolTip.text: root.fullWidthActive ? "Use standard width" : "Use full width"
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
        leftMargin: 8
        right: parent.right
        rightMargin: 8
        bottom: parent.bottom
        bottomMargin: 8
      }

      clip: true
      model: root.options
      spacing: 1

      delegate: Rectangle {
        id: optionRow

        required property int index
        required property var modelData

        readonly property bool selected: index === root.selectedIndex

        width: ListView.view.width
        height: root.itemHeight
        radius: 8
        color: selected
               ? Theme.tint(Theme.accent, 0.16)
               : (optionMouse.containsMouse ? Theme.surfaceHover : "transparent")

        Behavior on color {
          ColorAnimation {
            duration: 150
            easing.type: Easing.OutQuad
          }
        }

        Rectangle {
          visible: optionRow.selected

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
          text: modelData.label
          color: optionRow.selected ? Theme.text : Theme.text
          font.pixelSize: 13
        }

        Text {
          anchors {
            right: parent.right
            rightMargin: 14
            verticalCenter: parent.verticalCenter
          }
          visible: text.length > 0
          text: modelData.shortcut
          color: Theme.textMuted
          font.pixelSize: 12
        }

        MouseArea {
          id: optionMouse
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
