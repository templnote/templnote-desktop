import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Templnote

Item {
  id: root

  property string blockId: ""
  property int blockType: 10
  property string content: ""
  property int headingLevel: 0
  property int numberIndex: 1
  property string alignment: "left"
  property bool isChecked: false
  property string calloutType: "note"
  property bool showPlaceholder: false
  property var model: null
  property var searchHighlights: []

  readonly property int cursorPosition: editor.cursorPosition
  readonly property string editorText: editor.text
  readonly property string cleanBody: root.parseRawMarkdown(editor.text).cleanBody
  readonly property bool isFocused: editor.activeFocus
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
  property bool searchNavigationActive: false

  // Internal flag to avoid syncing loops when setting editor.text
  property bool internalUpdating: false

  // Map callout types to emoji, accent colors, and background tints
  function calloutConfig(type) {
    const t = (type || "note").toLowerCase()
    switch (t) {
    case "tip":
    case "idea":
      return { emoji: "💡", color: "#e3b341", bgAlpha: 0.12, name: "Tip" }
    case "warning":
    case "caution":
      return { emoji: "⚠️", color: "#f0883e", bgAlpha: 0.13, name: "Warning" }
    case "danger":
    case "error":
      return { emoji: "🚨", color: "#f7768e", bgAlpha: 0.14, name: "Danger" }
    case "success":
    case "done":
      return { emoji: "✅", color: "#73daca", bgAlpha: 0.12, name: "Success" }
    case "info":
      return { emoji: "ℹ️", color: "#7aa2f7", bgAlpha: 0.12, name: "Info" }
    case "note":
    default:
      return { emoji: "📝", color: Theme.accent, bgAlpha: 0.12, name: "Note" }
    }
  }

  readonly property var currentConfig: calloutConfig(root.calloutType)
  property string customTitle: ""

  // Converts the internal stored content to the full markdown string shown while editing
  // e.g. "> [!NOTE] Optional Title\n> line 1\n> line 2"
  function getRawMarkdownFromModel() {
    const typeTag = (root.calloutType || "note").toUpperCase()
    const titlePart = root.customTitle && root.customTitle.length > 0 ? (" " + root.customTitle) : ""
    const header = "> [!" + typeTag + "]" + titlePart
    if (!root.content || root.content.length === 0) {
      return header + "\n> "
    }
    const lines = root.content.split("\n")
    const prefixedLines = lines.map(function(line) {
      return line.startsWith("> ") ? line : ("> " + line)
    })
    return header + "\n" + prefixedLines.join("\n")
  }

  // Parses raw editor markdown back into { type, title, cleanBody, isValidCallout }
  function parseRawMarkdown(raw) {
    const lines = (raw || "").split("\n")
    if (lines.length === 0) {
      return { type: "note", title: "", cleanBody: "", isValidCallout: false }
    }

    const firstLine = lines[0].trim()
    const match = /^>\s*\[!([a-zA-Z0-9_-]+)\](?:\s*(.*))?$/.exec(firstLine)
    if (!match) {
      return { type: "note", title: "", cleanBody: raw, isValidCallout: false }
    }

    const parsedType = match[1].toLowerCase()
    const parsedTitle = match[2] ? match[2].trim() : ""
    const bodyLines = lines.slice(1).map(function(line) {
      if (line.startsWith("> ")) return line.substring(2)
      if (line.startsWith(">")) return line.substring(1)
      return line
    })

    return {
      type: parsedType,
      title: parsedTitle,
      cleanBody: bodyLines.join("\n"),
      isValidCallout: true
    }
  }

  function commitEditorText(newText, newStart, newEnd) {
    editor.text = newText
    editor.select(newStart, newEnd)
    if (root.model && root.blockId.length > 0) {
      const parsed = parseRawMarkdown(newText)
      if (parsed.isValidCallout) {
        if (parsed.type !== root.calloutType)
          root.model.setBlockCalloutType(root.blockId, parsed.type)
        root.customTitle = parsed.title
        root.model.updateBlockText(root.blockId, parsed.cleanBody)
      } else {
        root.model.updateBlockText(root.blockId, newText)
      }
    }
    editor.notifySelection()
  }

  function isInlineTagFormat(type) {
    return type === "underline" || type === "subscript" || type === "superscript"
  }

  function inlineTagType(type) {
    if (type === "underline") return "u"
    if (type === "subscript") return "sub"
    if (type === "superscript") return "sup"
    return ""
  }

  function parseInlineTags(text) {
    const spans = []
    const tokens = []
    const stack = []
    const tagRegex = /<(\/?)(u|sub|sup)>/gi
    let match

    while ((match = tagRegex.exec(text)) !== null) {
      const type = match[2].toLowerCase()
      const closing = match[1] === "/"
      const token = {
        type: type,
        closing: closing,
        start: match.index,
        end: match.index + match[0].length,
        matched: false
      }
      tokens.push(token)

      if (!closing) {
        stack.push({ type: type, token: token })
      } else if (stack.length > 0 && stack[stack.length - 1].type === type) {
        const open = stack.pop()
        open.token.matched = true
        token.matched = true
        spans.push({
          type: type,
          openStart: open.token.start,
          openEnd: open.token.end,
          contentStart: open.token.end,
          contentEnd: token.start,
          closeStart: token.start,
          closeEnd: token.end
        })
      }
    }
    return { spans: spans, tokens: tokens }
  }

  function isFormatActive(type) {
    const raw = editor.text
    const s = Math.min(editor.selectionStart, editor.selectionEnd)
    const e = Math.max(editor.selectionStart, editor.selectionEnd)
    if (s === e || !raw) return false

    const sel = raw.substring(s, e)
    let openTag = "", closeTag = ""
    let regex = null

    if (type === "bold") {
      openTag = "**"; closeTag = "**"
      regex = /\*\*(.+?)\*\*/g
    } else if (type === "italic") {
      openTag = "*"; closeTag = "*"
      regex = /(?:^|[^*])\*([^*\n]+?)\*(?:[^*]|$)/g
    } else if (type === "underline") {
      openTag = "<u>"; closeTag = "</u>"
    } else if (type === "strike") {
      openTag = "~~"; closeTag = "~~"
      regex = /~~(.+?)~~/g
    } else if (type === "subscript") {
      openTag = "<sub>"; closeTag = "</sub>"
    } else if (type === "superscript") {
      openTag = "<sup>"; closeTag = "</sup>"
    }

    const otLen = openTag.length
    const ctLen = closeTag.length

    if (sel.toLowerCase().startsWith(openTag.toLowerCase()) &&
        sel.toLowerCase().endsWith(closeTag.toLowerCase()) &&
        sel.length >= (otLen + ctLen)) {
      return true
    }

    if (s >= otLen && e + ctLen <= raw.length &&
        raw.substring(s - otLen, s).toLowerCase() === openTag.toLowerCase() &&
        raw.substring(e, e + ctLen).toLowerCase() === closeTag.toLowerCase()) {
      return true
    }

    if (isInlineTagFormat(type)) {
      const tagName = inlineTagType(type)
      const parsed = parseInlineTags(raw)
      for (let i = 0; i < parsed.spans.length; ++i) {
        const span = parsed.spans[i]
        if (span.type === tagName && s >= span.contentStart && e <= span.contentEnd)
          return true
      }
    } else if (regex) {
      let match
      while ((match = regex.exec(raw)) !== null) {
        let mStart = match.index
        let mEnd = match.index + match[0].length
        let contentStart = mStart + otLen
        let contentEnd = mEnd - ctLen
        if (type === "italic") {
          if (!match[0].startsWith("*")) mStart += 1
          if (!match[0].endsWith("*")) mEnd -= 1
          contentStart = mStart + 1
          contentEnd = mEnd - 1
        }
        if (s >= contentStart && e <= contentEnd)
          return true
      }
    }
    return false
  }

  function toggleFormat(type) {
    const raw = editor.text
    const s = Math.min(editor.selectionStart, editor.selectionEnd)
    const e = Math.max(editor.selectionStart, editor.selectionEnd)

    const sel = raw.substring(s, e)
    let openTag = "", closeTag = ""
    let regex = null

    if (type === "bold") {
      openTag = "**"; closeTag = "**"
      regex = /\*\*(.+?)\*\*/g
    } else if (type === "italic") {
      openTag = "*"; closeTag = "*"
      regex = /(?:^|[^*])\*([^*\n]+?)\*(?:[^*]|$)/g
    } else if (type === "underline") {
      openTag = "<u>"; closeTag = "</u>"
    } else if (type === "strike") {
      openTag = "~~"; closeTag = "~~"
      regex = /~~(.+?)~~/g
    } else if (type === "subscript") {
      openTag = "<sub>"; closeTag = "</sub>"
    } else if (type === "superscript") {
      openTag = "<sup>"; closeTag = "</sup>"
    }

    const otLen = openTag.length
    const ctLen = closeTag.length

    if (s === e) {
      if (type !== "subscript" && type !== "superscript")
        return

      if (s >= otLen && s + ctLen <= raw.length &&
          raw.substring(s - otLen, s).toLowerCase() === openTag.toLowerCase() &&
          raw.substring(s, s + ctLen).toLowerCase() === closeTag.toLowerCase()) {
        commitEditorText(raw.substring(0, s - otLen) + raw.substring(s + ctLen),
                         s - otLen, s - otLen)
      } else {
        const newText = raw.substring(0, s) + openTag + closeTag + raw.substring(e)
        commitEditorText(newText, s + otLen, s + otLen)
      }
      return
    }

    let newText = ""
    let newStart = s
    let newEnd = e

    if (sel.toLowerCase().startsWith(openTag.toLowerCase()) &&
        sel.toLowerCase().endsWith(closeTag.toLowerCase()) &&
        sel.length >= (otLen + ctLen)) {
      const inner = sel.substring(otLen, sel.length - ctLen)
      newText = raw.substring(0, s) + inner + raw.substring(e)
      newStart = s
      newEnd = s + inner.length
    } else if (s >= otLen && e + ctLen <= raw.length &&
               raw.substring(s - otLen, s).toLowerCase() === openTag.toLowerCase() &&
               raw.substring(e, e + ctLen).toLowerCase() === closeTag.toLowerCase()) {
      newText = raw.substring(0, s - otLen) + sel + raw.substring(e + ctLen)
      newStart = s - otLen
      newEnd = e - otLen
    } else if (isInlineTagFormat(type)) {
      newText = raw.substring(0, s) + openTag + sel + closeTag + raw.substring(e)
      newStart = s
      newEnd = e + otLen + ctLen
    } else {
      newText = raw.substring(0, s) + openTag + sel + closeTag + raw.substring(e)
      newStart = s
      newEnd = e + otLen + ctLen
    }

    commitEditorText(newText, newStart, newEnd)
  }

  function toggleScriptFormat(type) {
    const raw = editor.text
    const s = Math.min(editor.selectionStart, editor.selectionEnd)
    const e = Math.max(editor.selectionStart, editor.selectionEnd)

    if (s !== e && isFormatActive(type)) {
      toggleFormat(type)
      return
    }

    const other = type === "subscript" ? "superscript" : "subscript"
    if (s !== e && isFormatActive(other)) {
      toggleFormat(other)
    }

    toggleFormat(type)
  }

  // Renders rich text HTML for the body of the callout (when unfocused)
  function renderBodyHtml(rawBody) {
    if (!rawBody || rawBody.length === 0) return ""

    let escaped = rawBody.replace(/&/g, "&amp;")
                         .replace(/</g, "&lt;")
                         .replace(/>/g, "&gt;")

    escaped = escaped.replace(/\*\*(.+?)\*\*/g, "<b>$1</b>")
    escaped = escaped.replace(/~~(.+?)~~/g, "<s>$1</s>")
    escaped = escaped.replace(/(?:^|[^*])\*([^*\n]+?)\*(?:[^*]|$)/g, function(match, p1) {
      let prefix = match.startsWith("*") ? "" : match.charAt(0)
      let suffix = match.endsWith("*") ? "" : match.charAt(match.length - 1)
      return prefix + "<i>" + p1 + "</i>" + suffix
    })

    escaped = escaped.replace(/\n/g, "<br/>")
    escaped = escaped.replace(/  +/g, function(m) {
      return " " + "&nbsp;".repeat(m.length - 1)
    })
    return escaped
  }

  function focusEditor(position) {
    if (editor.text.length === 0) {
      internalUpdating = true
      editor.text = root.getRawMarkdownFromModel()
      internalUpdating = false
    }

    editor.focus = true
    editor.forceActiveFocus()

    let safePos = position
    if (position < 0) {
      safePos = editor.length
    } else if (position === 0) {
      // When converting into Callout or landing at pos 0, place cursor after header line and prefix
      const firstLineBreak = editor.text.indexOf("\n")
      if (firstLineBreak >= 0) {
        safePos = Math.min(firstLineBreak + 3, editor.length)
      } else {
        safePos = editor.length
      }
    } else {
      const firstLineBreak = editor.text.indexOf("\n")
      if (firstLineBreak >= 0) {
        safePos = Math.min(firstLineBreak + 3 + position, editor.length)
      } else {
        safePos = Math.max(0, Math.min(position, editor.length))
      }
    }
    editor.cursorPosition = safePos
    updateCaretGeometry()
  }

  function focusEditorAtX(xPosition) {
    if (editor.text.length === 0) {
      internalUpdating = true
      editor.text = root.getRawMarkdownFromModel()
      internalUpdating = false
    }

    editor.focus = true
    editor.forceActiveFocus()
    const localX = Math.max(0, xPosition - editor.x)
    if (editor.text.length > 0) {
      editor.cursorPosition = editor.positionAt(localX, root.firstLineHeight / 2)
    } else {
      editor.cursorPosition = 0
    }
    updateCaretGeometry()
  }

  function focusEditorRange(start, end) {
    editor.focus = true
    editor.forceActiveFocus()
    editor.select(start, end)
    updateCaretGeometry()
  }

  function refreshFromModel() {
    if (!editor.activeFocus) {
      internalUpdating = true
      editor.text = getRawMarkdownFromModel()
      internalUpdating = false
    }
  }

  function updateCaretGeometry() {
    if (editor.activeFocus) {
      const cRect = editor.cursorRectangle
      const mapped = editor.mapToItem(null, cRect.x, cRect.y)
      root.caretMoved(editor.cursorPosition, mapped ? mapped.y : 0, Math.max(18, cRect.height))
    }
  }

  // Helpers to detect if cursor is on the first or last visual line of the callout editor
  function isCursorOnFirstLine() {
    const textBefore = editor.text.substring(0, editor.cursorPosition)
    return textBefore.indexOf("\n") < 0
  }

  function isCursorOnLastLine() {
    const textAfter = editor.text.substring(editor.cursorPosition)
    return textAfter.indexOf("\n") < 0
  }

  onContentChanged: {
    if (!editor.activeFocus) {
      internalUpdating = true
      editor.text = getRawMarkdownFromModel()
      internalUpdating = false
    }
  }

  onCalloutTypeChanged: {
    if (!editor.activeFocus) {
      internalUpdating = true
      editor.text = getRawMarkdownFromModel()
      internalUpdating = false
    }
  }

  implicitWidth: parent ? parent.width : 400
  implicitHeight: editor.activeFocus
                  ? Math.max(48, focusedEditorContainer.implicitHeight + 8)
                  : Math.max(48, renderedContainer.implicitHeight + 8)

  // =========================================================================
  // 1. UNFOCUSED / RENDERED VIEW (Obsidian-like callout box)
  // =========================================================================
  Rectangle {
    id: renderedContainer
    anchors.top: parent.top
    anchors.topMargin: 4
    anchors.left: parent.left
    anchors.leftMargin: 8
    anchors.right: parent.right
    anchors.rightMargin: 8
    visible: opacity > 0.0
    opacity: editor.activeFocus ? 0.0 : 1.0
    z: editor.activeFocus ? 1 : 2
    radius: 8

    implicitHeight: Math.max(44, renderedContentCol.implicitHeight + 20)

    // Background color tint & border
    color: Qt.rgba(root.currentConfig.color.r, root.currentConfig.color.g, root.currentConfig.color.b, root.currentConfig.bgAlpha)
    border.color: Qt.rgba(root.currentConfig.color.r, root.currentConfig.color.g, root.currentConfig.color.b, 0.35)
    border.width: 1

    Column {
      id: renderedContentCol
      anchors.top: parent.top
      anchors.left: parent.left
      anchors.right: parent.right
      anchors.margins: 10
      spacing: 6

      // Header row: Emoji + Title (e.g. "Note", "Tip" or custom title)
      Row {
        spacing: 8
        anchors.left: parent.left
        anchors.right: parent.right

        Text {
          text: root.currentConfig.emoji
          font.pixelSize: 17
          font.family: "Noto Color Emoji, Apple Color Emoji, Segoe UI Emoji, sans-serif"
          verticalAlignment: Text.AlignVCenter
        }

        Text {
          text: (root.customTitle && root.customTitle.length > 0) ? root.customTitle : root.currentConfig.name
          font.pixelSize: 15
          font.bold: true
          color: root.currentConfig.color
          verticalAlignment: Text.AlignVCenter
        }
      }

      // Rendered Rich Text body
      Text {
        id: renderedBodyText
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 2
        wrapMode: Text.Wrap
        textFormat: Text.RichText
        font.pixelSize: 15
        color: Theme.text
        text: root.renderBodyHtml(root.content)
        visible: text.length > 0
      }
    }

    // Clicking anywhere on the rendered callout box focuses the raw markdown editor
    MouseArea {
      anchors.fill: parent
      cursorShape: Qt.IBeamCursor
      onClicked: function(mouse) {
        if (editor.text.length === 0) {
          root.internalUpdating = true
          editor.text = root.getRawMarkdownFromModel()
          root.internalUpdating = false
        }
        editor.forceActiveFocus()
        const localX = Math.max(0, mouse.x - editor.x)
        const localY = Math.max(0, mouse.y - editor.y)
        const pos = editor.positionAt(localX, localY)
        editor.cursorPosition = Math.max(0, Math.min(pos, editor.length))
        root.updateCaretGeometry()
      }
    }
  }

  // =========================================================================
  // 2. FOCUSED VIEW: Raw Markdown Editor (like Obsidian Live Preview)
  // Shows full markdown syntax: e.g. `> [!NOTE]\n> content` with styled border
  // Always visible in visual tree so TextArea never drops activeFocus
  // =========================================================================
  Rectangle {
    id: focusedEditorContainer
    anchors.top: parent.top
    anchors.topMargin: 4
    anchors.left: parent.left
    anchors.leftMargin: 8
    anchors.right: parent.right
    anchors.rightMargin: 8
    visible: opacity > 0.0
    opacity: editor.activeFocus ? 1.0 : 0.0
    z: editor.activeFocus ? 2 : 1
    radius: 6

    implicitHeight: Math.max(48, editor.implicitHeight + 16)

    // Keep callout color tint and left accent bar even while editing
    color: Qt.rgba(root.currentConfig.color.r, root.currentConfig.color.g, root.currentConfig.color.b, 0.06)
    border.color: Qt.rgba(root.currentConfig.color.r, root.currentConfig.color.g, root.currentConfig.color.b, 0.35)
    border.width: 1

    // Left accent bar
    Rectangle {
      width: 4
      anchors.top: parent.top
      anchors.bottom: parent.bottom
      anchors.left: parent.left
      radius: 2
      color: root.currentConfig.color
    }

    TextArea {
      id: editor
      anchors.fill: parent
      anchors.leftMargin: 12
      anchors.rightMargin: 12
      anchors.topMargin: 8
      anchors.bottomMargin: 8
      wrapMode: TextArea.Wrap
      textFormat: TextArea.PlainText
      font.pixelSize: 15
      color: Theme.text
      cursorVisible: activeFocus
      selectionColor: Qt.rgba(root.currentConfig.color.r, root.currentConfig.color.g, root.currentConfig.color.b, 0.35)
      selectedTextColor: Theme.text
      background: null
      padding: 0
      leftPadding: 0
      rightPadding: 0
      topPadding: 0
      bottomPadding: 0

      Keys.priority: Keys.BeforeItem

      function notifySelection() {
        if (editor.selectionStart === editor.selectionEnd) {
          root.selectionChanged(false, 0, 0, 0)
          return
        }
        if (!editor.activeFocus) return
        const cRect = editor.cursorRectangle
        const mapped = editor.mapToItem(root, cRect.x + cRect.width / 2, cRect.y)
        root.selectionChanged(true, mapped ? mapped.x : 0, mapped ? mapped.y : 0, Math.max(18, cRect.height))
      }

      onSelectionStartChanged: notifySelection()
      onSelectionEndChanged: notifySelection()

      onActiveFocusChanged: {
        root.updateCaretGeometry()
        if (activeFocus) {
          if (editor.text.length === 0) {
            internalUpdating = true
            editor.text = root.getRawMarkdownFromModel()
            internalUpdating = false
          }
        } else {
          // On losing focus: parse the raw markdown and update model text and callout type
          const parsed = root.parseRawMarkdown(editor.text)
          if (!parsed.isValidCallout) {
            // Only convert to paragraph if the text was actually modified/cleared of callout syntax
            if (root.model && root.blockId.length > 0 && editor.text.trim().length > 0) {
              root.model.updateBlockText(root.blockId, editor.text)
              root.model.changeBlockType(root.blockId, 0, 0)
            }
          } else {
            if (root.model && root.blockId.length > 0) {
              if (parsed.type !== root.calloutType) {
                root.model.setBlockCalloutType(root.blockId, parsed.type)
              }
              root.customTitle = parsed.title
              root.model.updateBlockText(root.blockId, parsed.cleanBody)
            }
          }
          root.selectionChanged(false, 0, 0, 0)
        }
      }

      onCursorPositionChanged: {
        root.updateCaretGeometry()
      }

      onTextChanged: {
        if (!activeFocus || root.internalUpdating) return

        // Live check if callout type or title changed
        const parsed = root.parseRawMarkdown(editor.text)
        if (parsed.isValidCallout) {
          root.customTitle = parsed.title
          if (parsed.type !== root.calloutType && root.model && root.blockId.length > 0) {
            root.model.setBlockCalloutType(root.blockId, parsed.type)
          }
        }
      }

      Keys.onPressed: function(event) {
        // Option 1 & Option A behavior:
        // Shift+Enter exits the callout into a new block below
        if ((event.key === Qt.Key_Enter || event.key === Qt.Key_Return) && (event.modifiers & Qt.ShiftModifier)) {
          event.accepted = true
          // Sync current body to model first
          const parsed = root.parseRawMarkdown(editor.text)
          if (root.model && root.blockId.length > 0) {
            if (parsed.isValidCallout) {
              if (parsed.type !== root.calloutType)
                root.model.setBlockCalloutType(root.blockId, parsed.type)
              root.customTitle = parsed.title
              root.model.updateBlockText(root.blockId, parsed.cleanBody)
            } else {
              root.model.updateBlockText(root.blockId, editor.text)
            }
          }
          root.enterPressed()
          return
        }

        // Regular Enter: auto-continue markdown quote prefix `> ` on new line
        if (event.key === Qt.Key_Enter || event.key === Qt.Key_Return) {
          event.accepted = true

          // Check current line text: if user pressed Enter on an empty `> ` or `>` line, exit callout!
          const textBeforeCursor = editor.text.substring(0, editor.cursorPosition)
          const textAfterCursor = editor.text.substring(editor.cursorPosition)
          const lastLineStart = textBeforeCursor.lastIndexOf("\n") + 1
          const nextLineBreak = textAfterCursor.indexOf("\n")
          const currentLineEnd = nextLineBreak === -1 ? editor.text.length : (editor.cursorPosition + nextLineBreak)
          const fullCurrentLine = editor.text.substring(lastLineStart, currentLineEnd)

          if (fullCurrentLine.trim() === ">" || fullCurrentLine.trim() === "") {
            // Exit callout: remove the empty `> ` line and create new paragraph block below
            const remaining = nextLineBreak === -1 ? "" : textAfterCursor.substring(nextLineBreak + 1)
            let cleanPrefix = editor.text.substring(0, lastLineStart)
            if (cleanPrefix.endsWith("\n")) {
              cleanPrefix = cleanPrefix.substring(0, cleanPrefix.length - 1)
            }
            const cleanText = cleanPrefix + (remaining.length > 0 ? ("\n" + remaining) : "")
            const parsed = root.parseRawMarkdown(cleanText)
            if (root.model && root.blockId.length > 0) {
              if (parsed.isValidCallout) {
                root.customTitle = parsed.title
                root.model.updateBlockText(root.blockId, parsed.cleanBody)
              } else {
                root.model.updateBlockText(root.blockId, cleanText)
              }
            }
            root.enterPressed()
            return
          }

          // Otherwise insert newline with `> ` prefix
          editor.insert(editor.cursorPosition, "\n> ")
          return
        }

        // Backspace:
        // Convert to paragraph if cursor is at position 0, or if deleting empty line
        if (event.key === Qt.Key_Backspace) {
          const textBefore = editor.text.substring(0, editor.cursorPosition)
          const isFirstLine = textBefore.indexOf("\n") < 0
          if (editor.cursorPosition === 0 || (isFirstLine && editor.text.indexOf("\n") === -1 && editor.text.trim() === "")) {
            event.accepted = true
            const parsed = root.parseRawMarkdown(editor.text)
            if (root.model && root.blockId.length > 0) {
              root.model.updateBlockText(root.blockId, parsed.cleanBody)
              root.model.changeBlockType(root.blockId, 0, 0)
              root.focusRequested(0)
            }
            return
          }

          // If on a subsequent line and cursor is right after `> ` or `>`, delete the prefix and merge with line above
          const lastLineStart = textBefore.lastIndexOf("\n") + 1
          const col = editor.cursorPosition - lastLineStart
          const lineText = textBefore.substring(lastLineStart)
          if (lastLineStart > 0 && (lineText === "> " || lineText === ">") && col <= 2) {
            event.accepted = true
            // Delete from lastLineStart - 1 (the preceding newline) to cursorPosition
            editor.remove(lastLineStart - 1, editor.cursorPosition)
            return
          }
        }

        // Up arrow: if on the first line, move focus to the block above
        if (event.key === Qt.Key_Up && root.isCursorOnFirstLine()) {
          event.accepted = true
          const cRect = editor.cursorRectangle
          root.moveFocusRequested(-1, cRect.x + editor.x)
          return
        }

        // Down arrow: if on the last line, move focus to the block below
        if (event.key === Qt.Key_Down && root.isCursorOnLastLine()) {
          event.accepted = true
          const cRect = editor.cursorRectangle
          root.moveFocusRequested(1, cRect.x + editor.x)
          return
        }

        if (event.matches(StandardKey.Undo)) {
          event.accepted = true
          root.undoRequested()
          return
        }

        if (event.matches(StandardKey.Redo)) {
          event.accepted = true
          root.redoRequested()
          return
        }
      }

      Component.onCompleted: {
        internalUpdating = true
        editor.text = root.getRawMarkdownFromModel()
        internalUpdating = false
      }
    }
  }
}
