import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Dialogs
import Templnote

Item {
  id: root

  property string blockId: ""
  property int blockType: 8
  property string content: ""
  property int headingLevel: 0
  property int numberIndex: 1
  property string alignment: "left"
  property bool isChecked: false
  property string imageSource: ""
  property real imageWidthRatio: 1.0
  property bool isBlockHovered: false
  property bool showPlaceholder: false
  property var model: null
  property var searchHighlights: []
  property bool searchNavigationActive: false

  readonly property int cursorPosition: 0
  readonly property string editorText: root.content
  readonly property bool isFocused: root.activeFocus || captionInput.activeFocus
  readonly property real firstLineHeight: 24

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
  signal focusDelegateRequested(string blockId, int position)
  signal focusRequested(int position)
  signal searchNavigated(int direction)
  signal searchDismissed()
  signal searchEditStarted()

  implicitWidth: parent ? parent.width : 400
  implicitHeight: root.hasImage ? imageContainer.height + (captionContainer.visible ? captionContainer.height : 0) : placeholderBox.height

  readonly property bool hasImage: root.imageSource && root.imageSource.length > 0
  property bool menuOpen: false
  property bool isHovered: root.isBlockHovered || (imageHoverArea && imageHoverArea.containsMouse) || menuOpen || root.activeFocus || isResizing
  readonly property real horizontalPadding: 8
  readonly property real usableWidth: Math.max(0, root.width - horizontalPadding * 2)

  property bool isResizing: dragHandleLeft.pressed || dragHandleRight.pressed

  // Smooth physics ratio tracking
  property real targetRatio: root.imageWidthRatio
  property real animatedRatio: targetRatio

  // Only run spring physics when NOT dragging (for release-snap or programmatic changes).
  // During active dragging, animatedRatio directly tracks targetRatio without any spring lag.
  Behavior on animatedRatio {
    enabled: !root.isResizing
    NumberAnimation {
      duration: 180
      easing.type: Easing.OutCubic
    }
  }

  // Smooth elastic boundary calculation
  function computeElasticRatio(rawRatio) {
    if (rawRatio > 1.0) {
      var over = rawRatio - 1.0
      return 1.0 + Math.log(1.0 + over * 1.5) * 0.1
    } else if (rawRatio < 0.2) {
      var under = 0.2 - rawRatio
      return Math.max(0.1, 0.2 - Math.log(1.0 + under * 1.5) * 0.08)
    }
    return rawRatio
  }

  onImageWidthRatioChanged: {
    if (!isResizing) {
      targetRatio = root.imageWidthRatio
    }
  }

  function focusEditor(position) {
    if (!root.hasImage) {
      placeholderBox.forceActiveFocus()
    } else {
      root.forceActiveFocus()
    }
  }

  function focusEditorAtX(xPosition) {
    focusEditor(0)
  }

  function focusEditorRange(start, end) {
    focusEditor(0)
  }

  function refreshFromModel() {
    // Model reactive bindings update properties automatically
  }

  signal imageMenuRequested(string blockId, real globalX, real globalY,
                            real globalBottom)

  function toggleSourceMenu() {
    root.menuOpen = !root.menuOpen

    if (root.menuOpen) {
      const target = root.hasImage ? imageContainer : placeholderBox
      const position = target.mapToItem(null, 0, 0)
      root.imageMenuRequested(root.blockId, position.x, position.y,
                              position.y + target.height)
    }
  }

  // ==========================================
  // EMPTY / PLACEHOLDER STATE (Notion style)
  // ==========================================
  Rectangle {
    id: placeholderBox
    visible: !root.hasImage
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.leftMargin: root.horizontalPadding
    anchors.rightMargin: root.horizontalPadding
    anchors.top: parent.top
    height: 44
    radius: 6
    color: placeholderHover.hovered || root.activeFocus
           ? (Theme.isDark ? "#2a2d32" : "#f1f3f5")
           : (Theme.isDark ? "#1e2023" : "#f8f9fa")
    border.color: root.activeFocus
                  ? Theme.accent
                  : (placeholderHover.hovered ? Theme.border : "transparent")
    border.width: 1

    HoverHandler {
      id: placeholderHover
    }

    TapHandler {
      onTapped: root.toggleSourceMenu()
    }

    Keys.onPressed: function(event) {
      if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
        event.accepted = true
        root.backspacePressed()
      } else if (event.key === Qt.Key_Escape) {
        event.accepted = true
        root.menuOpen = false
      } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
        event.accepted = true
        root.toggleSourceMenu()
      } else if (event.key === Qt.Key_Up) {
        event.accepted = true
        root.moveFocusRequested(-1, 0)
      } else if (event.key === Qt.Key_Down) {
        event.accepted = true
        root.moveFocusRequested(1, 0)
      }
    }

    Row {
      anchors.verticalCenter: parent.verticalCenter
      anchors.left: parent.left
      anchors.leftMargin: 12
      spacing: 8

      IconImage {
        width: 18
        height: 18
        source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_image_20_regular.svg"
        color: Theme.textMuted
        anchors.verticalCenter: parent.verticalCenter
      }

      Text {
        text: "Add an image"
        font.pixelSize: 14
        color: Theme.textMuted
        anchors.verticalCenter: parent.verticalCenter
      }
    }
  }

  // ==========================================
  // LOADED IMAGE CONTAINER & CONTROLS
  // ==========================================
  Item {
    id: imageContainer
    visible: root.hasImage
    anchors.top: parent.top
    width: Math.max(120, root.usableWidth * root.animatedRatio)
    // When width changes, compute height preserving aspect ratio, using paintedHeight once loaded
    height: {
      if (renderedImage.status === Image.Ready && renderedImage.sourceSize.width > 0) {
        var aspect = renderedImage.sourceSize.height / renderedImage.sourceSize.width
        return width * aspect
      }
      return 200
    }

    x: {
      if (root.alignment === "right") {
        return Math.max(root.horizontalPadding, root.width - root.horizontalPadding - width)
      } else if (root.alignment === "center") {
        return Math.max(root.horizontalPadding, Math.round(root.horizontalPadding + (root.usableWidth - width) / 2))
      } else {
        return root.horizontalPadding
      }
    }

    Item {
      anchors.fill: parent
      layer.enabled: true
      layer.smooth: true

      Image {
        id: renderedImage
        anchors.fill: parent
        source: MarkdownSyntax.safeImageSource(root.imageSource)
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        mipmap: true
        smooth: true

        // Loading indicator / fallback
        Rectangle {
          anchors.fill: parent
          visible: renderedImage.status === Image.Loading || renderedImage.status === Image.Error
          color: Theme.isDark ? "#282c34" : "#f1f3f5"
          radius: 4

          Text {
            anchors.centerIn: parent
            text: renderedImage.status === Image.Loading ? "Loading image..." : "Failed to load image"
            color: Theme.textMuted
            font.pixelSize: 13
          }
        }
      }
    }

    MouseArea {
      id: imageHoverArea
      anchors.fill: parent
      hoverEnabled: true
      acceptedButtons: Qt.LeftButton
      z: 5
      onClicked: root.forceActiveFocus()
    }

    // Border highlight when activeFocus
    Rectangle {
      anchors.fill: parent
      anchors.margins: -2
      radius: 6
      color: "transparent"
      border.color: root.activeFocus ? Theme.accent : "transparent"
      border.width: 2
      z: 1
    }

    // Notion-style vertical pill resize handles (Left and Right)
    Item {
      id: resizeHandleLeft
      visible: opacity > 0.001
      opacity: root.isHovered ? 1.0 : 0.0
      scale: root.isHovered ? 1.0 : 0.85
      Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
      Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
      width: 24
      height: 56
      anchors.left: parent.left
      anchors.leftMargin: -12
      anchors.verticalCenter: parent.verticalCenter
      z: 25

      Rectangle {
        anchors.centerIn: parent
        width: 6
        height: 48
        radius: 3
        color: (dragHandleLeft.containsMouse || dragHandleLeft.pressed) ? Theme.accent : (Theme.isDark ? "rgba(255,255,255,0.7)" : "rgba(0,0,0,0.5)")
      }

      MouseArea {
        id: dragHandleLeft
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        property real startRatio: 1.0
        property real startGlobalX: 0

        onPressed: function(mouse) {
          startRatio = root.targetRatio
          var g = mapToItem(null, mouse.x, mouse.y)
          startGlobalX = g.x
        }

        onPositionChanged: function(mouse) {
          if (pressed && root.usableWidth > 0) {
            var g = mapToItem(null, mouse.x, mouse.y)
            var deltaPixels = startGlobalX - g.x
            if (root.alignment === "center") {
              deltaPixels *= 2
            }
            var deltaRatio = deltaPixels / root.usableWidth
            root.targetRatio = root.computeElasticRatio(startRatio + deltaRatio)
          }
        }

        onReleased: {
          var finalRatio = Math.max(0.2, Math.min(1.0, root.targetRatio))
          root.targetRatio = finalRatio
          root.model.setBlockImageWidthRatio(root.blockId, finalRatio)
        }
        onCanceled: {
          var finalRatio = Math.max(0.2, Math.min(1.0, root.targetRatio))
          root.targetRatio = finalRatio
          root.model.setBlockImageWidthRatio(root.blockId, finalRatio)
        }
      }
    }

    Item {
      id: resizeHandleRight
      visible: opacity > 0.001
      opacity: root.isHovered ? 1.0 : 0.0
      scale: root.isHovered ? 1.0 : 0.85
      Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
      Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
      width: 24
      height: 56
      anchors.right: parent.right
      anchors.rightMargin: -12
      anchors.verticalCenter: parent.verticalCenter
      z: 25

      Rectangle {
        anchors.centerIn: parent
        width: 6
        height: 48
        radius: 3
        color: (dragHandleRight.containsMouse || dragHandleRight.pressed) ? Theme.accent : (Theme.isDark ? "rgba(255,255,255,0.7)" : "rgba(0,0,0,0.5)")
      }

      MouseArea {
        id: dragHandleRight
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        property real startRatio: 1.0
        property real startGlobalX: 0

        onPressed: function(mouse) {
          startRatio = root.targetRatio
          var g = mapToItem(null, mouse.x, mouse.y)
          startGlobalX = g.x
        }

        onPositionChanged: function(mouse) {
          if (pressed && root.usableWidth > 0) {
            var g = mapToItem(null, mouse.x, mouse.y)
            var deltaPixels = g.x - startGlobalX
            if (root.alignment === "center") {
              deltaPixels *= 2
            }
            var deltaRatio = deltaPixels / root.usableWidth
            root.targetRatio = root.computeElasticRatio(startRatio + deltaRatio)
          }
        }

        onReleased: {
          var finalRatio = Math.max(0.2, Math.min(1.0, root.targetRatio))
          root.targetRatio = finalRatio
          root.model.setBlockImageWidthRatio(root.blockId, finalRatio)
        }
        onCanceled: {
          var finalRatio = Math.max(0.2, Math.min(1.0, root.targetRatio))
          root.targetRatio = finalRatio
          root.model.setBlockImageWidthRatio(root.blockId, finalRatio)
        }
      }
    }

    // Top-right action toolbar (Replace, Align, Delete)
    Rectangle {
      id: actionToolbar
      visible: opacity > 0.001
      opacity: root.isHovered ? 1.0 : 0.0
      scale: root.isHovered ? 1.0 : 0.92
      transformOrigin: Item.TopRight
      Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
      Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
      z: 30
      anchors.top: parent.top
      anchors.right: parent.right
      anchors.margins: 8
      height: 30
      width: actionRow.width + 12
      radius: 6
      color: Theme.isDark ? "rgba(33, 37, 43, 0.92)" : "rgba(255, 255, 255, 0.92)"
      border.color: Theme.border
      border.width: 1

      Row {
        id: actionRow
        anchors.centerIn: parent
        spacing: 6

        // Replace Button
        Rectangle {
          width: 60
          height: 22
          radius: 4
          color: replaceHover.hovered ? (Theme.isDark ? "#3a3f4b" : "#e9ecef") : "transparent"
          HoverHandler { id: replaceHover }
          TapHandler {
            onTapped: root.toggleSourceMenu()
          }
          Text {
            anchors.centerIn: parent
            text: "Replace"
            font.pixelSize: 11
            color: Theme.text
          }
        }

        // Align Toggle
        Rectangle {
          width: 22
          height: 22
          radius: 4
          color: alignHover.hovered ? (Theme.isDark ? "#3a3f4b" : "#e9ecef") : "transparent"
          HoverHandler { id: alignHover }
          TapHandler {
            onTapped: {
              var nextAlign = root.alignment === "center" ? "left" : (root.alignment === "left" ? "right" : "center")
              root.model.setBlockAlignment(root.blockId, nextAlign)
            }
          }
          IconImage {
            anchors.centerIn: parent
            width: 14
            height: 14
            source: root.alignment === "center"
              ? "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_align_center_vertical_20_regular.svg"
              : (root.alignment === "right"
                ? "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_align_right_20_regular.svg"
                : "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_align_left_20_regular.svg")
            color: Theme.text
          }
        }

        // Delete Button
        Rectangle {
          width: 22
          height: 22
          radius: 4
          color: deleteHover.hovered ? (Theme.isDark ? "#4c2d2d" : "#fee2e2") : "transparent"
          HoverHandler { id: deleteHover }
          TapHandler {
            onTapped: root.model.removeBlock(root.blockId)
          }
          IconImage {
            anchors.centerIn: parent
            width: 14
            height: 14
            source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_delete_20_regular.svg"
            color: deleteHover.hovered ? "#ef4444" : Theme.textMuted
          }
        }
      }
    }
  }

  // Caption text input below image
  Item {
    id: captionContainer
    visible: root.hasImage
    anchors.top: imageContainer.bottom
    anchors.topMargin: 4
    anchors.horizontalCenter: imageContainer.horizontalCenter
    width: imageContainer.width
    height: 26

    TextInput {
      id: captionInput
      anchors.fill: parent
      horizontalAlignment: TextInput.AlignHCenter
      verticalAlignment: TextInput.AlignVCenter
      font.pixelSize: 12
      color: Theme.textMuted
      text: root.content
      selectByMouse: true

      Text {
        anchors.centerIn: parent
        text: "Add a caption..."
        font.pixelSize: 12
        color: Theme.textMuted
        opacity: 0.6
        visible: !captionInput.text && !captionInput.activeFocus && root.isHovered
      }

      onEditingFinished: {
        root.model.updateBlockText(root.blockId, text)
      }

      Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
          event.accepted = true
          root.enterPressed()
        } else if (event.key === Qt.Key_Escape) {
          event.accepted = true
          root.forceActiveFocus()
        }
      }
    }
  }

  // Keyboard navigation & deletion when image block is selected
  Keys.onPressed: function(event) {
    if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
      event.accepted = true
      root.model.removeBlock(root.blockId)
    } else if (event.key === Qt.Key_Escape) {
      event.accepted = true
      root.menuOpen = false
    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
      event.accepted = true
      root.enterPressed()
    } else if (event.key === Qt.Key_Up) {
      event.accepted = true
      root.moveFocusRequested(-1, 0)
    } else if (event.key === Qt.Key_Down) {
      event.accepted = true
      root.moveFocusRequested(1, 0)
    }
  }
}
