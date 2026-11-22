import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Templnote

Rectangle {
  id: root

  // This decoration's Loader does not inject a pluginContext, so provide one
  // backed by the pluginManager context property. A host-provided context, if
  // any, simply overrides this default.
  property var pluginContext: ({
    pluginId: "prompt-panel",
    getSetting: function(key) {
      return typeof pluginManager !== "undefined" && pluginManager
        ? pluginManager.pluginSetting("prompt-panel", key)
        : undefined
    },
    setSetting: function(key, value) {
      if (typeof pluginManager !== "undefined" && pluginManager)
        pluginManager.setPluginSetting("prompt-panel", key, value)
    }
  })

  property bool expanded: true
  // This is a UI-only placeholder plugin: there is no AI backend wired up, so
  // it must stay idle rather than pretend a task is constantly running.
  property bool agentRunning: false
  property string currentTask: ""
  property string noticeText: ""
  property string statusText: agentRunning
    ? (currentTask.length > 0 ? selectedModel + " • " + currentTask : selectedModel)
    : "Idle (AI unavailable)"
  property string selectedModel: {
    const m = pluginContext && typeof pluginContext.getSetting === "function"
      ? pluginContext.getSetting("defaultModel")
      : undefined
    return (m !== undefined && m !== null && m.toString().length > 0)
      ? m.toString()
      : "Claude 3.5 Sonnet"
  }
  // Keep these identifiers in sync with PromptPanelSettings.qml so the value
  // persisted there is the one displayed and highlighted here.
  property var availableModels: [
    "Claude 3.5 Sonnet",
    "GPT-4o",
    "Gemini 1.5 Pro",
    "Ollama (Local)"
  ]

  // Keep the displayed model in sync when PromptPanelSettings.qml writes a new
  // "defaultModel". Updating the value in place means the host Loader never
  // has to recreate this component (which would discard the user's draft).
  Connections {
    target: typeof pluginManager !== "undefined" ? pluginManager : null

    function onPluginSettingChanged(pluginId, key) {
      if (pluginId !== "prompt-panel")
        return
      // An empty key is the "all settings changed" signal from a reset.
      if (key !== "defaultModel" && key !== "" && key !== undefined && key !== null)
        return
      const m = typeof pluginManager !== "undefined" && pluginManager
        ? pluginManager.pluginSetting("prompt-panel", "defaultModel")
        : undefined
      root.selectedModel = (m !== undefined && m !== null && m.toString().length > 0)
        ? m.toString()
        : "Claude 3.5 Sonnet"
    }
  }

  property real defaultExpandedWidth: 600
  readonly property real responsiveExpandedWidth: parent ? Math.min(defaultExpandedWidth, parent.width - 48) : defaultExpandedWidth
  property real expandedWidth: responsiveExpandedWidth
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
    Math.max(root.minExpandedHeight,
             promptInput.contentHeight + 54 + (root.noticeText.length > 0 ? 20 : 0))
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
      bottomMargin: root.noticeText.length > 0 ? 24 : 4
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

  // Honest feedback: there is no backend, so a submitted prompt is kept in
  // the editor and this notice explains that nothing was sent.
  Text {
    id: noticeLabel
    visible: root.expanded && root.noticeText.length > 0
    anchors {
      left: textScrollView.left
      right: textScrollView.right
      bottom: bottomToolbar.top
      bottomMargin: 4
    }
    text: root.noticeText
    color: Theme.danger
    font.pixelSize: 11
    elide: Text.ElideRight
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
              if (pluginContext && typeof pluginContext.setSetting === "function")
                pluginContext.setSetting("defaultModel", modelData)
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

  // No streaming state exists in this stub, so there is no perimeter shimmer
  // animation (it used an FBO Canvas repainting forever, even while idle).

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

    // Keep the user's text and say plainly that this stub cannot send it.
    // No AI backend is configured, so nothing is actually submitted.
    noticeText = "No AI backend configured — prompt was not sent."
  }
}
