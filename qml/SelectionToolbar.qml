import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl

Rectangle {
  id: root

  // Exposed properties for state/layout
  property int currentBlockType: 0
  property int currentHeadingLevel: 0
  property bool isBold: false
  property bool isItalic: false
  property bool isUnderline: false
  property bool isStrikethrough: false
  property bool isSubscript: false
  property bool isSuperscript: false
  property string selectedColor: "#e6edf3"
  property int fontSizeValue: 16
  property string alignment: "left" // "left", "center", "right", "justify"

  readonly property bool hasActivePopup: (blockTypeMenu && blockTypeMenu.visible) ||
                                          (colorPalettePopup && colorPalettePopup.visible) ||
                                          (fontSizeMenu && fontSizeMenu.visible)

  // Signals (reserved for functionality later)
  signal blockTypeSelected(int type, int level)
  signal boldToggled()
  signal italicToggled()
  signal underlineToggled()
  signal strikethroughToggled()
  signal subscriptToggled()
  signal superscriptToggled()
  signal colorSelected(string color)
  signal fontSizeChanged(int size)
  signal alignmentSelected(string align)

  property bool showToolbar: false

  height: 40
  width: implicitWidth
  implicitWidth: mainRow.implicitWidth + 16
  radius: 8
  color: Theme.surface
  border.color: Theme.border
  border.width: 1
  visible: opacity > 0.0
  opacity: showToolbar ? 1.0 : 0.0
  scale: showToolbar ? 1.0 : 0.94
  z: 25

  // Kinetic slide tracking across selections
  Behavior on x {
    NumberAnimation {
      duration: 220
      easing.type: Easing.OutBack
      easing.overshoot: 1.15
    }
  }

  Behavior on y {
    NumberAnimation {
      duration: 220
      easing.type: Easing.OutBack
      easing.overshoot: 1.15
    }
  }

  // Micro-spring scale and fade pop-in
  Behavior on opacity {
    NumberAnimation {
      duration: 160
      easing.type: Easing.OutQuad
    }
  }

  Behavior on scale {
    NumberAnimation {
      duration: 200
      easing.type: Easing.OutBack
      easing.overshoot: 1.25
    }
  }

  function blockTypeName(type, level) {
    if (type === 1) {
      if (level === 1) return "Heading 1"
      if (level === 2) return "Heading 2"
      if (level === 3) return "Heading 3"
      return "Heading"
    } else if (type === 2) {
      return "Bulleted List"
    } else if (type === 3) {
      return "Numbered List"
    } else if (type === 4) {
      return "To-do List"
    } else if (type === 5) {
      return "Quote"
    } else if (type === 10) {
      return "Callout"
    }
    return "Paragraph"
  }

  function closePopups() {
    blockTypeMenu.close()
    colorPalettePopup.close()
    fontSizeMenu.close()
  }

  Row {
    id: mainRow
    z: 2
    anchors.verticalCenter: parent.verticalCenter
    anchors.left: parent.left
    anchors.leftMargin: 8
    spacing: 4

    // 1. Block Type Selector (nested dropdown)
    Item {
      id: blockTypeBtn
      width: blockTypeRow.implicitWidth + 14
      height: 28
      anchors.verticalCenter: parent.verticalCenter

      Rectangle {
        anchors.fill: parent
        radius: 5
        color: blockTypeMenu.visible
               ? Theme.tint(Theme.accent, 0.16)
               : (blockTypeMouse.containsMouse ? Theme.surfaceHover : "transparent")
      }

      Row {
        id: blockTypeRow
        anchors.centerIn: parent
        spacing: 4

        Text {
          text: root.blockTypeName(root.currentBlockType, root.currentHeadingLevel)
          color: Theme.text
          font.pixelSize: 12
          anchors.verticalCenter: parent.verticalCenter
        }

        IconImage {
          width: 14
          height: 14
          source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_chevron_down_20_regular.svg"
          color: Theme.textMuted
          anchors.verticalCenter: parent.verticalCenter
        }
      }

      MouseArea {
        id: blockTypeMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
          if (blockTypeMenu.visible) {
            blockTypeMenu.close()
          } else {
            root.closePopups()
            blockTypeMenu.open()
          }
        }
      }

      Menu {
        id: blockTypeMenu
        y: blockTypeBtn.height + 4
        width: 180

        background: Rectangle {
          radius: 8
          color: Theme.surface
          border.color: Theme.border
          border.width: 1
        }

        Action {
          text: "Paragraph"
          onTriggered: {
            root.currentBlockType = 0
            root.currentHeadingLevel = 0
            root.blockTypeSelected(0, 0)
          }
        }
        Action {
          text: "Heading 1"
          onTriggered: {
            root.currentBlockType = 1
            root.currentHeadingLevel = 1
            root.blockTypeSelected(1, 1)
          }
        }
        Action {
          text: "Heading 2"
          onTriggered: {
            root.currentBlockType = 1
            root.currentHeadingLevel = 2
            root.blockTypeSelected(1, 2)
          }
        }
        Action {
          text: "Heading 3"
          onTriggered: {
            root.currentBlockType = 1
            root.currentHeadingLevel = 3
            root.blockTypeSelected(1, 3)
          }
        }
        Action {
          text: "Bulleted list"
          onTriggered: {
            root.currentBlockType = 2
            root.currentHeadingLevel = 0
            root.blockTypeSelected(2, 0)
          }
        }
        Action {
          text: "Numbered list"
          onTriggered: {
            root.currentBlockType = 3
            root.currentHeadingLevel = 0
            root.blockTypeSelected(3, 0)
          }
        }
        Action {
          text: "To-do list"
          onTriggered: {
            root.currentBlockType = 4
            root.currentHeadingLevel = 0
            root.blockTypeSelected(4, 0)
          }
        }
        Action {
          text: "Quote"
          onTriggered: {
            root.currentBlockType = 5
            root.currentHeadingLevel = 0
            root.blockTypeSelected(5, 0)
          }
        }
        Action {
          text: "Callout"
          onTriggered: {
            root.currentBlockType = 10
            root.currentHeadingLevel = 0
            root.blockTypeSelected(10, 0)
          }
        }

        delegate: MenuItem {
          id: menuItem
          implicitHeight: 30

          contentItem: Text {
            text: menuItem.text
            color: menuItem.highlighted ? Theme.text : Theme.textMuted
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
            leftPadding: 8
          }

          background: Rectangle {
            color: menuItem.highlighted ? Theme.surfaceHover : "transparent"
            radius: 4
          }
        }
      }
    }

    // Divider
    Rectangle {
      width: 1
      height: 18
      color: Theme.border
      anchors.verticalCenter: parent.verticalCenter
    }

    // 2. Formatting Toggles: Bold, Italic, Underline, Strikethrough,
    //    Subscript, Superscript
    Item {
      width: 28
      height: 28
      anchors.verticalCenter: parent.verticalCenter

      Rectangle {
        anchors.fill: parent
        radius: 5
        color: root.isBold
               ? Theme.tint(Theme.accent, 0.16)
               : (boldMouse.containsMouse ? Theme.surfaceHover : "transparent")
      }

      IconImage {
        anchors.centerIn: parent
        width: 16
        height: 16
        source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_text_bold_20_regular.svg"
        color: root.isBold ? Theme.accent : Theme.text
      }

      MouseArea {
        id: boldMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
          root.isBold = !root.isBold
          root.boldToggled()
        }
      }
    }

    Item {
      width: 28
      height: 28
      anchors.verticalCenter: parent.verticalCenter

      Rectangle {
        anchors.fill: parent
        radius: 5
        color: root.isItalic
               ? Theme.tint(Theme.accent, 0.16)
               : (italicMouse.containsMouse ? Theme.surfaceHover : "transparent")
      }

      IconImage {
        anchors.centerIn: parent
        width: 16
        height: 16
        source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_text_italic_20_regular.svg"
        color: root.isItalic ? Theme.accent : Theme.text
      }

      MouseArea {
        id: italicMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
          root.isItalic = !root.isItalic
          root.italicToggled()
        }
      }
    }

    Item {
      width: 28
      height: 28
      anchors.verticalCenter: parent.verticalCenter

      Rectangle {
        anchors.fill: parent
        radius: 5
        color: root.isUnderline
               ? Theme.tint(Theme.accent, 0.16)
               : (underlineMouse.containsMouse ? Theme.surfaceHover : "transparent")
      }

      IconImage {
        anchors.centerIn: parent
        width: 16
        height: 16
        source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_text_underline_20_regular.svg"
        color: root.isUnderline ? Theme.accent : Theme.text
      }

      MouseArea {
        id: underlineMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
          root.isUnderline = !root.isUnderline
          root.underlineToggled()
        }
      }
    }

    Item {
      width: 28
      height: 28
      anchors.verticalCenter: parent.verticalCenter

      Rectangle {
        anchors.fill: parent
        radius: 5
        color: root.isStrikethrough
               ? Theme.tint(Theme.accent, 0.16)
               : (strikeMouse.containsMouse ? Theme.surfaceHover : "transparent")
      }

      IconImage {
        anchors.centerIn: parent
        width: 16
        height: 16
        source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_text_strikethrough_20_regular.svg"
        color: root.isStrikethrough ? Theme.accent : Theme.text
      }

      MouseArea {
        id: strikeMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
          root.isStrikethrough = !root.isStrikethrough
          root.strikethroughToggled()
        }
      }
    }

    Item {
      width: 28
      height: 28
      anchors.verticalCenter: parent.verticalCenter

      Rectangle {
        anchors.fill: parent
        radius: 5
        color: root.isSubscript
               ? Theme.tint(Theme.accent, 0.16)
               : (subscriptMouse.containsMouse ? Theme.surfaceHover : "transparent")
      }

      IconImage {
        anchors.centerIn: parent
        width: 16
        height: 16
        source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_text_subscript_20_regular.svg"
        color: root.isSubscript ? Theme.accent : Theme.text
      }

      MouseArea {
        id: subscriptMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
          const next = !root.isSubscript
          root.isSubscript = next
          if (next)
            root.isSuperscript = false
          root.subscriptToggled()
        }
      }
    }

    Item {
      width: 28
      height: 28
      anchors.verticalCenter: parent.verticalCenter

      Rectangle {
        anchors.fill: parent
        radius: 5
        color: root.isSuperscript
               ? Theme.tint(Theme.accent, 0.16)
               : (superscriptMouse.containsMouse ? Theme.surfaceHover : "transparent")
      }

      IconImage {
        anchors.centerIn: parent
        width: 16
        height: 16
        source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_text_superscript_20_regular.svg"
        color: root.isSuperscript ? Theme.accent : Theme.text
      }

      MouseArea {
        id: superscriptMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
          const next = !root.isSuperscript
          root.isSuperscript = next
          if (next)
            root.isSubscript = false
          root.superscriptToggled()
        }
      }
    }

    // Divider
    Rectangle {
      width: 1
      height: 18
      color: Theme.border
      anchors.verticalCenter: parent.verticalCenter
    }

    // 3. Text Color Button (with color indicator & nested palette popup)
    Item {
      id: colorBtn
      width: 32
      height: 28
      anchors.verticalCenter: parent.verticalCenter

      Rectangle {
        anchors.fill: parent
        radius: 5
        color: colorPalettePopup.visible
               ? Theme.tint(Theme.accent, 0.16)
               : (colorMouse.containsMouse ? Theme.surfaceHover : "transparent")
      }

      Column {
        anchors.centerIn: parent
        spacing: 2

        IconImage {
          anchors.horizontalCenter: parent.horizontalCenter
          width: 16
          height: 16
          source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_text_color_20_regular.svg"
          color: Theme.text
        }

        Rectangle {
          anchors.horizontalCenter: parent.horizontalCenter
          width: 14
          height: 2
          radius: 1
          color: root.selectedColor
        }
      }

      MouseArea {
        id: colorMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
          if (colorPalettePopup.visible) {
            colorPalettePopup.close()
          } else {
            root.closePopups()
            colorPalettePopup.open()
          }
        }
      }

      Popup {
        id: colorPalettePopup
        y: colorBtn.height + 4
        padding: 8
        width: 196
        height: 140
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
          radius: 8
          color: Theme.surface
          border.color: Theme.border
          border.width: 1
        }

        contentItem: Column {
          spacing: 8

          Text {
            text: "Preset colors"
            font.pixelSize: 11
            color: Theme.textMuted
          }

          // Swatches Grid
          Grid {
            columns: 5
            spacing: 6

            readonly property var colors: [
              "#e6edf3", // Default text
              "#8b949e", // Muted
              "#58a6ff", // Blue
              "#3fb950", // Green
              "#d29922", // Yellow
              "#f85149", // Red
              "#db61a2", // Pink
              "#bc8cff", // Purple
              "#f0883e", // Orange
              "#000000"  // Black
            ]

            Repeater {
              model: parent.colors
              Rectangle {
                required property string modelData
                width: 22
                height: 22
                radius: 4
                color: modelData
                border.color: root.selectedColor.toLowerCase() === modelData.toLowerCase()
                              ? Theme.accent
                              : Theme.border
                border.width: root.selectedColor.toLowerCase() === modelData.toLowerCase() ? 2 : 1

                MouseArea {
                  anchors.fill: parent
                  cursorShape: Qt.PointingHandCursor
                  onClicked: {
                    root.selectedColor = parent.modelData
                    hexInput.text = parent.modelData
                    root.colorSelected(parent.modelData)
                    colorPalettePopup.close()
                  }
                }
              }
            }
          }

          // Hex input row
          Row {
            spacing: 6
            anchors.horizontalCenter: parent.horizontalCenter

            Text {
              text: "#"
              font.pixelSize: 12
              color: Theme.textMuted
              anchors.verticalCenter: parent.verticalCenter
            }

            Rectangle {
              width: 120
              height: 24
              radius: 4
              color: Theme.surfaceAlt
              border.color: Theme.border
              border.width: 1

              TextInput {
                id: hexInput
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                verticalAlignment: TextInput.AlignVCenter
                font.pixelSize: 11
                color: Theme.text
                text: root.selectedColor.replace("#", "")
                maximumLength: 7
                selectByMouse: true

                onAccepted: {
                  let val = text.trim()
                  if (!val.startsWith("#")) val = "#" + val
                  root.selectedColor = val
                  root.colorSelected(val)
                  colorPalettePopup.close()
                }
              }
            }
          }
        }
      }
    }

    // Divider
    Rectangle {
      width: 1
      height: 18
      color: Theme.border
      anchors.verticalCenter: parent.verticalCenter
    }

    // 4. Font Size (Text Field + Dropdown Menu)
    Row {
      spacing: 2
      anchors.verticalCenter: parent.verticalCenter

      Rectangle {
        width: 32
        height: 26
        radius: 4
        color: Theme.surfaceAlt
        border.color: Theme.border
        border.width: 1
        anchors.verticalCenter: parent.verticalCenter

        TextInput {
          id: sizeInput
          anchors.fill: parent
          horizontalAlignment: TextInput.AlignHCenter
          verticalAlignment: TextInput.AlignVCenter
          font.pixelSize: 12
          color: Theme.text
          text: root.fontSizeValue.toString()
          selectByMouse: true

          onAccepted: {
            const parsed = parseInt(text, 10)
            if (!isNaN(parsed) && parsed > 4 && parsed < 120) {
              root.fontSizeValue = parsed
              root.fontSizeChanged(parsed)
            } else {
              text = root.fontSizeValue.toString()
            }
          }
        }
      }

      Item {
        id: sizeDropdownBtn
        width: 16
        height: 26
        anchors.verticalCenter: parent.verticalCenter

        Rectangle {
          anchors.fill: parent
          radius: 3
          color: fontSizeMenu.visible
                 ? Theme.tint(Theme.accent, 0.16)
                 : (sizeDropdownMouse.containsMouse ? Theme.surfaceHover : "transparent")
        }

        IconImage {
          anchors.centerIn: parent
          width: 12
          height: 12
          source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_chevron_down_20_regular.svg"
          color: Theme.textMuted
        }

        MouseArea {
          id: sizeDropdownMouse
          anchors.fill: parent
          hoverEnabled: true
          cursorShape: Qt.PointingHandCursor
          onClicked: {
            if (fontSizeMenu.visible) {
              fontSizeMenu.close()
            } else {
              root.closePopups()
              fontSizeMenu.open()
            }
          }
        }

        Menu {
          id: fontSizeMenu
          y: sizeDropdownBtn.height + 4
          width: 60

          background: Rectangle {
            radius: 8
            color: Theme.surface
            border.color: Theme.border
            border.width: 1
          }

          readonly property var sizes: [10, 12, 14, 16, 18, 20, 24, 28, 32]

          Repeater {
            model: fontSizeMenu.sizes
            MenuItem {
              required property int modelData
              text: modelData.toString()
              implicitHeight: 26

              contentItem: Text {
                text: parent.text
                color: parent.highlighted ? Theme.text : Theme.textMuted
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
              }

              background: Rectangle {
                color: parent.highlighted ? Theme.surfaceHover : "transparent"
                radius: 4
              }

              onTriggered: {
                root.fontSizeValue = modelData
                sizeInput.text = modelData.toString()
                root.fontSizeChanged(modelData)
              }
            }
          }
        }
      }
    }

    // Divider
    Rectangle {
      width: 1
      height: 18
      color: Theme.border
      anchors.verticalCenter: parent.verticalCenter
    }

    // 5. Horizontal Alignment Multi-Button Group
    Row {
      spacing: 2
      anchors.verticalCenter: parent.verticalCenter

      // Align Left
      Item {
        width: 26
        height: 26

        Rectangle {
          anchors.fill: parent
          radius: 4
          color: root.alignment === "left"
                 ? Theme.tint(Theme.accent, 0.16)
                 : (alignLeftMouse.containsMouse ? Theme.surfaceHover : "transparent")
        }

        IconImage {
          anchors.centerIn: parent
          width: 15
          height: 15
          source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_align_left_20_regular.svg"
          color: root.alignment === "left" ? Theme.accent : Theme.text
        }

        MouseArea {
          id: alignLeftMouse
          anchors.fill: parent
          hoverEnabled: true
          cursorShape: Qt.PointingHandCursor
          onClicked: {
            root.alignment = "left"
            root.alignmentSelected("left")
          }
        }
      }

      // Align Center
      Item {
        width: 26
        height: 26

        Rectangle {
          anchors.fill: parent
          radius: 4
          color: root.alignment === "center"
                 ? Theme.tint(Theme.accent, 0.16)
                 : (alignCenterMouse.containsMouse ? Theme.surfaceHover : "transparent")
        }

        IconImage {
          anchors.centerIn: parent
          width: 15
          height: 15
          source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_align_center_vertical_20_regular.svg"
          color: root.alignment === "center" ? Theme.accent : Theme.text
        }

        MouseArea {
          id: alignCenterMouse
          anchors.fill: parent
          hoverEnabled: true
          cursorShape: Qt.PointingHandCursor
          onClicked: {
            root.alignment = "center"
            root.alignmentSelected("center")
          }
        }
      }

      // Align Right
      Item {
        width: 26
        height: 26

        Rectangle {
          anchors.fill: parent
          radius: 4
          color: root.alignment === "right"
                 ? Theme.tint(Theme.accent, 0.16)
                 : (alignRightMouse.containsMouse ? Theme.surfaceHover : "transparent")
        }

        IconImage {
          anchors.centerIn: parent
          width: 15
          height: 15
          source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_align_right_20_regular.svg"
          color: root.alignment === "right" ? Theme.accent : Theme.text
        }

        MouseArea {
          id: alignRightMouse
          anchors.fill: parent
          hoverEnabled: true
          cursorShape: Qt.PointingHandCursor
          onClicked: {
            root.alignment = "right"
            root.alignmentSelected("right")
          }
        }
      }
    }
  }
}
