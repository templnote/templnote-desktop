import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl

Rectangle {
  id: root

  property bool expanded: true
  property bool agentRunning: true
  property string currentTask: "Generating..."
  property string statusText: agentRunning
    ? (currentTask.length > 0 ? selectedModel + " • " + currentTask : selectedModel)
    : "Meow"
  property string selectedModel: "Claude 3.5 Sonnet"
  property var availableModels: [
    "Claude 3.5 Sonnet",
    "GPT-4o",
    "Gemini 1.5 Pro",
    "Ollama (Local)"
  ]

  property real expandedWidth: 820
  readonly property real minCollapsedWidth: 120
  readonly property real maxCollapsedWidth: 420

  // Collapsed width fits content: leftMargin (12) + catIcon (24) + spacing (8) + statusDisplay.implicitWidth + spacing (8) + toggleButton (24) + rightMargin (8) = implicitWidth + 84
  readonly property real fittedCollapsedWidth: Math.min(
    root.maxCollapsedWidth,
    Math.max(root.minCollapsedWidth, statusDisplay.implicitWidth + 84)
  )

  readonly property real minExpandedHeight: 84
  readonly property real maxExpandedHeight: 200
  readonly property real collapsedHeight: 36

  // Calculate target height when expanded based on text area content height + bottom toolbar + margins
  // Top margin (10) + text edit height + middle spacing (6) + bottom bar (28) + bottom margin (10) = contentHeight + 54
  readonly property real calculatedExpandedHeight: Math.min(
    root.maxExpandedHeight,
    Math.max(root.minExpandedHeight, promptInput.contentHeight + 54)
  )

  width: root.expanded ? root.expandedWidth : root.fittedCollapsedWidth
  height: root.expanded ? root.calculatedExpandedHeight : root.collapsedHeight
  color: Theme.surface
  border.width: 1
  border.color: Theme.border
  topLeftRadius: 8
  topRightRadius: 8
  bottomLeftRadius: 0
  bottomRightRadius: 0

  Behavior on width {
    NumberAnimation {
      duration: 180
      easing.type: Easing.OutCubic
    }
  }

  Behavior on height {
    NumberAnimation {
      duration: 180
      easing.type: Easing.OutCubic
    }
  }

  // --- COLLAPSED VIEW ---
  Button {
    id: collapsedCatIcon
    visible: !root.expanded

    icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_animal_cat_20_regular.svg"
    icon.width: 20
    icon.height: 20
    icon.color: Theme.accent
    width: 24
    height: 24
    padding: 0
    background: null

    anchors {
      left: parent.left
      leftMargin: 12
      verticalCenter: parent.verticalCenter
    }

    onClicked: root.expanded = true
  }

  Item {
    id: statusDisplayContainer
    visible: !root.expanded
    anchors {
      left: collapsedCatIcon.right
      leftMargin: 8
      right: toggleButton.left
      rightMargin: 8
      verticalCenter: parent.verticalCenter
    }
    height: statusDisplay.implicitHeight
    clip: true

    Text {
      id: statusDisplay
      anchors.fill: parent
      text: root.statusText
      color: root.agentRunning ? Theme.accent : Theme.textMuted
      font.pixelSize: 13
      elide: Text.ElideRight

      Behavior on color {
        ColorAnimation { duration: 200 }
      }
    }

    // Text shimmer wave across status text when agent is running
    Rectangle {
      id: textShimmerSweep
      visible: root.agentRunning
      width: 48
      height: parent.height
      y: 0
      x: -width
      opacity: 0.65

      gradient: Gradient {
        orientation: Gradient.Horizontal
        GradientStop { position: 0.0; color: "transparent" }
        GradientStop { position: 0.5; color: Theme.accent }
        GradientStop { position: 1.0; color: "transparent" }
      }

      NumberAnimation on x {
        running: textShimmerSweep.visible
        from: -textShimmerSweep.width
        to: statusDisplayContainer.width
        duration: 1800
        loops: Animation.Infinite
        easing.type: Easing.InOutQuad
      }
    }
  }

  // --- EXPANDED VIEW: TOP/MAIN TEXT INPUT ---
  ScrollView {
    id: textScrollView
    visible: root.expanded
    clip: true

    anchors {
      top: parent.top
      topMargin: 10
      left: parent.left
      leftMargin: 14
      right: toggleButton.left
      rightMargin: 8
      bottom: bottomToolbar.top
      bottomMargin: 4
    }

    ScrollBar.vertical.policy: promptInput.contentHeight + 54 > root.maxExpandedHeight
      ? ScrollBar.AsNeeded
      : ScrollBar.AlwaysOff

    TextArea {
      id: promptInput

      font.pixelSize: 13
      color: Theme.text
      placeholderText: "Ask Templnote anything..."
      placeholderTextColor: Theme.textFaint
      selectionColor: Theme.accent
      selectedTextColor: Theme.accentText
      wrapMode: TextEdit.Wrap
      padding: 0
      leftPadding: 0
      rightPadding: 0
      topPadding: 0
      bottomPadding: 0

      background: null

      Keys.onReturnPressed: function(event) {
        if (event.modifiers & Qt.ShiftModifier) {
          event.accepted = false
        } else {
          event.accepted = true
          root.sendPrompt()
        }
      }
    }
  }

  // --- EXPANDED VIEW: BOTTOM TOOLBAR ---
  Item {
    id: bottomToolbar
    visible: root.expanded
    height: 28

    anchors {
      left: parent.left
      leftMargin: 10
      right: parent.right
      rightMargin: 10
      bottom: parent.bottom
      bottomMargin: 8
    }

    // Cat icon (leftmost)
    Button {
      id: catIcon
      width: 28
      height: 28
      padding: 0

      anchors {
        left: parent.left
        verticalCenter: parent.verticalCenter
      }

      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_animal_cat_20_regular.svg"
      icon.width: 18
      icon.height: 18
      icon.color: Theme.accent

      background: Rectangle {
        radius: 6
        color: catIcon.hovered ? Theme.surfaceHover : "transparent"
      }
    }

    // Add icon (+)
    Button {
      id: addIcon
      width: 28
      height: 28
      padding: 0

      anchors {
        left: catIcon.right
        leftMargin: 4
        verticalCenter: parent.verticalCenter
      }

      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_add_20_regular.svg"
      icon.width: 18
      icon.height: 18
      icon.color: Theme.textMuted

      background: Rectangle {
        radius: 6
        color: addIcon.hovered ? Theme.surfaceHover : "transparent"
      }
    }

    // Model Selector Dropdown
    Button {
      id: modelButton
      height: 24
      padding: 0

      anchors {
        left: addIcon.right
        leftMargin: 6
        verticalCenter: parent.verticalCenter
      }

      contentItem: Row {
        spacing: 4
        leftPadding: 8
        rightPadding: 8
        topPadding: 3
        bottomPadding: 3

        Text {
          text: root.selectedModel
          color: Theme.textMuted
          font.pixelSize: 12
          anchors.verticalCenter: parent.verticalCenter
        }

        IconImage {
          source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_chevron_down_20_regular.svg"
          width: 12
          height: 12
          sourceSize.width: 12
          sourceSize.height: 12
          color: Theme.textMuted
          anchors.verticalCenter: parent.verticalCenter
        }
      }

      background: Rectangle {
        radius: 6
        color: modelButton.hovered || modelMenu.visible ? Theme.surfaceHover : Theme.surfaceAlt
        border.color: Theme.border
        border.width: 1
      }

      onClicked: {
        if (modelMenu.visible) {
          modelMenu.close()
        } else {
          modelMenu.open()
        }
      }

      Menu {
        id: modelMenu
        y: -height - 4
        width: 170

        background: Rectangle {
          radius: 8
          color: Theme.surface
          border.color: Theme.border
          border.width: 1
        }

        Instantiator {
          model: root.availableModels
          delegate: MenuItem {
            id: menuItem
            text: modelData
            height: 30

            contentItem: Text {
              text: menuItem.text
              font.pixelSize: 12
              color: menuItem.text === root.selectedModel ? Theme.accent : Theme.text
              verticalAlignment: Text.AlignVCenter
              leftPadding: 10
            }

            background: Rectangle {
              radius: 4
              color: menuItem.hovered ? Theme.surfaceHover : "transparent"
            }

            onTriggered: {
              root.selectedModel = modelData
            }
          }
          onObjectAdded: function(index, object) {
            modelMenu.insertItem(index, object)
          }
          onObjectRemoved: function(index, object) {
            modelMenu.removeItem(object)
          }
        }
      }
    }

    // Send / Arrow button (rightmost)
    Button {
      id: sendButton
      width: 28
      height: 28
      padding: 0

      anchors {
        right: parent.right
        verticalCenter: parent.verticalCenter
      }

      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_send_16_regular.svg"
      icon.width: 18
      icon.height: 18
      icon.color: promptInput.text.trim().length > 0 ? Theme.accent : Theme.textMuted

      background: Rectangle {
        radius: 6
        color: sendButton.hovered ? Theme.surfaceHover : "transparent"
      }

      onClicked: {
        root.sendPrompt()
      }
    }

    // Mic Pulse icon (to the left of Send)
    Button {
      id: micButton
      width: 28
      height: 28
      padding: 0

      anchors {
        right: sendButton.left
        rightMargin: 4
        verticalCenter: parent.verticalCenter
      }

      icon.source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_mic_pulse_20_regular.svg"
      icon.width: 18
      icon.height: 18
      icon.color: Theme.textMuted

      background: Rectangle {
        radius: 6
        color: micButton.hovered ? Theme.surfaceHover : "transparent"
      }
    }
  }

  // --- STREAMING PERIMETER SHIMMER (Curves around rounded top corners and traces perimeter) ---
  Canvas {
    id: perimeterShimmerCanvas
    anchors.fill: parent
    z: 2
    antialiasing: true
    renderTarget: Canvas.FramebufferObject
    renderStrategy: Canvas.Cooperative
    visible: root.agentRunning
    opacity: root.agentRunning ? 1.0 : 0.0

    Behavior on opacity {
      NumberAnimation { duration: 250; easing.type: Easing.InOutQuad }
    }

    property real shimmerPhase: 0.0

    // Smooth, slow progression from bottom-left to bottom-right
    NumberAnimation on shimmerPhase {
      running: perimeterShimmerCanvas.visible
      from: 0.0
      to: 1.0
      duration: 5200
      loops: Animation.Infinite
    }

    onShimmerPhaseChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onVisibleChanged: if (visible) requestPaint()

    Connections {
      target: Theme
      function onAccentChanged() { perimeterShimmerCanvas.requestPaint() }
    }

    // Open path starting at bottom-left, going up, around the corners, and ending at bottom-right
    function roundedTopArchPath(ctx, x, y, w, h, r) {
      ctx.beginPath()
      ctx.moveTo(x, y + h)
      ctx.lineTo(x, y + r)
      ctx.arcTo(x, y, x + r, y, r)
      ctx.lineTo(x + w - r, y)
      ctx.arcTo(x + w, y, x + w, y + r, r)
      ctx.lineTo(x + w, y + h)
    }

    onPaint: {
      const ctx = getContext("2d")
      ctx.clearRect(0, 0, width, height)

      if (!root.agentRunning || width <= 0 || height <= 0)
        return

      const w = width
      const h = height
      const r = root.topLeftRadius // 8

      // Exposed perimeter length (left wall + top-left curve + top wall + top-right curve + right wall):
      // Left vertical: (h - r)
      // Top-left arc: (PI * r / 2)
      // Top horizontal: (w - 2 * r)
      // Top-right arc: (PI * r / 2)
      // Right vertical: (h - r)
      const arcLen = Math.PI * r * 0.5
      const totalLen = (h - r) + arcLen + (w - 2 * r) + arcLen + (h - r)
      if (totalLen <= 0)
        return

      // Length of the glowing shimmer beam
      const beamLen = Math.max(90, Math.min(220, totalLen * 0.22))
      // Starts emerging at bottom-left (-beamLen) and completes exiting at bottom-right (totalLen)
      const offset = -beamLen + shimmerPhase * (totalLen + beamLen)

      ctx.save()
      ctx.lineWidth = 1.5
      ctx.lineCap = "round"
      ctx.lineJoin = "round"
      ctx.strokeStyle = Theme.accent
      ctx.setLineDash([beamLen, totalLen * 2])
      ctx.lineDashOffset = -offset

      roundedTopArchPath(ctx, 0.75, 0.75, w - 1.5, h - 0.75, Math.max(0, r - 0.75))
      ctx.stroke()
      ctx.restore()
    }
  }

  // --- COLLAPSE / EXPAND TOGGLE BUTTON ---
  Button {
    id: toggleButton
    width: 24
    height: 24
    padding: 0
    background: null

    icon.source: root.expanded ? "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_chevron_double_down_20_filled.svg"
                               : "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_chevron_double_up_20_filled.svg"
    icon.width: 18
    icon.height: 18
    icon.color: Theme.textMuted

    anchors {
      top: parent.top
      right: parent.right
      topMargin: 6
      rightMargin: 8
    }

    onClicked: {
      root.expanded = !root.expanded
    }
  }

  function sendPrompt() {
    const text = promptInput.text.trim()
    if (text.length === 0)
      return

    promptInput.text = ""
  }
}
