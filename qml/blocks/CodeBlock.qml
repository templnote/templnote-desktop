import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Templnote

Item {
  id: root

  property string blockId: ""
  property int blockType: 6 // BlockType::Code
  property string content: ""
  property int headingLevel: 0
  property int numberIndex: 1
  property string alignment: "left"
  property bool isChecked: false
  property string codeLanguage: ""
  property var blockMetadata: null
  readonly property string codeFence: {
    const fence = blockMetadata && blockMetadata.codeFence
                  ? String(blockMetadata.codeFence) : ""
    return /^[`~]{3,}$/.test(fence) ? fence : "```"
  }
  property bool showPlaceholder: false
  property var model: null
  property var searchHighlights: []

  readonly property int cursorPosition: editor.cursorPosition
  readonly property string editorText: editor.text
  // Model text without the displayed ```lang / ``` fence lines.
  readonly property string cleanBody: parseEditorText(editor.text).body
  readonly property bool isFocused: editor.activeFocus
  readonly property real firstLineHeight: 22

  // The focused editor always shows the closing fence as its last line.
  // Clicking that line is an "exit" affordance rather than an edit target.
  readonly property int closingFenceLineStart: {
    if (!editor)
      return -1

    const text = editor.text || ""
    const lastNewline = text.lastIndexOf("\n")

    if (lastNewline < 0)
      return -1

    return text.substring(lastNewline + 1).trim() === root.codeFence
           ? lastNewline + 1
           : -1
  }

  readonly property rect closingFenceLineRect: {
    // Reference the text and layout so the rectangle is recomputed when the
    // code, wrapping or focus state changes.
    const text = editor ? editor.text : ""
    const width = editor ? editor.width : 0
    const height = editor ? editor.implicitHeight : 0

    const fenceStart = root.closingFenceLineStart
    // closingFenceLineStart can still hold the old value for one evaluation
    // while editor.text has already been replaced by the shorter body. Passing
    // an out-of-range position to positionToRectangle makes QTextCursor warn.
    if (!root.isFocused || fenceStart < 0 || fenceStart >= text.length
        || text.length === 0 || width <= 0 || height <= 0)
      return Qt.rect(0, 0, 0, 0)

    const lineRect = editor.positionToRectangle(fenceStart)

    return Qt.rect(editor.x, editor.y + lineRect.y, editor.width,
                   Math.max(lineRect.height, editor.font.pixelSize))
  }

  // Pending focus/range requests can reference an older, longer version of
  // the text. Clamp against the live document length so QTextCursor is never
  // asked to move to an out-of-range position.
  function clampCursorPosition(position) {
    if (!editor)
      return 0
    const max = Math.max(0, editor.text.length)
    return Math.max(0, Math.min(position, max))
  }

  // Replacing the raw markdown (with fences) by the plain body shortens the
  // document. Move the caret into range first, otherwise QQuickTextEdit warns
  // with "QTextCursor::setPosition: Position ... out of range" and resets it.
  function setEditorText(text) {
    if (!editor)
      return
    const value = text || ""
    editor.cursorPosition = root.clampCursorPosition(
          Math.min(editor.cursorPosition, value.length))
    editor.text = value
  }

  // Last editable position in the body: the newline just before the closing
  // fence. Capping the caret here (instead of at editor.text.length) keeps it
  // off the closing fence line, so Enter inserts a newline instead of exiting.
  readonly property int bodyEndPosition: {
    if (!editor)
      return 0
    if (!root.isFocused)
      return editor.text.length
    const fenceStart = root.closingFenceLineStart
    return fenceStart >= 0
           ? Math.min(fenceStart - 1, editor.text.length)
           : editor.text.length
  }

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

  property bool internalUpdating: false
  property bool copiedNotification: false
  // Set by exitCodeBlockBelow() after it strips the fences and saves the model.
  // Prevents the subsequent focus-loss handler from re-parsing the plain body
  // as raw markdown (which would clear the language tag).
  property bool skipFocusLossSync: false

  // Formats raw markdown into editor text when focused
  function getRawMarkdown() {
    const lang = (root.codeLanguage || "").trim()
    const header = root.codeFence + lang
    const body = root.content || ""
    // Keep an explicit body line even when empty, so there is a real editing
    // position between the fences instead of the caret sitting on the close.
    return header + "\n" + body + "\n" + root.codeFence
  }

  // Parses the editor text back into language and code body
  function parseEditorText(raw) {
    const lines = (raw || "").split("\n")
    if (lines.length === 0) {
      return { lang: "", body: "" }
    }

    let lang = ""
    let bodyStartIndex = 0
    let bodyEndIndex = lines.length

    if (lines[0].startsWith(root.codeFence)) {
      lang = lines[0].substring(root.codeFence.length).trim()
      bodyStartIndex = 1
    }

    if (lines.length > 1 && lines[lines.length - 1].trim() === root.codeFence) {
      bodyEndIndex = lines.length - 1
    }

    const bodyLines = lines.slice(bodyStartIndex, bodyEndIndex)
    return {
      lang: lang,
      body: bodyLines.join("\n")
    }
  }

  function syncToEditor() {
    if (internalUpdating)
      return
    internalUpdating = true
    if (root.isFocused) {
      const prevPos = editor.cursorPosition
      editor.text = getRawMarkdown()
      editor.cursorPosition = root.clampCursorPosition(
            Math.min(prevPos, root.bodyEndPosition))
    } else {
      root.setEditorText(root.content || "")
    }
    internalUpdating = false
  }

  function focusEditor(position) {
    editor.forceActiveFocus()
    if (!root.isFocused)
      return

    if (position === -1) {
      // Focus in the body just before the closing ```
      const lines = editor.text.split("\n")
      if (lines.length > 1 && lines[lines.length - 1].trim() === root.codeFence) {
        const lastLineLen = lines[lines.length - 1].length
        const targetPos = Math.max(0, editor.text.length - (lastLineLen + 1))
        editor.cursorPosition = root.clampCursorPosition(targetPos)
      } else {
        editor.cursorPosition = editor.text.length
      }
    } else {
      // If position was requested relative to clean body, offset by first fence line
      const firstLineEnd = editor.text.indexOf("\n")
      const offset = (firstLineEnd >= 0) ? (firstLineEnd + 1) : 0
      editor.cursorPosition = root.clampCursorPosition(
            Math.min(offset + position, root.bodyEndPosition))
    }
    updateCaretGeometry()
  }

  // The editor shows ` ```lang\n ... \n``` ` while focused, but the model (and
  // therefore focus requests) uses body-relative positions. Convert a raw
  // editor offset to the body coordinate before handing it back to
  // focusEditor(), which adds the fence offset again.
  function rawToBodyPosition(rawPosition) {
    const text = editor ? editor.text : ""
    const firstLineEnd = text.indexOf("\n")
    const offset = (firstLineEnd >= 0) ? (firstLineEnd + 1) : 0
    return Math.max(0, rawPosition - offset)
  }

  function focusEditorAtX(xPosition) {
    editor.forceActiveFocus()
    const targetY = editor.cursorRectangle.y
    editor.cursorPosition = root.clampCursorPosition(editor.positionAt(xPosition, targetY))
    updateCaretGeometry()
  }

  function focusEditorRange(start, end) {
    editor.forceActiveFocus()
    editor.select(root.clampCursorPosition(start), root.clampCursorPosition(end))
    updateCaretGeometry()
  }

  function refreshFromModel() {
    syncToEditor()
  }

  function updateCaretGeometry() {
    if (editor.activeFocus) {
      const cRect = editor.cursorRectangle
      const mapped = editor.mapToItem(null, cRect.x, cRect.y)
      root.caretMoved(editor.cursorPosition, mapped ? mapped.y : 0, Math.max(18, cRect.height))
    }
  }

  function isCursorOnFirstLine() {
    const textBefore = editor.text.substring(0, editor.cursorPosition)
    return textBefore.indexOf("\n") < 0
  }

  function isCursorOnLastLine() {
    const textAfter = editor.text.substring(editor.cursorPosition)
    return textAfter.indexOf("\n") < 0
  }

  onContentChanged: syncToEditor()
  onCodeLanguageChanged: {
    highlighter.language = root.codeLanguage || ""
    syncToEditor()
  }

  onIsFocusedChanged: {
    internalUpdating = true
    if (isFocused) {
      const prevPos = editor.cursorPosition
      editor.text = getRawMarkdown()
      const firstLineEnd = editor.text.indexOf("\n")
      const firstLineLen = (firstLineEnd >= 0) ? (firstLineEnd + 1) : 0
      editor.cursorPosition = root.clampCursorPosition(
            Math.min(firstLineLen + prevPos, root.bodyEndPosition))
      Qt.callLater(updateCaretGeometry)
    } else {
      // After a keyboard exit the fences are already stripped and the model is
      // saved, so editor.text holds just the body. Parsing that as raw markdown
      // would derive an empty language and clear the saved tag. Only sync when
      // a fence header is actually present and the exit path did not opt out.
      const raw = editor.text || ""
      const hasFenceHeader = raw.trim().startsWith(root.codeFence)
      if (!root.skipFocusLossSync && hasFenceHeader) {
        const parsed = parseEditorText(raw)
        if (root.model && root.blockId.length > 0) {
          if (parsed.lang !== root.codeLanguage) {
            root.model.setBlockCodeLanguage(root.blockId, parsed.lang)
          }
          root.model.updateBlockText(root.blockId, parsed.body)
        }
        root.setEditorText(parsed.body)
      }
      root.skipFocusLossSync = false
    }
    internalUpdating = false
  }

  implicitHeight: cardBackground.height + 12

  Rectangle {
    id: cardBackground
    x: 0
    y: 6
    width: root.width
    height: Math.max(editor.implicitHeight + 24, 48)
    radius: 6
    // Theme.codeBlockBackground was removed; use a current palette colour
    // with an explicit fallback for themes that do not define it.
    color: Theme.surfaceAlt || (Theme.isDark ? "#1e1e24" : "#f4f4f6")
    border.color: root.isFocused ? Theme.accent : (Theme.border || (Theme.isDark ? "#2e2e38" : "#e0e0e4"))
    border.width: root.isFocused ? 1.5 : 1

    Behavior on border.color {
      ColorAnimation { duration: 150 }
    }

    // Top-Right Language / Copy controls (visible when not focused, or on hover)
    Row {
      anchors.top: parent.top
      anchors.right: parent.right
      anchors.topMargin: 6
      anchors.rightMargin: 8
      spacing: 6
      z: 10

      // Language badge
      Text {
        anchors.verticalCenter: parent.verticalCenter
        visible: !root.isFocused && (root.codeLanguage && root.codeLanguage.length > 0)
        text: root.codeLanguage.toUpperCase()
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.family: "monospace"
        color: Theme.isDark ? "#8b949e" : "#57606a"
      }

      // Copy Button
      Rectangle {
        id: copyBtn
        width: 24
        height: 24
        radius: 4
        visible: (!root.isFocused && (cardHoverArea.containsMouse || copyBtnArea.containsMouse)) || root.copiedNotification
        color: copyBtnArea.containsMouse ? (Theme.isDark ? "#30363d" : "#e1e4e8") : "transparent"

        Image {
          anchors.centerIn: parent
          width: 14
          height: 14
          sourceSize: Qt.size(14, 14)
          source: root.copiedNotification
                  ? "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_checkbox_checked_20_regular.svg"
                  : "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_copy_20_regular.svg"
        }

        MouseArea {
          id: copyBtnArea
          anchors.fill: parent
          hoverEnabled: true
          cursorShape: Qt.PointingHandCursor
          onClicked: {
            const rawBody = root.isFocused ? parseEditorText(editor.text).body : editor.text
            ClipboardService.setText(rawBody)
            root.copiedNotification = true
            copyResetTimer.restart()
          }
        }

        Timer {
          id: copyResetTimer
          interval: 1500
          onTriggered: root.copiedNotification = false
        }
      }
    }

    TextArea {
      id: editor
      x: 12
      y: 12
      width: parent.width - 24
      text: root.content
      font.family: "JetBrains Mono, Consolas, Fira Code, Source Code Pro, monospace"
      font.pixelSize: 14
      font.letterSpacing: 0.2
      wrapMode: TextArea.Wrap
      textFormat: TextArea.PlainText
      color: Theme.text
      selectByMouse: true
      padding: 0
      topPadding: 0
      bottomPadding: 0
      leftPadding: 0
      rightPadding: 0
      background: null

      Keys.priority: Keys.BeforeItem

      CodeHighlighter {
        id: highlighter
        textDocument: editor.textDocument
        language: root.codeLanguage || ""
      }

      Keys.onTabPressed: function(event) {
        event.accepted = true
        editor.insert(editor.cursorPosition, "  ")
      }

      Keys.onPressed: function(event) {
        // Ctrl + Enter or Cmd + Enter: Exit code block and create a paragraph block below
        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
          event.accepted = true
          exitCodeBlockBelow()
          return
        }

        // Return / Enter key
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
          event.accepted = true

          if (root.isFocused) {
            // Check if cursor is on the closing ``` line
            const currentText = editor.text
            const pos = editor.cursorPosition
            const lastLineIndex = currentText.lastIndexOf("\n")
            // Only exit when the caret is on the closing-fence line itself,
            // i.e. strictly after the newline that precedes it. A caret at
            // lastLineIndex is still at the end of the last code line, where
            // Enter must insert a newline instead.
            if (lastLineIndex >= 0 && pos > lastLineIndex) {
              const lineContent = currentText.substring(lastLineIndex + 1).trim()
              if (lineContent === root.codeFence) {
                exitCodeBlockBelow()
                return
              }
            }
          }

          // Normal newline inside code block
          editor.insert(editor.cursorPosition, "\n")
          return
        }

        // Up arrow: move to block above if on first visual line
        if (event.key === Qt.Key_Up && root.isCursorOnFirstLine()) {
          event.accepted = true
          const cRect = editor.cursorRectangle
          root.moveFocusRequested(-1, cRect.x + editor.x)
          return
        }

        // Down arrow: move to block below if on last visual line
        if (event.key === Qt.Key_Down && root.isCursorOnLastLine()) {
          event.accepted = true
          const cRect = editor.cursorRectangle
          root.moveFocusRequested(1, cRect.x + editor.x)
          return
        }

        if (event.key === Qt.Key_Backspace) {
          // If completely empty or only fence lines left with empty body, backspace deletes or converts block
          const parsed = parseEditorText(editor.text)
          if (parsed.body.length === 0) {
            // Check if cursor is near the start or whole block is effectively empty
            if (editor.cursorPosition <= 3 ||
                editor.text.trim() === root.codeFence ||
                editor.text.trim() === root.codeFence + root.codeFence ||
                parsed.body.length === 0) {
              event.accepted = true
              root.backspacePressed()
              return
            }
          }
        }

        if (event.key === Qt.Key_Z && (event.modifiers & Qt.ControlModifier)) {
          event.accepted = true
          if (event.modifiers & Qt.ShiftModifier) {
            root.redoRequested()
          } else {
            root.undoRequested()
          }
          return
        }
      }

      onTextChanged: {
        if (root.internalUpdating)
          return

        if (root.isFocused) {
          const parsed = root.parseEditorText(editor.text)
          root.internalUpdating = true
          if (parsed.lang !== root.codeLanguage && root.model && root.blockId.length > 0) {
            root.model.setBlockCodeLanguage(root.blockId, parsed.lang)
          }
          if (root.model && root.blockId.length > 0) {
            root.model.updateBlockText(root.blockId, parsed.body)
          }
          root.internalUpdating = false
        } else {
          if (root.model && root.blockId.length > 0) {
            root.internalUpdating = true
            root.model.updateBlockText(root.blockId, editor.text)
            root.internalUpdating = false
          }
        }
      }

      onCursorPositionChanged: {
        root.updateCaretGeometry()
      }
    }

    MouseArea {
      id: cardHoverArea
      anchors.fill: parent
      hoverEnabled: true
      acceptedButtons: Qt.NoButton
    }

    // Clicking the closing ``` line exits the code block instead of just
    // placing the caret on the fence. Positioned over the editor's last line,
    // above the editor in z-order so the click is captured here.
    MouseArea {
      id: closingFenceClickArea
      x: root.closingFenceLineRect.x
      y: root.closingFenceLineRect.y
      width: root.closingFenceLineRect.width
      height: root.closingFenceLineRect.height
      visible: root.isFocused && root.closingFenceLineStart >= 0
      z: 5
      acceptedButtons: Qt.LeftButton
      cursorShape: Qt.PointingHandCursor
      onPressed: function(mouse) { mouse.accepted = true }
      onClicked: root.exitCodeBlockBelow()
    }
  }

  function exitCodeBlockBelow() {
    if (!root.model)
      return

    // Strip the closing ``` if still in editor
    const parsed = parseEditorText(editor.text)
    root.internalUpdating = true
    if (parsed.lang !== root.codeLanguage)
      root.model.setBlockCodeLanguage(root.blockId, parsed.lang)
    root.model.updateBlockText(root.blockId, parsed.body)
    root.skipFocusLossSync = true
    root.setEditorText(parsed.body)
    root.internalUpdating = false

    const newId = root.model.insertBlockAfter(root.blockId, 0, "")
    if (newId.length > 0) {
      Qt.callLater(function() {
        root.focusDelegateRequested(newId, 0)
      })
    }
  }
}
