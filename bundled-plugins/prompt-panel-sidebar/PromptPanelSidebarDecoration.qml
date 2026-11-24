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
      anchors {
        left: parent.left
        leftMargin: 12
        verticalCenter: parent.verticalCenter
      }
      width: Math.min(190, parent.width - 120)
      height: 28
      radius: 8
      color: chatMenuMouse.containsMouse ? Theme.surfaceAlt : "transparent"
      border.width: 1
      border.color: Theme.border

      Text {
        anchors {
          left: parent.left
          leftMargin: 10
          verticalCenter: parent.verticalCenter
        }
        text: root.agent && root.agent.activeSessionTitle
              ? root.agent.activeSessionTitle : "New AI chat"
        color: Theme.text
        font.pixelSize: 12
        font.bold: true
        elide: Text.ElideRight
        width: parent.width - 32
      }
      Text {
        anchors {
          right: parent.right
          rightMargin: 9
          verticalCenter: parent.verticalCenter
        }
        text: "⌄"
        color: Theme.textMuted
        font.pixelSize: 13
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

    Text {
      anchors {
        right: parent.right
        rightMargin: 14
        verticalCenter: parent.verticalCenter
      }
      width: Math.min(150, parent.width - 210)
      text: root.statusText
      color: root.errorMessage.length > 0 ? Theme.danger
             : root.agentRunning ? Theme.accent : Theme.textMuted
      font.pixelSize: 10
      elide: Text.ElideRight
      horizontalAlignment: Text.AlignRight
      Behavior on color { ColorAnimation { duration: 200 } }
    }

    Rectangle {
      objectName: "compactionChip"
      visible: root.contextCompacted
      anchors {
        right: parent.right
        rightMargin: 14
        bottom: parent.bottom
        bottomMargin: 4
      }
      radius: 6
      width: 110
      height: 16
      color: Theme.tint(Theme.accent, 0.12)
      Text {
        anchors.centerIn: parent
        text: "◇ context compacted"
        color: Theme.accent
        font.pixelSize: 9
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
    contentHeight: conversationColumn.y + conversationColumn.implicitHeight + 24
    clip: true
    flickableDirection: Flickable.VerticalFlick
    boundsBehavior: Flickable.StopAtBounds

    Column {
      id: conversationColumn
      objectName: "conversationColumn"
      width: parent.width - 28
      x: 14
      y: 16
      spacing: 16

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

          Rectangle {
            width: 44
            height: 44
            radius: 22
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
            font.pixelSize: 16
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
                font.pixelSize: 14
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
                font.pixelSize: 12
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
              width: Math.min(parent.width - 36,
                              Math.max(80, userText.implicitWidth + 26))
              height: Math.max(30, userText.implicitHeight + 16)
              radius: 12
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
                font.pixelSize: 12
                wrapMode: Text.Wrap
              }
            }

            Rectangle {
              objectName: "systemNotice"
              visible: turn.role === "system"
              width: parent.width
              height: noticeTextItem.implicitHeight + 12
              radius: 8
              color: Theme.tint(Theme.accent, 0.08)
              Text {
                id: noticeTextItem
                anchors { left: parent.left; right: parent.right; margins: 6 }
                text: (turn.kind === "compaction" ? "◇ " :
                       turn.kind === "skill" ? "✦ " : "• ") + (turn.text || "")
                color: Theme.textMuted
                font.pixelSize: 10
                wrapMode: Text.Wrap
              }
            }

            Item {
              id: assistantRoot
              objectName: "assistantMessage"
              visible: turn.role === "assistant"
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
                    height: blockInner.implicitHeight

                    Item {
                      id: blockInner
                      anchors {
                        left: parent.left
                        leftMargin: blockRow.indent
                        right: parent.right
                      }
                      height: blockShell.implicitHeight

                      Rectangle {
                        id: blockShell
                        objectName: "activityBlock"
                        width: parent.width
                        implicitHeight: blockContent.implicitHeight + 12
                        function activate() {
                          if (block.description || block.detail)
                            blockRow.expanded = !blockRow.expanded
                        }
                        radius: 8
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
                              font.pixelSize: 11
                              font.bold: true
                              elide: Text.ElideRight
                            }
                            Text {
                              anchors.verticalCenter: parent.verticalCenter
                              text: (block.durationMs || 0) > 0
                                    ? ((block.durationMs / 1000).toFixed(1) + "s") : ""
                              color: Theme.textMuted
                              font.pixelSize: 10
                            }
                            Text {
                              anchors.verticalCenter: parent.verticalCenter
                              text: (block.description || block.detail)
                                    ? (blockRow.expanded ? "⌃" : "⌄") : ""
                              color: Theme.textMuted
                              font.pixelSize: 11
                            }
                          }

                          Text {
                            id: blockDescription
                            objectName: "activityDescription"
                            visible: (block.description || "").length > 0
                            width: parent.width
                            text: block.description || ""
                            color: Theme.textMuted
                            font.pixelSize: 10
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
                            font.pixelSize: 10
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
                  font.pixelSize: 12
                  wrapMode: Text.Wrap
                  textFormat: Text.RichText
                  onLinkActivated: function (link) {
                    if (link.indexOf("http://") === 0 || link.indexOf("https://") === 0)
                      Qt.openUrlExternally(link)
                  }
                }

                Text {
                  objectName: "answerMeta"
                  visible: (turn.durationMs || 0) > 0 || (turn.time || "").length > 0
                  text: (turn.durationMs || 0) > 0
                        ? "Worked for " + Math.max(1, Math.round(turn.durationMs / 1000)) + "s"
                        : (turn.time || "")
                  color: Theme.textFaint
                  font.pixelSize: 10
                }

                // Approval needed by a tool in this turn, inline with the work.
                Rectangle {
                  objectName: "inlinePermissionBanner"
                  visible: turn.streaming === true && root.permissionCount > 0
                  width: parent.width
                  height: visible ? 34 : 0
                  radius: 6
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
                      font.pixelSize: 10
                      elide: Text.ElideRight
                    }
                    Text {
                      objectName: "inlineAllow"
                      text: "Allow"
                      color: Theme.accent
                      font.pixelSize: 10
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
                      font.pixelSize: 10
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
                      font.pixelSize: 10
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
                    font.pixelSize: 10
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
                    font.pixelSize: 10
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
                    font.pixelSize: 10
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
                    font.pixelSize: 10
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
                    font.pixelSize: 10
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
              font.pixelSize: 11
              font.bold: true
            }
            Text {
              text: "›"
              color: Theme.textMuted
              font.pixelSize: 13
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
                radius: 6
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
                font.pixelSize: 11
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
                font.pixelSize: 11
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
                font.pixelSize: 10
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
        font.pixelSize: 11
        elide: Text.ElideRight
      }

      // --- ERROR BANNER ---
      Rectangle {
        id: errorBanner
        objectName: "promptErrorBanner"
        visible: root.errorMessage.length > 0 && !root.hasLiveTurn
        width: parent.width
        height: Math.min(120, Math.max(32, errorText.implicitHeight + 12))
        radius: 8
        color: Theme.surfaceAlt
        border.width: 1
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
          font.pixelSize: 11
          wrapMode: Text.Wrap
          textFormat: Text.PlainText
        }
        Button {
          id: dismissError
          objectName: "promptErrorDismiss"
          anchors {
            right: parent.right
            rightMargin: 4
            verticalCenter: parent.verticalCenter
          }
          width: 22
          height: 22
          padding: 0
          text: "×"
          background: Rectangle { color: "transparent" }
          contentItem: Text {
            text: parent.text
            color: Theme.danger
            font.pixelSize: 15
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
          }
          onClicked: {
            if (root.agent)
              root.agent.clearErrors()
          }
        }
      }

      // Permission requests: approval is non-blocking; allowing saves a rule
      // and the model's repair round retries the call.
      Rectangle {
        objectName: "permissionBanner"
        visible: root.permissionCount > 0 && !root.hasLiveTurn
        width: parent.width
        height: visible ? 46 : 0
        radius: 8
        color: Theme.tint(Theme.accent, 0.10)
        border.width: 1
        border.color: Theme.accent
        Row {
          anchors { left: parent.left; right: parent.right; margins: 8 }
          spacing: 8
          Text {
            width: parent.width - 190
            anchors.verticalCenter: parent.verticalCenter
            text: "Tool approval needed: " +
                  ((root.firstPermission.action || "tool") + " → " +
                   (root.firstPermission.resource || "*"))
            color: Theme.text
            font.pixelSize: 11
            elide: Text.ElideRight
          }
          Button {
            text: "Allow"
            onClicked: {
              const req = root.firstPermission
              if (root.agent) root.agent.approveTool(req.id || "", false)
            }
          }
          Button {
            objectName: "allowAlwaysButton"
            text: "Always"
            onClicked: {
              const req = root.firstPermission
              if (root.agent) root.agent.approveTool(req.id || "", true)
            }
          }
          Button {
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
          Rectangle {
            anchors.fill: parent
            radius: 6
            color: Theme.surfaceAlt
            border.width: 1
            border.color: Theme.border
          }
          Text {
            anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
            text: (info.running ? "◌ " : "✓ ") + (info.title || "Subagent")
            color: info.running ? Theme.accent : Theme.text
            font.pixelSize: 11
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
      Rectangle {
        id: reviewCard
        objectName: "reviewCard"
        visible: root.agent && root.agent.hasPending && !root.agentRunning
        width: parent.width
        height: 42 + (root.showReviewDetails ? detailColumn.implicitHeight + 44 : 0)
        radius: 10
        color: Theme.surfaceAlt
        border.width: 1
        border.color: Theme.border

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
            spacing: 8
            Text {
              anchors.verticalCenter: parent.verticalCenter
              text: "AI changes ready"
              color: Theme.text
              font.pixelSize: 12
              font.bold: true
            }
            Text {
              anchors.verticalCenter: parent.verticalCenter
              text: root.changedAgentCount + " agent(s) · "
                    + (root.agent ? (root.agent.pendingDiff || []).length : 0)
                    + " block(s)"
              color: Theme.textMuted
              font.pixelSize: 10
              elide: Text.ElideRight
            }
          }

          Row {
            width: parent.width
            spacing: 8
            Button {
              id: reviewToggle
              text: root.showReviewDetails ? "Hide changes" : "Show changes"
              onClicked: root.showReviewDetails = !root.showReviewDetails
              background: Rectangle {
                radius: 6
                color: parent.hovered ? Theme.surfaceHover : Theme.surface
                border.width: 1
                border.color: Theme.border
              }
              contentItem: Text {
                text: parent.text
                color: Theme.text
                font.pixelSize: 11
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
              }
            }
            Button {
              id: undoChanges
              text: "Undo"
              onClicked: {
                if (root.agent)
                  root.agent.reject()
              }
              background: Rectangle { color: "transparent" }
              contentItem: Text {
                text: parent.text
                color: Theme.textMuted
                font.pixelSize: 11
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
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
                  font.pixelSize: 11
                  elide: Text.ElideRight
                }
              }
            }

            Row {
              spacing: 8
              Button {
                objectName: "acceptAllButton"
                text: "Accept all"
                enabled: root.agent && !root.agentRunning
                onClicked: if (root.agent) root.agent.acceptAll()
              }
              Button {
                objectName: "rejectAllButton"
                text: "Reject all"
                enabled: root.agent && !root.agentRunning
                onClicked: if (root.agent) root.agent.rejectAll()
              }
            }

            Button {
              id: acceptButton
              objectName: "acceptButton"
              width: parent.width
              text: "Accept " + root.acceptedAgentCount + " agent(s)"
              enabled: root.agent && !root.agentRunning && root.acceptedAgentCount > 0
              onClicked: {
                if (root.agent.commit())
                  root.noticeText = ""
              }
              background: Rectangle {
                radius: 6
                color: parent.hovered ? Qt.lighter(Theme.accent, 1.15) : Theme.accent
              }
              contentItem: Text {
                text: parent.text
                color: Theme.accentText
                font.pixelSize: 11
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
              }
            }
            Button {
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
              background: Rectangle {
                radius: 6
                color: parent.hovered ? Theme.surfaceHover : Theme.surface
                border.width: 1
                border.color: Theme.border
              }
              contentItem: Text {
                text: parent.text
                color: Theme.text
                font.pixelSize: 11
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
              }
            }
          }
        }
      }
    }
      }

  // --- COMPOSER ---
  Rectangle {
    id: composer

    anchors {
      left: parent.left
      right: parent.right
      bottom: parent.bottom
      leftMargin: 12
      rightMargin: 12
      bottomMargin: 12
    }
    height: Math.min(168, 68 + promptInput.contentHeight)
    radius: 14
    color: Theme.surfaceAlt
    border.width: 1
    border.color: Theme.border

    TextArea {
      id: promptInput
      objectName: "promptInput"
      anchors {
        top: parent.top
        left: parent.left
        right: parent.right
        topMargin: 10
        leftMargin: 12
        rightMargin: 12
      }
      height: Math.max(28, contentHeight)
      font.pixelSize: 12
      color: Theme.text
      placeholderText: "Ask Templnote anything…"
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

        Button {
          id: addButton
          width: 28
          height: 28
          padding: 0
          text: "+"
          background: Rectangle {
            radius: 8
            color: addButton.hovered ? Theme.surfaceHover : "transparent"
          }
          contentItem: Text {
            text: parent.text
            color: Theme.textMuted
            font.pixelSize: 18
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
          }
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
          font.pixelSize: 12
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

        Button {
          id: modelButton
          objectName: "modelPickerButton"
          height: 24
          padding: 0
          contentItem: Row {
            spacing: 4
            leftPadding: 8
            rightPadding: 8
            Text {
              anchors.verticalCenter: parent.verticalCenter
              text: root.selectedModelKey.length > 0 ? root.selectedModel : "Auto"
              color: Theme.textMuted
              font.pixelSize: 11
              elide: Text.ElideRight
            }
            Text {
              anchors.verticalCenter: parent.verticalCenter
              text: "⌄"
              color: Theme.textMuted
              font.pixelSize: 12
            }
          }
          background: Rectangle {
            radius: 6
            color: modelButton.hovered || modelPicker.opened
                  ? Theme.surfaceHover : "transparent"
          }
          onClicked: {
            modelPicker.searchText = ""
            modelPicker.levelModelKey = ""
            modelPicker.activeModelKey = root.selectedModelKey
            modelPicker.open()
          }
        }

        Button {
          id: skillsButton
          objectName: "skillsButton"
          width: 24
          height: 24
          padding: 0
          text: "✦"
          background: Rectangle { color: "transparent" }
          contentItem: Text {
            text: parent.text
            color: Theme.textMuted
            font.pixelSize: 14
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
          }
          ToolTip.visible: skillsButton.hovered
          ToolTip.text: "Load a skill"
          ToolTip.delay: 500
          onClicked: skillPicker.open()
        }

        Button {
          id: sendButton
          objectName: "sendButton"
          width: 28
          height: 28
          padding: 0
          background: Rectangle {
            radius: 14
            color: root.agentRunning ? Theme.danger
                   : (sendButton.hovered ? Qt.lighter(Theme.accent, 1.15) : Theme.accent)
            Behavior on color { ColorAnimation { duration: 150 } }
          }
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

    background: Rectangle {
      radius: 10
      color: Theme.surface
      border.width: 1
      border.color: Theme.border
    }

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

          Rectangle {
            anchors {
              left: parent.left
              leftMargin: 12
              right: parent.right
              rightMargin: 12
              verticalCenter: parent.verticalCenter
            }
            height: 30
            radius: 8
            color: Theme.surfaceAlt
            border.width: 1
            border.color: pickerSearch.activeFocus ? Theme.accent : Theme.border

            TextField {
              id: pickerSearch
              objectName: "modelPickerSearch"
              anchors.fill: parent
              leftPadding: 28
              rightPadding: 8
              placeholderText: "Search models"
              placeholderTextColor: Theme.textFaint
              color: Theme.text
              font.pixelSize: 12
              background: Item {}
              onTextChanged: modelPicker.searchText = text
            }

            Text {
              anchors {
                left: parent.left
                leftMargin: 9
                verticalCenter: parent.verticalCenter
              }
              text: "\u2315"
              color: Theme.textFaint
              font.pixelSize: 16
            }
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
              font.pixelSize: 13
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
                font.pixelSize: 12
                elide: Text.ElideRight
              }
              Text {
                width: parent.width
                text: modelData.providerName
                color: Theme.textFaint
                font.pixelSize: 10
                elide: Text.ElideRight
              }
            }

            Button {
              id: levelButton
              anchors {
                right: parent.right
                rightMargin: 12
                verticalCenter: parent.verticalCenter
              }
              width: 16
              height: 22
              padding: 0
              text: "\u203a"
              enabled: registry && registry.thinkingLevels(modelData.key).length > 1
              background: Rectangle { color: "transparent" }
              contentItem: Text {
                text: parent.text
                color: parent.enabled ? Theme.textMuted : "transparent"
                font.pixelSize: 17
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
              }
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
              font.pixelSize: 11
              elide: Text.ElideRight
            }
            Text {
              width: parent.width
              text: modelPicker.activeDetails.modalities || ""
              color: Theme.textMuted
              font.pixelSize: 10
              elide: Text.ElideRight
            }
          }
        }
      }

      // Thinking-level submenu, overlays the right side of the list.
      Rectangle {
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
        border.width: 1
        border.color: Theme.border

        Column {
          anchors.fill: parent
          anchors.margins: 8
          spacing: 2

          Text {
            width: parent.width
            text: "Thinking level"
            color: Theme.textMuted
            font.pixelSize: 11
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
                font.pixelSize: 11
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
                font.pixelSize: 12
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
    width: Math.min(320, root.width - 24)
    height: Math.min(360, 60 + root.skillCount * 44)
    x: 12
    y: Math.max(20, composer.y - height - 8)
    modal: true
    focus: true
    padding: 0
    background: Rectangle {
      color: Theme.surface
      border.color: Theme.border
      border.width: 1
      radius: 10
    }
    contentItem: Column {
      anchors.fill: parent
      Text {
        width: parent.width
        padding: 10
        text: "Skills"
        color: Theme.text
        font.bold: true
        font.pixelSize: 12
      }
      Repeater {
        id: skillListView
        model: root.skillList || []
        ItemDelegate {
          objectName: "skillItem"
          property var info: (typeof modelData !== "undefined") ? modelData : model
          width: parent ? parent.width : 0
          height: 44
          text: info.name || info.id
          onClicked: {
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
    width: Math.min(360, root.width - 24)
    height: Math.min(480, 200 + (sessionList ? sessionList.count : 0) * 48)
    x: root.width - width - 12
    y: 44
    padding: 0
    modal: true
    focus: true
    background: Rectangle {
      color: Theme.surface
      border.color: Theme.border
      border.width: 1
      radius: 10
    }
    contentItem: Column {
      anchors.fill: parent
      clip: true

      Row {
        width: parent.width
        height: 40
        spacing: 4
        TextField {
          id: renameField
          width: parent.width - 84
          height: 32
          placeholderText: "Rename current chat"
          text: root.agent ? root.agent.activeSessionTitle : ""
        }
        Button {
          width: 76
          height: 32
          text: "Rename"
          enabled: root.agent && renameField.text.trim().length > 0
          onClicked: {
            root.agent.renameSession(root.agent.activeSessionId,
                                     renameField.text.trim())
            renameField.text = ""
          }
        }
      }
      Rectangle {
        width: parent.width
        height: 1
        color: Theme.border
      }
      ItemDelegate {
        width: parent.width
        height: 40
        text: "＋ New conversation"
        onClicked: {
          if (root.agent)
            root.agent.createSession()
          chatListPopup.close()
          root.noticeText = ""
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
        height: Math.min(320, count * 44)
        model: (root.agent && root.agent.sessionsModel)
               ? root.agent.sessionsModel : ((root.agent && root.agent.sessions) || [])
        clip: true
        delegate: ItemDelegate {
          id: sessionRow
          property var sessionInfo: (typeof modelData !== "undefined")
                                    ? modelData : model
          width: sessionList.width
          height: 48
          highlighted: sessionInfo.id === root.agent.activeSessionId
          text: (sessionInfo.running ? "◌ " : "") +
                (sessionInfo.title || "Conversation") +
                (sessionInfo.unread ? "  (" + sessionInfo.unread + ")" : "")
          onClicked: {
            root.agent.switchSession(sessionInfo.id)
            chatListPopup.close()
            conversation.contentY = 0
          }
          ToolButton {
            anchors {
              right: parent.right
              verticalCenter: parent.verticalCenter
              rightMargin: 6
            }
            width: 28
            height: 28
            text: "✕"
            font.pixelSize: 11
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
