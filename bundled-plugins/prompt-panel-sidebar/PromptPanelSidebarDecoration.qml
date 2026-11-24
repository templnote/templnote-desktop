import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Templnote

// Premium right-sidebar variant of the AI prompt panel. Composer and model
// picker logic are identical to the original decoration; the surrounding
// conversation UI adds an empty state, a live step trace and result cards.
Rectangle {
  id: root
  objectName: "promptPanelRoot"

  property var pluginContext: ({
    pluginId: "prompt-panel-sidebar",
    getSetting: function(key) {
      return typeof pluginManager !== "undefined" && pluginManager
        ? pluginManager.pluginSetting("prompt-panel-sidebar", key)
        : undefined
    },
    setSetting: function(key, value) {
      if (typeof pluginManager !== "undefined" && pluginManager)
        pluginManager.setPluginSetting("prompt-panel-sidebar", key, value)
    }
  })

  readonly property bool expanded: true
  Shortcut {
    sequence: "Esc"
    context: Qt.ApplicationShortcut
    onActivated: {
      if (root.agentRunning && root.agent) root.agent.cancel()
      else if (skillPicker.opened) skillPicker.close()
      else if (chatListPopup.opened) chatListPopup.close()
    }
  }
  Shortcut {
    sequence: "Ctrl+Return"
    context: Qt.ApplicationShortcut
    onActivated: root.sendPrompt()
  }
  Shortcut {
    sequence: "Ctrl+Alt+A"
    context: Qt.ApplicationShortcut
    enabled: root.agent && !root.agentRunning
    onActivated: root.agent.acceptAll()
  }
  Shortcut {
    sequence: "Ctrl+Alt+R"
    context: Qt.ApplicationShortcut
    enabled: root.agent && !root.agentRunning
    onActivated: root.agent.rejectAll()
  }
  readonly property var agent: typeof agentService !== "undefined" ? agentService : null
  property bool agentRunning: agent ? agent.running : false
  property string currentTask: agent ? agent.currentAgent : ""
  property string noticeText: ""
  property bool showReviewDetails: false

  readonly property int changedAgentCount: {
    if (!root.agent)
      return 0
    const runs = root.agent.agentRuns || []
    let count = 0
    for (let i = 0; i < runs.length; ++i)
      if (runs[i].changed)
        ++count
    return count
  }
  readonly property int acceptedAgentCount: {
    if (!root.agent)
      return 0
    const runs = root.agent.agentRuns || []
    let count = 0
    for (let i = 0; i < runs.length; ++i)
      if (runs[i].changed && runs[i].accepted)
        ++count
    return count
  }

  function shortText(value, maxLength) {
    const text = String(value === undefined || value === null ? "" : value)
                   .replace(/\s+/g, " ").trim()
    return text.length > maxLength ? text.slice(0, maxLength - 1) + "…" : text
  }

  readonly property var registry: typeof providerManager !== "undefined"
                                  ? providerManager : null
  readonly property int registryRevision: registry ? registry.revision : 0
  property var availableModels: {
    const rev = root.registryRevision
    const connected = registry ? registry.anyConnected : false
    return connected ? registry.enabledModels() : []
  }
  property string selectedModelKey: {
    const rev = root.registryRevision
    return registry ? registry.defaultModelKey : ""
  }
  property var pickerModels: {
    const query = modelPicker.searchText.trim().toLowerCase()
    const all = root.availableModels
    if (query.length === 0)
      return all
    return all.filter(function(model) {
      return model.label.toLowerCase().indexOf(query) >= 0 ||
             model.modelId.toLowerCase().indexOf(query) >= 0 ||
             model.providerName.toLowerCase().indexOf(query) >= 0
    })
  }
  property string selectedModel: {
    for (let i = 0; i < root.availableModels.length; ++i) {
      if (root.availableModels[i].key === root.selectedModelKey)
        return root.availableModels[i].label
    }
    return root.availableModels.length > 0
      ? root.availableModels[0].label : "Auto"
  }
  readonly property bool aiReady: root.availableModels.length > 0

  readonly property string errorMessage: {
    if (!root.agent)
      return ""
    const connection = root.agent.connectionError || ""
    const generation = root.agent.lastError || ""
    if (connection.length > 0 && generation.length > 0)
      return connection + " — " + generation
    return connection.length > 0 ? connection : generation
  }

  readonly property var transcript: root.agent ? (root.agent.transcript || []) : []
  readonly property var steps: root.agent ? (root.agent.steps || []) : []

  // Text is escaped first, then a small Markdown subset is applied. Qt's rich
  // text engine renders the resulting HTML; links are guarded on activation.
  function escapeHtml(value) {
    return String(value === undefined || value === null ? "" : value)
      .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;").replace(/'/g, "&#39;")
  }

  function inlineMd(value) {
    let out = value
    out = out.replace(/`([^`\n]+)`/g, function (_, code) {
      return "<span style='font-family:monospace;color:" + Theme.accent +
             "'>" + code + "</span>"
    })
    out = out.replace(/\*\*([^*\n]+)\*\*/g, "<b>$1</b>")
    out = out.replace(/(^|[^*])\*([^*\n]+)\*/g, "$1<i>$2</i>")
    out = out.replace(/\[([^\]\n]+)\]\((https?:\/\/[^\s)]+)\)/g,
                      "<a href='$2'>$1</a>")
    return out
  }

  function markdownHtml(text, time) {
    const lines = escapeHtml(text).split(/\r?\n/)
    let html = ""
    let inCode = false
    let list = ""
    function closeList() {
      if (list.length > 0) { html += "</" + list + ">"; list = "" }
    }
    for (let i = 0; i < lines.length; ++i) {
      const line = lines[i]
      const trimmed = line.trim()
      if (/^```/.test(trimmed)) {
        if (inCode) {
          html += "</span></p>"
          inCode = false
        } else {
          closeList()
          html += "<p><span style='font-family:monospace;font-size:11px;color:" +
                  Theme.textMuted + "'>"
          inCode = true
        }
        continue
      }
      if (inCode) { html += line + "<br>"; continue }
      if (trimmed.length === 0) { closeList(); continue }
      const heading = /^(#{1,6})\s+(.*)$/.exec(trimmed)
      if (heading) {
        closeList()
        const size = heading[1].length === 1 ? 16 : heading[1].length === 2 ? 14 : 12
        html += "<p><b><span style='font-size:" + size + "px'>" +
                inlineMd(heading[2]) + "</span></b></p>"
        continue
      }
      const bullet = /^[-*+]\s+(.*)$/.exec(trimmed)
      const numbered = /^\d+[.)]\s+(.*)$/.exec(trimmed)
      if (bullet || numbered) {
        const wanted = bullet ? "ul" : "ol"
        if (list !== wanted) { closeList(); html += "<" + wanted + ">"; list = wanted }
        html += "<li>" + inlineMd((bullet || numbered)[1]) + "</li>"
        continue
      }
      if (/^>\s?/.test(trimmed)) {
        closeList()
        html += "<p><span style='color:" + Theme.textMuted + "'>" +
                inlineMd(trimmed.replace(/^>\s?/, "")) + "</span></p>"
        continue
      }
      closeList()
      html += "<p>" + inlineMd(trimmed) + "</p>"
    }
    if (inCode) html += "</span></p>"
    closeList()
    const stamp = escapeHtml(time)
    if (stamp.length > 0)
      html += "<p><span style='color:" + Theme.textMuted +
              ";font-size:10px'>" + stamp + "</span></p>"
    return html
  }

  // Kept as a compatibility alias for older consumers.
  function answerHtml(text, time) { return markdownHtml(text, time) }

  readonly property string usageText: root.agent && root.agent.liveTokens > 0
    ? root.agent.liveTokens + " tok · $" + root.agent.liveCostUsd.toFixed(3)
    : ""
  readonly property bool contextCompacted: root.agent && root.agent.compacted
  readonly property bool hasPendingChanges: !!root.agent && root.agent.hasPending && !root.agentRunning
  readonly property bool hasLiveTurn: {
    const t = root.transcript
    return t.length > 0 && t[t.length - 1].role === "assistant" &&
           t[t.length - 1].streaming === true
  }
  // Once the running turn carries its own activity blocks, the separate trace
  // would be a duplicate; keep it only as a fallback for hosts that still
  // publish steps without blocks.
  readonly property bool liveTurnHasBlocks: {
    const t = root.transcript
    if (!root.agentRunning || t.length === 0)
      return false
    const last = t[t.length - 1]
    return last.role === "assistant" && last.streaming === true &&
           last.blocks && last.blocks.length > 0
  }
  function revealReview() {
    root.showReviewDetails = true
    if (!reviewCard || !conversation)
      return
    const point = reviewCard.mapToItem(conversation, 0, 0)
    conversation.contentY = Math.max(
      0, conversation.contentY + point.y - conversation.height * 0.45)
  }
  readonly property var permissionRequests: root.agent
    ? root.agent.permissionModel : null
  readonly property var skillList: root.agent ? root.agent.skillModel : null
  function modelCount(value) {
    if (!value) return 0
    if (typeof value.count !== "undefined") return value.count
    if (typeof value.length !== "undefined") return value.length
    return 0
  }
  readonly property int permissionCount: root.agent && typeof root.agent.pendingPermissionCount !== "undefined"
    ? root.agent.pendingPermissionCount : root.modelCount(root.permissionRequests)
  readonly property int subagentCount: root.agent
    ? root.modelCount(root.agent.subagentModel) : 0
  readonly property int skillCount: root.modelCount(root.skillList)
  readonly property var firstPermission: root.agent
    ? (typeof root.agent.firstPermission !== "undefined"
       ? root.agent.firstPermission
       : (root.permissionRequests && root.permissionRequests[0]) || {})
    : ({})

  property string statusText: root.errorMessage.length > 0
    ? root.errorMessage
    : aiReady
      ? (agentRunning
         ? (currentTask.length > 0 ? selectedModel + " • " + currentTask : selectedModel)
         : selectedModel) + (root.usageText ? " · " + root.usageText : "")
      : "Connect a provider in Settings"

  // Empty-state suggestions. Each one sends a real prompt through the same
  // code path as the composer.
  readonly property var suggestions: [
    { icon: "≡", label: "Summarize this page",
      prompt: "Summarize the current document and list the key points." },
    { icon: "文", label: "Translate this page",
      prompt: "Translate the current document into clear English." },
    { icon: "◎", label: "Analyze for insights",
      prompt: "Analyze this document and share the most useful insights." },
    { icon: "✓", label: "Create a task tracker",
      prompt: "Create a task tracker from the current document." }
  ]

  property int elapsedSeconds: 0
  Timer {
    id: elapsedTimer
    interval: 1000
    repeat: true
    onTriggered: ++root.elapsedSeconds
  }

  anchors.fill: parent
  color: Theme.background

  // Shared building blocks for this panel. All metrics come from Theme so the
  // controls stay aligned and adopt every light/dark theme without per-widget
  // style copies.
  component PanelLabel: Text {
    color: Theme.text
    font.pixelSize: Theme.fontSize
    verticalAlignment: Text.AlignVCenter
    wrapMode: Text.NoWrap
    elide: Text.ElideRight
  }

  component PanelCaption: Text {
    color: Theme.textMuted
    font.pixelSize: Theme.fontSizeSmall
    verticalAlignment: Text.AlignVCenter
    elide: Text.ElideRight
  }

  component PanelCard: Rectangle {
    radius: Theme.radiusLg
    color: Theme.surface
    border.width: 1
    border.color: Theme.border
  }

  // Buttons reuse the app-wide SettingsButton style. Only the compact icon
  // variant overrides sizing and label colour.
  component PanelButton: SettingsButton { }

  component PanelIconButton: SettingsButton {
    id: iconButton
    width: Theme.controlHeight
    height: Theme.controlHeight
    leftPadding: 0
    rightPadding: 0
    property color iconColor: Theme.textMuted
    property bool accentFill: false
    property bool dangerFill: false
    property real buttonRadius: Theme.radiusSm
    primary: accentFill
    danger: dangerFill
    textColor: (accentFill || dangerFill) ? Theme.accentText : iconColor
    cornerRadius: buttonRadius
  }

  component PanelField: TextField {
    height: Theme.buttonHeight
    leftPadding: Theme.spaceSm
    rightPadding: Theme.spaceSm
    color: Theme.text
    placeholderTextColor: Theme.textFaint
    font.pixelSize: Theme.fontSize
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    background: Rectangle {
      radius: Theme.radiusMd
      color: Theme.surfaceAlt
      border.width: 1
      border.color: parent.activeFocus ? Theme.accent : Theme.border
    }
  }

  component PanelTextArea: TextArea {
    color: Theme.text
    placeholderTextColor: Theme.textFaint
    font.pixelSize: Theme.fontSize
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    wrapMode: TextEdit.Wrap
    padding: 0
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0
    background: null
  }

  component PanelListRow: Item {
    id: listRow
    property string labelText: ""
    property string badgeText: ""
    property bool selected: false
    property real leadingMargin: Theme.spaceMd
    signal activated()

    height: Theme.rowHeight
    Rectangle {
      anchors.fill: parent
      radius: Theme.radiusMd
      color: listRow.selected ? Theme.tint(Theme.accent, 0.12)
                             : (rowMouse.containsMouse ? Theme.surfaceHover : "transparent")
    }
    PanelLabel {
      anchors {
        left: parent.left
        leftMargin: listRow.leadingMargin
        right: parent.right
        rightMargin: listRow.leadingMargin + 28
        verticalCenter: parent.verticalCenter
      }
      text: listRow.labelText + (listRow.badgeText.length > 0
                                 ? "  (" + listRow.badgeText + ")" : "")
      color: listRow.selected ? Theme.accent : Theme.text
    }
    MouseArea {
      id: rowMouse
      anchors.fill: parent
      hoverEnabled: true
      cursorShape: Qt.PointingHandCursor
      onClicked: listRow.activated()
    }
  }

  // --- HEADER ---
  Rectangle {
    id: headerBar

    anchors {
      top: parent.top
      left: parent.left
      right: parent.right
    }
    height: 52
    color: "transparent"

    Rectangle {
      id: chatTitleButton
      anchors {
        left: parent.left
        leftMargin: Theme.spaceMd
        verticalCenter: parent.verticalCenter
      }
      width: Math.min(190, parent.width - 140)
      height: Theme.controlHeight
      radius: Theme.radiusMd
      color: chatMenuMouse.containsMouse ? Theme.surfaceAlt : "transparent"
      border.width: 1
      border.color: Theme.border

      Text {
        anchors {
          left: parent.left
          leftMargin: Theme.spaceMd - 2
          verticalCenter: parent.verticalCenter
        }
        width: parent.width - 30
        text: root.agent && root.agent.activeSessionTitle
              ? root.agent.activeSessionTitle : "New AI chat"
        color: Theme.text
        font.pixelSize: Theme.fontSize
        font.bold: true
        elide: Text.ElideRight
      }
      Text {
        anchors {
          right: parent.right
          rightMargin: 9
          verticalCenter: parent.verticalCenter
        }
        text: "⌄"
        color: Theme.textMuted
        font.pixelSize: Theme.fontSize + 1
      }
      MouseArea {
        id: chatMenuMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: chatListPopup.open()
      }
      ToolTip.visible: chatMenuMouse.containsMouse
      ToolTip.text: "Switch or start a conversation"
      ToolTip.delay: 500
    }

    PanelCaption {
      id: statusLabel
      anchors {
        right: parent.right
        rightMargin: Theme.spaceMd
        verticalCenter: parent.verticalCenter
      }
      width: Math.max(48, parent.width - chatTitleButton.width - 36)
      text: root.statusText
      color: root.errorMessage.length > 0 ? Theme.danger
             : root.agentRunning ? Theme.accent : Theme.textMuted
      horizontalAlignment: Text.AlignRight
      Behavior on color { ColorAnimation { duration: 200 } }
    }

    PanelCard {
      objectName: "compactionChip"
      visible: root.contextCompacted
      anchors {
        right: parent.right
        rightMargin: Theme.spaceMd + 2
        bottom: parent.bottom
        bottomMargin: Theme.spaceXs
      }
      radius: Theme.radiusSm
      width: 110
      height: 16
      color: Theme.tint(Theme.accent, 0.12)
      Text {
        anchors.centerIn: parent
        text: "◇ context compacted"
        color: Theme.accent
        font.pixelSize: Theme.fontSizeSmall - 1
      }
    }

    Rectangle {
      anchors {
        left: parent.left
        right: parent.right
        bottom: parent.bottom
      }
      height: 1
      color: Theme.border
    }
  }

  // --- CONVERSATION ---
  Flickable {
    id: conversation
    objectName: "conversation"

    anchors {
      top: headerBar.bottom
      left: parent.left
      right: parent.right
      bottom: composer.top
    }
    contentWidth: width
    // Explicit padding above the first turn and below the last one; the
    // composer is a separate surface, so the last message must not touch it.
    contentHeight: conversationColumn.y + conversationColumn.implicitHeight +
                   Theme.spaceLg + Theme.spaceSm
    clip: true
    flickableDirection: Flickable.VerticalFlick
    boundsBehavior: Flickable.StopAtBounds

    Column {
      id: conversationColumn
      objectName: "conversationColumn"
      width: parent.width - (Theme.spaceMd + 2) * 2
      x: Theme.spaceMd + 2
      y: 16
      spacing: Theme.spaceLg

      // Empty state: greeting and tappable suggestions.
      Item {
        objectName: "emptyState"
        width: parent.width
        height: visible ? emptyColumn.implicitHeight : 0
        visible: root.transcript.length === 0 && !root.agentRunning

        Column {
          id: emptyColumn
          anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            topMargin: Math.max(36, conversation.height * 0.22)
          }
          spacing: 8

          PanelCard {
            width: 44
            height: 44
            radius: width / 2
            color: Theme.surfaceAlt
            border.width: 1
            border.color: Theme.border
            Image {
              anchors.centerIn: parent
              width: 30
              height: 30
              source: "qrc:/qt/qml/Templnote/assets/logo.png"
              sourceSize.width: 60
              sourceSize.height: 60
              fillMode: Image.PreserveAspectFit
            }
          }

          Text {
            text: "How can I help you today?"
            color: Theme.text
            font.pixelSize: Theme.fontSizeTitle
            font.bold: true
            topPadding: 10
            bottomPadding: 8
          }

          Repeater {
            model: root.suggestions
            delegate: Item {
              width: conversationColumn.width
              height: 36

              MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.sendPrompt(modelData.prompt)
              }

              Text {
                anchors {
                  left: parent.left
                  verticalCenter: parent.verticalCenter
                }
                width: 26
                text: modelData.icon
                color: Theme.textMuted
                font.pixelSize: Theme.fontSize + 2
                horizontalAlignment: Text.AlignHCenter
              }
              Text {
                anchors {
                  left: parent.left
                  leftMargin: 30
                  right: parent.right
                  verticalCenter: parent.verticalCenter
                }
                text: modelData.label
                color: Theme.text
                font.pixelSize: Theme.fontSize
                elide: Text.ElideRight
              }
            }
          }
        }
      }

      // Conversation turns. A ListView keeps delegates alive while new
      // streaming text and background reports are appended incrementally.
      ListView {
        objectName: "transcriptRepeater"
        model: (root.agent && root.agent.transcriptModel)
               ? root.agent.transcriptModel : (root.transcript || [])
        width: conversationColumn.width
        height: contentHeight
        interactive: false
        clip: true
        reuseItems: true
        delegate: Item {
          id: turnDelegate
          property var turn: (typeof modelData !== "undefined")
                             ? modelData : model
          width: ListView.view.width
          height: turnColumn.implicitHeight
          Accessible.role: Accessible.ListItem
          Accessible.name: (turn.role || "message") + ": " + (turn.text || "")

          Column {
            id: turnColumn
            width: parent.width
            spacing: 6

            Rectangle {
              objectName: "userBubble"
              visible: turn.role === "user"
              width: Math.min(parent.width - Theme.spaceLg * 2,
                              Math.max(80, userText.implicitWidth + Theme.spaceLg * 2))
              height: Math.max(Theme.controlHeight,
                               userText.implicitHeight + Theme.spaceMd + Theme.spaceSm)
              radius: Theme.radiusMd
              color: turn.kind === "steer" ? Theme.tint(Theme.accent, 0.12)
                                           : Theme.surfaceAlt
              border.width: 1
              border.color: Theme.border
              anchors.right: parent.right
              Text {
                id: userText
                anchors {
                  left: parent.left
                  right: parent.right
                  margins: 8
                  verticalCenter: parent.verticalCenter
                }
                text: (turn.kind === "steer" ? "↳ " : "") + (turn.text || "")
                color: Theme.text
                font.pixelSize: Theme.fontSize
                wrapMode: Text.Wrap
              }
            }

            PanelCard {
              objectName: "systemNotice"
              visible: turn.role === "system"
              width: parent.width
              height: noticeTextItem.implicitHeight + 12
              radius: Theme.radiusMd
              color: Theme.tint(Theme.accent, 0.08)
              Text {
                id: noticeTextItem
                anchors { left: parent.left; right: parent.right; margins: 6 }
                text: (turn.kind === "compaction" ? "◇ " :
                       turn.kind === "skill" ? "✦ " : "• ") + (turn.text || "")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
              }
            }

            Item {
              id: assistantRoot
              objectName: "assistantMessage"
              visible: turn.role === "assistant" &&
                       ((turn.text || "").length > 0 ||
                        (turn.blocks && turn.blocks.length > 0))
              width: parent.width
              height: assistantInner.implicitHeight
              property bool feedbackUp: false
              property bool feedbackDown: false

              MouseArea {
                id: answerHover
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.NoButton
              }

              Column {
                id: assistantInner
                width: parent.width
                spacing: 6

                // Durable, collapsible record of the work done for this turn.
                Repeater {
                  objectName: "activityRepeater"
                  model: (turn.blocks && turn.blocks.length) ? turn.blocks : []
                  delegate: Item {
                    id: blockRow
                    property var block: (typeof modelData !== "undefined") ? modelData : model
                    property bool expanded: false
                    property int indent: Math.min(2, (block.depth || 0)) * 14
                    width: assistantInner.width
                    height: blockInner.height

                    Item {
                      id: blockInner
                      anchors {
                        left: parent.left
                        leftMargin: blockRow.indent
                        right: parent.right
                      }
                      height: blockShell.implicitHeight

                      PanelCard {
                        id: blockShell
                        objectName: "activityBlock"
                        width: parent.width
                        implicitHeight: blockContent.implicitHeight + 12
                        function activate() {
                          if (block.description || block.detail)
                            blockRow.expanded = !blockRow.expanded
                        }
                        radius: Theme.radiusMd
                        color: block.kind === "reasoning"
                               ? Theme.surface
                               : (block.status === "error"
                                  ? Theme.tint(Theme.danger, 0.10)
                                  : Theme.tint(Theme.accent, 0.04))
                        border.width: 1
                        border.color: block.status === "error" ? Theme.danger
                                      : (block.status === "running" ? Theme.accent
                                                                    : Theme.border)

                        MouseArea {
                          id: blockMouse
                          objectName: "activityToggle"
                          anchors.fill: parent
                          cursorShape: (block.description || block.detail)
                                       ? Qt.PointingHandCursor : Qt.DefaultCursor
                          onClicked: blockShell.activate()
                        }

                        Column {
                          id: blockContent
                          anchors {
                            left: parent.left
                            right: parent.right
                            top: parent.top
                            margins: 6
                          }
                          spacing: 3

                          Row {
                            id: blockTitleRow
                            width: parent.width
                            spacing: 6
                            Text {
                              objectName: "activityTitle"
                              width: parent.width - 56
                              anchors.verticalCenter: parent.verticalCenter
                              text: (block.icon || "›") + "  " +
                                    (block.title || block.label || "")
                              color: block.status === "error" ? Theme.danger
                                    : (block.status === "running" ? Theme.accent
                                                                  : Theme.text)
                              font.pixelSize: Theme.fontSize - 1
                              font.bold: true
                              elide: Text.ElideRight
                            }
                            Text {
                              anchors.verticalCenter: parent.verticalCenter
                              text: (block.durationMs || 0) > 0
                                    ? ((block.durationMs / 1000).toFixed(1) + "s") : ""
                              color: Theme.textMuted
                              font.pixelSize: Theme.fontSizeSmall
                            }
                            Text {
                              anchors.verticalCenter: parent.verticalCenter
                              text: (block.description || block.detail)
                                    ? (blockRow.expanded ? "⌃" : "⌄") : ""
                              color: Theme.textMuted
                              font.pixelSize: Theme.fontSize - 1
                            }
                          }

                          Text {
                            id: blockDescription
                            objectName: "activityDescription"
                            visible: (block.description || "").length > 0
                            width: parent.width
                            text: block.description || ""
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSizeSmall
                            wrapMode: blockRow.expanded ? Text.Wrap : Text.NoWrap
                            elide: blockRow.expanded ? Text.ElideNone : Text.ElideRight
                            maximumLineCount: blockRow.expanded ? 0 : 1
                          }

                          Text {
                            id: blockDetail
                            objectName: "activityDetail"
                            visible: blockRow.expanded && (block.detail || "").length > 0
                            width: parent.width
                            text: block.detail || ""
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSizeSmall
                            wrapMode: Text.WrapAnywhere
                          }
                        }
                      }
                    }
                  }
                }

                Text {
                  objectName: "promptAnswerText"
                  width: parent.width
                  visible: (turn.text || "").length > 0
                  text: root.markdownHtml(turn.text || "")
                  color: Theme.text
                  font.pixelSize: Theme.fontSize
                  wrapMode: Text.Wrap
                  textFormat: Text.RichText
                  onLinkActivated: function (link) {
                    if (link.indexOf("http://") === 0 || link.indexOf("https://") === 0)
                      Qt.openUrlExternally(link)
                  }
                }

                PanelCaption {
                  objectName: "answerMeta"
                  visible: (turn.text || "").length > 0 &&
                           ((turn.durationMs || 0) > 0 || (turn.time || "").length > 0)
                  text: (turn.durationMs || 0) > 0
                        ? "Worked for " + Math.max(1, Math.round(turn.durationMs / 1000)) + "s"
                        : (turn.time || "")
                  color: Theme.textFaint
                }

                // Approval needed by a tool in this turn, inline with the work.
                PanelCard {
                  objectName: "inlinePermissionBanner"
                  visible: turn.streaming === true && root.permissionCount > 0
                  width: parent.width
                  height: visible ? Theme.buttonHeight + Theme.spaceXs : 0
                  radius: Theme.radiusSm
                  color: Theme.tint(Theme.accent, 0.10)
                  border.width: 1
                  border.color: Theme.accent
                  Row {
                    anchors { left: parent.left; right: parent.right; margins: 6 }
                    spacing: 8
                    Text {
                      width: parent.width - 150
                      anchors.verticalCenter: parent.verticalCenter
                      text: "Needs approval: " + (root.firstPermission.action || "tool")
                      color: Theme.text
                      font.pixelSize: Theme.fontSizeSmall
                      elide: Text.ElideRight
                    }
                    Text {
                      objectName: "inlineAllow"
                      text: "Allow"
                      color: Theme.accent
                      font.pixelSize: Theme.fontSizeSmall
                      MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                          const req = root.firstPermission
                          if (root.agent) root.agent.approveTool(req.id || "", false)
                        }
                      }
                    }
                    Text {
                      text: "Always"
                      color: Theme.accent
                      font.pixelSize: Theme.fontSizeSmall
                      MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                          const req = root.firstPermission
                          if (root.agent) root.agent.approveTool(req.id || "", true)
                        }
                      }
                    }
                    Text {
                      objectName: "inlineDeny"
                      text: "Deny"
                      color: Theme.textMuted
                      font.pixelSize: Theme.fontSizeSmall
                      MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                          const req = root.firstPermission
                          if (root.agent) root.agent.denyTool(req.id || "")
                        }
                      }
                    }
                  }
                }

                Row {
                  opacity: answerHover.containsMouse || root.hasPendingChanges ? 1 : 0
                  Behavior on opacity { OpacityAnimator { duration: 120 } }
                  spacing: 12
                  Text {
                    text: "Copy"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                    MouseArea {
                      anchors.fill: parent
                      cursorShape: Qt.PointingHandCursor
                      onClicked: Clipboard.setText(turn.text || "")
                    }
                  }
                  Text {
                    objectName: "showChangesChip"
                    visible: root.hasPendingChanges
                    text: "Show changes"
                    color: Theme.accent
                    font.pixelSize: Theme.fontSizeSmall
                    function activate() { root.revealReview() }
                    MouseArea {
                      anchors.fill: parent
                      cursorShape: Qt.PointingHandCursor
                      onClicked: parent.activate()
                    }
                  }
                  Text {
                    objectName: "regenerateChip"
                    text: "↻ Regenerate"
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                    function activate() { if (root.agent) root.agent.regenerate() }
                    MouseArea {
                      anchors.fill: parent
                      cursorShape: Qt.PointingHandCursor
                      onClicked: parent.activate()
                    }
                  }
                  Text {
                    objectName: "feedbackUp"
                    text: assistantRoot.feedbackUp ? "✓ Good" : "Good"
                    color: assistantRoot.feedbackUp ? Theme.accent : Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                    function activate() {
                      assistantRoot.feedbackUp = !assistantRoot.feedbackUp
                      assistantRoot.feedbackDown = false
                      if (assistantRoot.feedbackUp && root.agent)
                        root.agent.sendFeedback(true)
                    }
                    MouseArea {
                      anchors.fill: parent
                      cursorShape: Qt.PointingHandCursor
                      onClicked: parent.activate()
                    }
                  }
                  Text {
                    objectName: "feedbackDown"
                    text: assistantRoot.feedbackDown ? "✓ Bad" : "Bad"
                    color: assistantRoot.feedbackDown ? Theme.danger : Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                    function activate() {
                      assistantRoot.feedbackDown = !assistantRoot.feedbackDown
                      assistantRoot.feedbackUp = false
                      if (assistantRoot.feedbackDown && root.agent)
                        root.agent.sendFeedback(false)
                    }
                    MouseArea {
                      anchors.fill: parent
                      cursorShape: Qt.PointingHandCursor
                      onClicked: parent.activate()
                    }
                  }
                }
              }
            }
          }
        }
      }

      // Live run trace (fallback for hosts that do not publish activity blocks).
      Item {
        width: parent.width
        height: visible ? traceColumn.implicitHeight : 0
        visible: root.agentRunning && !root.liveTurnHasBlocks

        Column {
          id: traceColumn
          width: parent.width
          spacing: 4

          Row {
            spacing: 6
            Text {
              text: "Working for " + root.elapsedSeconds + "s"
              color: Theme.accent
              font.pixelSize: Theme.fontSize - 1
              font.bold: true
            }
            Text {
              text: "›"
              color: Theme.textMuted
              font.pixelSize: Theme.fontSize + 1
            }
          }

          ListView {
            objectName: "stepRepeater"
            model: (root.agent && root.agent.stepsModel)
                   ? root.agent.stepsModel : (root.steps || [])
            width: traceColumn.width
            height: Math.min(180, count * 24)
            interactive: false
            clip: true
            reuseItems: true
            delegate: Item {
              id: stepDelegate
              property var stepInfo: (typeof modelData !== "undefined")
                                     ? modelData : model
              property bool expanded: false
              objectName: "agentStep"
              width: ListView.view.width
              height: expanded ? 24 + detailText.implicitHeight + 8 : 24
              MouseArea {
                anchors.fill: parent
                onClicked: if (stepInfo.detail) stepDelegate.expanded = !stepDelegate.expanded
              }
              Rectangle {
                visible: stepInfo.active
                anchors.fill: parent
                radius: Theme.radiusSm
                color: Theme.tint(Theme.accent, 0.12)
              }
              Text {
                anchors {
                  left: parent.left
                  leftMargin: 6
                  top: parent.top
                }
                width: 16
                text: stepInfo.kind === "agent" ? "✦"
                      : stepInfo.kind === "result" ? "✓" : "›"
                color: stepInfo.active ? Theme.accent : Theme.textMuted
                font.pixelSize: Theme.fontSize - 1
              }
              Text {
                anchors {
                  left: parent.left
                  leftMargin: 26
                  right: parent.right
                  top: parent.top
                }
                text: (stepInfo.label || "") + (stepInfo.detail ? "  ▸" : "")
                color: Theme.text
                font.pixelSize: Theme.fontSize - 1
                elide: Text.ElideRight
              }
              Text {
                id: detailText
                visible: stepDelegate.expanded && stepInfo.detail
                anchors {
                  left: parent.left
                  leftMargin: 26
                  right: parent.right
                  top: parent.top
                  topMargin: 22
                }
                text: stepInfo.detail || ""
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WrapAnywhere
              }
            }
          }
          }
        }

      Text {
        id: noticeLabel
        // Shown only while an answer is being generated. The step trace is
        // visible at the same time; once output arrives both disappear.
        visible: root.agentRunning && root.noticeText.length > 0
                 && root.errorMessage.length === 0
        width: parent.width
        text: root.noticeText
        color: Theme.textMuted
        font.pixelSize: Theme.fontSize - 1
        elide: Text.ElideRight
      }

      // --- ERROR BANNER ---
      PanelCard {
        id: errorBanner
        objectName: "promptErrorBanner"
        visible: root.errorMessage.length > 0 && !root.hasLiveTurn
        width: parent.width
        height: Math.min(120, Math.max(Theme.buttonHeight, errorText.implicitHeight + 12))
        color: Theme.surfaceAlt
        border.color: Theme.danger

        ToolTip.text: root.errorMessage
        ToolTip.visible: errorMouse.containsMouse
        ToolTip.delay: 500
        MouseArea {
          id: errorMouse
          anchors.fill: parent
          hoverEnabled: true
          acceptedButtons: Qt.NoButton
        }
        Text {
          id: errorText
          objectName: "promptErrorText"
          anchors {
            left: parent.left
            leftMargin: 8
            top: parent.top
            topMargin: 6
          }
          width: Math.max(120, errorBanner.width - 52)
          text: root.errorMessage
          color: Theme.danger
          font.pixelSize: Theme.fontSize - 1
          wrapMode: Text.Wrap
          textFormat: Text.PlainText
        }
        PanelIconButton {
          id: dismissError
          objectName: "promptErrorDismiss"
          anchors {
            right: parent.right
            rightMargin: Theme.spaceXs
            verticalCenter: parent.verticalCenter
          }
          iconColor: Theme.danger
          text: "×"
          onClicked: {
            if (root.agent)
              root.agent.clearErrors()
          }
        }
      }

      // Permission requests: approval is non-blocking; allowing saves a rule
      // and the model's repair round retries the call.
      PanelCard {
        objectName: "permissionBanner"
        visible: root.permissionCount > 0 && !root.hasLiveTurn
        width: parent.width
        height: visible ? Theme.buttonHeight + Theme.spaceMd * 2 : 0
        color: Theme.tint(Theme.accent, 0.10)
        border.color: Theme.accent
        Row {
          anchors {
            left: parent.left
            right: parent.right
            margins: Theme.spaceMd
          }
          spacing: Theme.spaceXs
          PanelCaption {
            width: Math.max(40, parent.width - 208)
            anchors.verticalCenter: parent.verticalCenter
            text: "Tool approval needed: " +
                  ((root.firstPermission.action || "tool") + " → " +
                   (root.firstPermission.resource || "*"))
            color: Theme.text
            font.pixelSize: Theme.fontSize - 1
          }
          PanelButton {
            width: 64
            text: "Allow"
            onClicked: {
              const req = root.firstPermission
              if (root.agent) root.agent.approveTool(req.id || "", false)
            }
          }
          PanelButton {
            objectName: "allowAlwaysButton"
            width: 64
            text: "Always"
            onClicked: {
              const req = root.firstPermission
              if (root.agent) root.agent.approveTool(req.id || "", true)
            }
          }
          PanelButton {
            width: 64
            text: "Deny"
            onClicked: {
              const req = root.firstPermission
              if (root.agent) root.agent.denyTool(req.id || "")
            }
          }
        }
      }

      // Background subagents report here; selecting one switches sessions.
      Repeater {
        objectName: "subagentList"
        model: root.subagentCount > 0 && root.agent
               ? root.agent.subagentModel : []
        Item {
          objectName: "subagentCard"
          property var info: (typeof modelData !== "undefined") ? modelData : model
          width: conversationColumn.width
          height: 30
          PanelCard {
            anchors.fill: parent
            radius: Theme.radiusSm
            color: Theme.surfaceAlt
          }
          Text {
            anchors { left: parent.left; leftMargin: Theme.spaceSm; verticalCenter: parent.verticalCenter }
            text: (info.running ? "◌ " : "✓ ") + (info.title || "Subagent")
            color: info.running ? Theme.accent : Theme.text
            font.pixelSize: Theme.fontSize - 1
          }
          MouseArea {
            anchors.fill: parent
            onClicked: {
              if (root.agent) root.agent.switchSession(info.id)
            }
          }
        }
      }

      // --- RESULT / REVIEW CARD ---
      PanelCard {
        id: reviewCard
        objectName: "reviewCard"
        visible: root.agent && root.agent.hasPending && !root.agentRunning
        width: parent.width
        height: 42 + (root.showReviewDetails ? detailColumn.implicitHeight + 44 : 0)
        color: Theme.surfaceAlt

        Column {
          anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            margins: 10
          }
          spacing: 6

          Row {
            width: parent.width
            spacing: Theme.spaceSm
            PanelLabel {
              id: reviewTitle
              anchors.verticalCenter: parent.verticalCenter
              text: "AI changes ready"
              font.bold: true
              elide: Text.ElideNone
            }
            PanelCaption {
              anchors.verticalCenter: parent.verticalCenter
              width: Math.max(60, parent.width - reviewTitle.implicitWidth - Theme.spaceSm)
              text: root.changedAgentCount + " agent(s) · "
                    + (root.agent ? (root.agent.pendingDiff || []).length : 0)
                    + " block(s)"
            }
          }

          Row {
            width: parent.width
            spacing: Theme.spaceSm
            PanelButton {
              id: reviewToggle
              text: root.showReviewDetails ? "Hide changes" : "Show changes"
              onClicked: root.showReviewDetails = !root.showReviewDetails
            }
            PanelButton {
              id: undoChanges
              text: "Undo"
              onClicked: {
                if (root.agent)
                  root.agent.reject()
              }
            }
          }

          Column {
            id: detailColumn
            visible: root.showReviewDetails
            width: parent.width
            spacing: 8

            Repeater {
              model: root.agent ? root.agent.agentRuns : []
              delegate: Item {
                width: detailColumn.width
                height: 46

                CheckBox {
                  id: acceptBox
                  anchors {
                    left: parent.left
                    verticalCenter: parent.verticalCenter
                  }
                  checked: modelData.accepted
                  enabled: modelData.changed && !root.agentRunning
                  onToggled: root.agent.setAgentAccepted(index, acceptBox.checked)
                }
                Text {
                  anchors {
                    left: acceptBox.right
                    leftMargin: 8
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                  }
                  text: modelData.label + (modelData.changed
                        ? "  •  " + modelData.edits + " edit(s)"
                        : "  •  no changes")
                  color: modelData.changed ? Theme.text : Theme.textMuted
                  font.pixelSize: Theme.fontSize - 1
                  elide: Text.ElideRight
                }
              }
            }

            Row {
              spacing: Theme.spaceSm
              PanelButton {
                objectName: "acceptAllButton"
                text: "Accept all"
                enabled: root.agent && !root.agentRunning
                onClicked: if (root.agent) root.agent.acceptAll()
              }
              PanelButton {
                objectName: "rejectAllButton"
                text: "Reject all"
                enabled: root.agent && !root.agentRunning
                onClicked: if (root.agent) root.agent.rejectAll()
              }
            }

            PanelButton {
              id: acceptButton
              objectName: "acceptButton"
              width: parent.width
              primary: true
              text: "Accept " + root.acceptedAgentCount + " agent(s)"
              enabled: root.agent && !root.agentRunning && root.acceptedAgentCount > 0
              onClicked: {
                if (root.agent.commit())
                  root.noticeText = ""
              }
            }
            PanelButton {
              id: rejectButton
              objectName: "rejectButton"
              width: parent.width
              text: root.agentRunning ? "Cancel" : "Reject all"
              enabled: root.agent
              onClicked: {
                if (root.agentRunning)
                  root.agent.cancel()
                else
                  root.agent.reject()
              }
            }
          }
        }
      }
    }
      }

  // --- COMPOSER ---
  PanelCard {
    id: composer

    anchors {
      left: parent.left
      right: parent.right
      bottom: parent.bottom
      leftMargin: Theme.spaceMd
      rightMargin: Theme.spaceMd
      bottomMargin: Theme.spaceMd
    }
    height: Math.min(168, 68 + promptInput.contentHeight)
    color: Theme.surfaceAlt

    PanelTextArea {
      id: promptInput
      objectName: "promptInput"
      anchors {
        top: parent.top
        left: parent.left
        right: parent.right
        topMargin: Theme.spaceSm + 2
        leftMargin: Theme.spaceMd
        rightMargin: Theme.spaceMd
      }
      height: Math.max(Theme.controlHeight, contentHeight)
      placeholderText: "Ask Templnote anything…"

      Keys.onReturnPressed: function(event) {
        if (event.modifiers & Qt.ShiftModifier) {
          event.accepted = false
        } else {
          event.accepted = true
          root.sendPrompt()
        }
      }
      Keys.onEscapePressed: function(event) {
        if (root.agentRunning && root.agent) {
          event.accepted = true
          root.agent.cancel()
        }
      }
      Accessible.role: Accessible.EditableText
      Accessible.name: "AI prompt input"
    }

    Item {
      id: composerToolbar
      anchors {
        left: parent.left
        right: parent.right
        bottom: parent.bottom
        leftMargin: 8
        rightMargin: 8
        bottomMargin: 8
      }
      height: 28

      Row {
        anchors {
          left: parent.left
          verticalCenter: parent.verticalCenter
        }
        spacing: 2

        PanelIconButton {
          id: addButton
          text: "+"
          onClicked: {
            const next = root.suggestions[(addButton.userData + 1) % root.suggestions.length]
            addButton.userData = (addButton.userData + 1) % root.suggestions.length
            promptInput.text = next.prompt
            promptInput.forceActiveFocus()
          }
          property int userData: -1
        }

        Text {
          anchors.verticalCenter: parent.verticalCenter
          text: "◈"
          color: Theme.textMuted
          font.pixelSize: Theme.fontSize
          ToolTip.visible: privacyMouse.containsMouse
          ToolTip.text: "API keys stay on this device"
          ToolTip.delay: 500
          MouseArea {
            id: privacyMouse
            anchors.fill: parent
            hoverEnabled: true
          }
        }
      }

      Row {
        anchors {
          right: parent.right
          verticalCenter: parent.verticalCenter
        }
        spacing: 4

        PanelIconButton {
          id: modelButton
          objectName: "modelPickerButton"
          width: Math.max(Theme.controlHeight, Math.min(130, root.width * 0.34))
          text: (root.selectedModelKey.length > 0 ? root.selectedModel : "Auto") + "  ⌄"
          onClicked: {
            modelPicker.searchText = ""
            modelPicker.levelModelKey = ""
            modelPicker.activeModelKey = root.selectedModelKey
            modelPicker.open()
          }
        }

        PanelIconButton {
          id: skillsButton
          objectName: "skillsButton"
          text: "✦"
          ToolTip.visible: skillsButton.hovered
          ToolTip.text: "Load a skill"
          ToolTip.delay: 500
          onClicked: skillPicker.open()
        }

        PanelIconButton {
          id: sendButton
          objectName: "sendButton"
          accentFill: !root.agentRunning
          dangerFill: root.agentRunning
          buttonRadius: width / 2
          onClicked: {
            if (root.agentRunning) {
              const text = promptInput.text.trim()
              if (text.length > 0 && root.agent &&
                  root.agent.steerCurrent(text)) {
                promptInput.text = ""
                root.noticeText = "Steering the running agent…"
              } else if (text.length === 0 && root.agent) {
                // Only an empty composer explicitly cancels a running agent;
                // a failed steer must never discard the run.
                root.agent.cancel()
              } else if (root.agent) {
                root.noticeText = "Could not steer the running agent."
              }
            } else {
              root.sendPrompt()
            }
          }
          Item {
            anchors.centerIn: parent
            width: 12
            height: 12
            visible: !root.agentRunning
            Image {
              anchors.centerIn: parent
              width: 12
              height: 12
              source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_send_16_regular.svg"
            }
          }
          Rectangle {
            anchors.centerIn: parent
            width: root.agentRunning ? 10 : 0
            height: root.agentRunning ? 10 : 0
            radius: 2
            color: "white"
            visible: root.agentRunning
          }
        }
      }
    }
  }


  // --- MODEL PICKER ---
  // Popup replacement for the old inline Menu: searchable model list with a
  // per-model thinking-level submenu and capability details.
  Popup {
    id: modelPicker
    objectName: "modelPicker"
    width: Math.max(240, root.width - 24)
    height: Math.min(420, Math.max(240, root.height - 120))
    x: (root.width - width) / 2
    y: Math.max(8, (root.height - height) / 2)
    padding: 0
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property string searchText: ""
    property string activeModelKey: ""
    property string levelModelKey: ""
    readonly property var activeDetails: registry && modelPicker.activeModelKey.length > 0
      ? registry.modelDetails(modelPicker.activeModelKey) : ({})

    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.45) }

    background: PanelCard { }

    contentItem: Item {
      id: pickerContent
      anchors.fill: parent
      clip: true

      Column {
        id: pickerColumn
        anchors.fill: parent
        spacing: 0

        Rectangle {
          width: parent.width
          height: 44
          color: "transparent"

          PanelField {
            id: pickerSearch
            objectName: "modelPickerSearch"
            anchors {
              left: parent.left
              leftMargin: Theme.spaceMd
              right: parent.right
              rightMargin: Theme.spaceMd
              verticalCenter: parent.verticalCenter
            }
            leftPadding: 28
            placeholderText: "Search models"
            onTextChanged: modelPicker.searchText = text
          }

          PanelCaption {
            anchors {
              left: pickerSearch.left
              leftMargin: Theme.spaceSm
              verticalCenter: pickerSearch.verticalCenter
            }
            text: "\u2315"
            font.pixelSize: Theme.fontSizeTitle
          }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.border }

        ListView {
          id: pickerList
          objectName: "modelPickerList"
          width: parent.width
          height: pickerColumn.height - 44 - 1 - 64
          clip: true
          model: root.pickerModels

          delegate: Item {
            id: modelRow
            width: pickerList.width
            height: 38

            MouseArea {
              anchors.fill: parent
              hoverEnabled: true
              onEntered: modelPicker.activeModelKey = modelData.key
              onClicked: {
                modelPicker.activeModelKey = modelData.key
                modelPicker.levelModelKey = modelData.key
                if (registry)
                  registry.defaultModelKey = modelData.key
                if (pluginContext && typeof pluginContext.setSetting === "function")
                  pluginContext.setSetting("defaultModel", modelData.key)
              }
            }

            Text {
              anchors {
                left: parent.left
                leftMargin: 12
                verticalCenter: parent.verticalCenter
              }
              width: 14
              text: modelData.key === root.selectedModelKey ? "\u2713" : ""
              color: Theme.accent
              font.pixelSize: Theme.fontSize + 1
            }

            Column {
              anchors {
                left: parent.left
                leftMargin: 32
                right: levelButton.left
                rightMargin: 8
                verticalCenter: parent.verticalCenter
              }
              spacing: 1
              Text {
                width: parent.width
                text: modelData.label
                color: modelData.key === root.selectedModelKey
                      ? Theme.accent : Theme.text
                font.pixelSize: Theme.fontSize
                elide: Text.ElideRight
              }
              Text {
                width: parent.width
                text: modelData.providerName
                color: Theme.textFaint
                font.pixelSize: Theme.fontSizeSmall
                elide: Text.ElideRight
              }
            }

            PanelIconButton {
              id: levelButton
              anchors {
                right: parent.right
                rightMargin: Theme.spaceMd
                verticalCenter: parent.verticalCenter
              }
              width: 22
              text: "\u203a"
              enabled: registry && registry.thinkingLevels(modelData.key).length > 1
              iconColor: Theme.textMuted
              onClicked: {
                modelPicker.activeModelKey = modelData.key
                modelPicker.levelModelKey = modelData.key
              }
            }
          }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.border }

        Rectangle {
          width: parent.width
          height: 64
          color: Theme.surfaceAlt

          Column {
            anchors {
              left: parent.left
              leftMargin: 12
              right: parent.right
              rightMargin: 12
              verticalCenter: parent.verticalCenter
            }
            spacing: 2

            Text {
              width: parent.width
              text: {
                const details = modelPicker.activeDetails
                const provider = details.providerName || ""
                return provider.length > 0
                      ? provider + " \u2022 " + (details.context || "")
                      : (details.label || "")
              }
              color: Theme.text
              font.pixelSize: Theme.fontSize - 1
              elide: Text.ElideRight
            }
            Text {
              width: parent.width
              text: modelPicker.activeDetails.modalities || ""
              color: Theme.textMuted
              font.pixelSize: Theme.fontSizeSmall
              elide: Text.ElideRight
            }
          }
        }
      }

      // Thinking-level submenu, overlays the right side of the list.
      PanelCard {
        id: levelOverlay
        objectName: "modelLevelOverlay"
        visible: modelPicker.levelModelKey.length > 0
        anchors {
          top: parent.top
          topMargin: 45
          right: parent.right
          bottom: parent.bottom
        }
        width: 184
        color: Theme.surfaceAlt

        Column {
          anchors.fill: parent
          anchors.margins: 8
          spacing: 2

          Text {
            width: parent.width
            text: "Thinking level"
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize - 1
            font.bold: true
          }

          Repeater {
            model: registry && modelPicker.levelModelKey.length > 0
                   ? registry.thinkingLevels(modelPicker.levelModelKey) : []
            delegate: Item {
              objectName: "modelLevelOption"
              width: levelOverlay.width - 16
              height: 26

              MouseArea {
                anchors.fill: parent
                onClicked: {
                  if (registry) {
                    const ref = modelPicker.levelModelKey.split("|")
                    registry.setModelThinkingLevel(ref[0], ref[1],
                                                   modelData.value)
                  }
                  modelPicker.levelModelKey = ""
                }
              }

              Text {
                anchors {
                  left: parent.left
                  leftMargin: 4
                  verticalCenter: parent.verticalCenter
                }
                text: modelData.label
                color: Theme.text
                font.pixelSize: Theme.fontSize - 1
              }

              Text {
                anchors {
                  right: parent.right
                  rightMargin: 4
                  verticalCenter: parent.verticalCenter
                }
                text: {
                  const ref = modelPicker.levelModelKey.split("|")
                  return registry &&
                         registry.modelThinkingLevel(ref[0], ref[1]) === modelData.value
                         ? "\u2713" : ""
                }
                color: Theme.accent
                font.pixelSize: Theme.fontSize
              }
            }
          }
        }
      }
    }
  }


  Popup {
    id: skillPicker
    objectName: "skillPicker"
    width: Math.min(320, root.width - Theme.spaceLg)
    height: Math.min(360, 60 + root.skillCount * Theme.rowHeight)
    x: Theme.spaceMd
    y: Math.max(20, composer.y - height - Theme.spaceSm)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.45) }
    background: PanelCard { }
    contentItem: Column {
      anchors.fill: parent
      anchors.margins: Theme.spaceMd
      spacing: Theme.spaceXs

      PanelLabel {
        width: parent.width
        text: "Skills"
        font.bold: true
      }
      Repeater {
        id: skillListView
        model: root.skillList || []
        PanelListRow {
          objectName: "skillItem"
          property var info: (typeof modelData !== "undefined") ? modelData : model
          width: parent ? parent.width : 0
          labelText: info.name || info.id
          onActivated: {
            if (root.agent && root.agent.loadSkill(info.id))
              root.noticeText = "Loaded skill: " + (info.name || info.id)
            else
              root.noticeText = "Could not load that skill."
            skillPicker.close()
          }
        }
      }
    }
  }

  // Conversation switcher: chats keep running in the background, and the
  // header shows whichever one is currently selected.
  Popup {
    id: chatListPopup
    objectName: "chatListPopup"
    width: Math.min(360, root.width - Theme.spaceLg)
    // Height follows the row count so the card never leaves a dead empty area.
    height: Math.min(root.height - 24,
                     140 + Math.min(320,
                                    (sessionList ? sessionList.count : 0) *
                                    Theme.rowHeight))
    x: root.width - width - Theme.spaceMd
    y: 44
    padding: 0
    modal: true
    focus: true
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.45) }
    background: PanelCard { }

    contentItem: Column {
      anchors {
        fill: parent
        margins: Theme.spaceMd
      }
      spacing: Theme.spaceXs
      clip: true

      // Card header.
      Item {
        width: parent.width
        height: Theme.controlHeight
        PanelLabel {
          anchors {
            left: parent.left
            verticalCenter: parent.verticalCenter
          }
          text: "Conversations"
          font.bold: true
        }
        PanelIconButton {
          anchors {
            right: parent.right
            verticalCenter: parent.verticalCenter
          }
          text: "✕"
          onClicked: chatListPopup.close()
        }
      }

      // Create a named conversation directly from the popup.
      Row {
        width: parent.width
        height: Theme.buttonHeight
        spacing: Theme.spaceSm

        PanelField {
          id: newChatField
          objectName: "newChatField"
          width: parent.width - 80 - Theme.spaceSm
          placeholderText: "New conversation name"
          Keys.onReturnPressed: createChat.activate()
        }
        PanelButton {
          id: createChat
          objectName: "createChatButton"
          width: 80
          text: "Create"
          enabled: root.agent && newChatField.text.trim().length > 0
          function activate() {
            if (!root.agent)
              return
            const name = newChatField.text.trim()
            if (name.length > 0)
              root.agent.createSession(name)
            newChatField.text = ""
            chatListPopup.close()
            root.noticeText = ""
          }
          onClicked: activate()
        }
      }

      // Rename the conversation that is currently open.
      Row {
        width: parent.width
        height: Theme.buttonHeight
        spacing: Theme.spaceSm

        PanelField {
          id: renameField
          objectName: "renameField"
          width: parent.width - 80 - Theme.spaceSm
          placeholderText: "Rename current chat"
          text: root.agent ? root.agent.activeSessionTitle : ""
          Keys.onReturnPressed: renameChat.activate()
        }
        PanelButton {
          id: renameChat
          width: 80
          text: "Rename"
          enabled: root.agent && renameField.text.trim().length > 0
          function activate() {
            if (!root.agent)
              return
            root.agent.renameSession(root.agent.activeSessionId,
                                     renameField.text.trim())
            renameField.text = ""
          }
          onClicked: activate()
        }
      }

      Rectangle {
        width: parent.width
        height: 1
        color: Theme.border
      }

      ListView {
        id: sessionList
        width: parent.width
        height: Math.min(320, count * Theme.rowHeight)
        model: (root.agent && root.agent.sessionsModel)
               ? root.agent.sessionsModel : ((root.agent && root.agent.sessions) || [])
        clip: true
        delegate: Item {
          id: sessionRow
          property var sessionInfo: (typeof modelData !== "undefined")
                                    ? modelData : model
          width: sessionList.width
          height: Theme.rowHeight

          PanelListRow {
            anchors.fill: parent
            leadingMargin: Theme.spaceSm
            selected: sessionInfo.id === root.agent.activeSessionId
            labelText: (sessionInfo.running ? "◌ " : "") +
                       (sessionInfo.title || "Conversation")
            badgeText: sessionInfo.unread ? String(sessionInfo.unread) : ""
            onActivated: {
              root.agent.switchSession(sessionInfo.id)
              chatListPopup.close()
              conversation.contentY = 0
            }
          }
          PanelIconButton {
            objectName: "removeSessionButton"
            anchors {
              right: parent.right
              rightMargin: Theme.spaceXs
              verticalCenter: parent.verticalCenter
            }
            text: "✕"
            onClicked: {
              root.agent.removeSession(sessionRow.sessionInfo.id)
              chatListPopup.close()
            }
          }
        }
      }
    }
  }

  Connections {
    target: typeof agentService !== "undefined" ? agentService : null

    function onPromptStarted() {
      root.elapsedSeconds = 0
      elapsedTimer.start()
      root.showReviewDetails = false
      conversation.contentY = 0
    }

    function onFinishedChanged() {
      elapsedTimer.stop()
      // The generating status belongs to the run, not to the finished answer.
      root.noticeText = ""
    }
  }

  function switchFirstSubagent() {
    const list = root.agent ? root.agent.subagentModel : null
    const item = list && typeof list.row === "function" ? list.row(0)
                 : (list ? list[0] : null)
    if (!item || !root.agent) return false
    return root.agent.switchSession(item.id)
  }

  function loadFirstSkill() {
    const list = root.skillList
    const item = list && typeof list.row === "function" ? list.row(0)
                 : (list ? list[0] : null)
    if (!item || !root.agent) return false
    const ok = root.agent.loadSkill(item.id)
    root.noticeText = ok ? "Loaded skill: " + (item.name || item.id)
                         : "Could not load that skill."
    return ok
  }

  // Shared by the composer, Enter key and the empty-state suggestions.
  function sendPrompt(text) {
    const value = String(text === undefined ? promptInput.text : text).trim()
    if (value.length === 0)
      return
    if (!root.aiReady) {
      noticeText = "Connect a provider in Settings → AI Providers, then choose a model."
      return
    }
    if (!root.agent) {
      noticeText = "AI service is unavailable in this build."
      return
    }
    if (root.agent.running) {
      noticeText = "An agent task is already running."
      return
    }
    root.agent.sendPrompt(value)
    if (text === undefined)
      promptInput.text = ""
    noticeText = "Running " + selectedModel + "…"
  }
}
