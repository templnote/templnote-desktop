import QtQuick
import QtQuick.Controls

Item {
  id: root

  // Provided by the BlockRenderer dispatcher.
  property string blockId: ""
  property int blockType: 0
  property string content: ""
  property int headingLevel: 0
  property int numberIndex: 1
  property var model: null

  // Appearance overrides for each concrete block type.
  property bool showBullet: false
  property bool showNumbered: false
  property bool showPlaceholder: false
  property string numberPrefix: numberIndex + "."
  property int verticalPadding: 0
  property int horizontalPadding: 12
  property int rowHeight: 0
  property real fontPixelSize: 16
  property bool fontBold: false

  readonly property int cursorPosition: editor.cursorPosition
  readonly property string editorText: editor.text
  readonly property int selectionStart: editor.selectionStart
  readonly property int selectionEnd: editor.selectionEnd
  readonly property bool isFocused: editor.activeFocus

  property bool transformingShortcut: false
  property bool syncingFromModel: false

  // The formatted RichText is computed imperatively and cached. Focus changes
  // rewrite the same text with/without syntax dimming, so caching avoids a
  // redundant parse + relayout when nothing textual changed.
  property string renderedHtml: ""
  property string renderCacheRaw: ""
  property bool renderCacheSyntax: false
  property bool renderCacheValid: false

  function refreshRenderedHtml() {
    if (!editor)
      return

    const raw = editor.text
    const syntax = editor.activeFocus

    if (renderCacheValid && raw === renderCacheRaw
        && syntax === renderCacheSyntax)
      return

    renderCacheRaw = raw
    renderCacheSyntax = syntax
    renderCacheValid = true
    renderedHtml = markdownToHtml(raw, syntax)
  }

  function findSyntaxTokens(text) {
    if (!text) return []
    let tokens = []
    let uRegex = /<\/?u>/gi
    let m
    while ((m = uRegex.exec(text)) !== null) {
      tokens.push({ start: m.index, end: m.index + m[0].length })
    }
    let bRegex = /\*\*(.+?)\*\*/g
    while ((m = bRegex.exec(text)) !== null) {
      tokens.push({ start: m.index, end: m.index + 2 })
      tokens.push({ start: m.index + m[0].length - 2, end: m.index + m[0].length })
    }
    let sRegex = /~~(.+?)~~/g
    while ((m = sRegex.exec(text)) !== null) {
      tokens.push({ start: m.index, end: m.index + 2 })
      tokens.push({ start: m.index + m[0].length - 2, end: m.index + m[0].length })
    }
    let iRegex = /(?:^|[^*])\*([^*\n]+?)\*(?:[^*]|$)/g
    while ((m = iRegex.exec(text)) !== null) {
      let offset = m[0].startsWith("*") ? 0 : 1
      let sPos = m.index + offset
      let ePos = sPos + 1 + m[1].length
      tokens.push({ start: sPos, end: sPos + 1 })
      tokens.push({ start: ePos, end: ePos + 1 })
    }

    tokens.sort(function(a, b) { return a.start - b.start })
    let clean = []
    let lastEnd = 0
    for (let i = 0; i < tokens.length; ++i) {
      let t = tokens[i]
      if (t.start >= lastEnd) {
        clean.push(t)
        lastEnd = t.end
      }
    }
    return clean
  }

  function markdownToHtml(raw, includeSyntax) {
    if (!raw) return ""
    const spanOpen = "<span style=\"color:" + Theme.textMuted + "; font-weight:normal; font-style:normal; text-decoration:none;\">"
    const spanClose = "</span>"

    let text = raw.replace(/<u>/gi, "\x01U\x01").replace(/<\/u>/gi, "\x01/U\x01")
    text = text.replace(/[&<>]/g, function(character) {
      return character === "&" ? "&amp;" : character === "<" ? "&lt;" : "&gt;"
    })

    if (includeSyntax) {
      // Focused mode: render formatted styling WITH dimmed syntax markers
      text = text.replace(/\*\*(.+?)\*\*/g, spanOpen + "**" + spanClose + "<b>$1</b>" + spanOpen + "**" + spanClose)
      text = text.replace(/~~(.+?)~~/g, spanOpen + "~~" + spanClose + "<s>$1</s>" + spanOpen + "~~" + spanClose)
      text = text.replace(/(?:^|[^*])\*([^*\n]+?)\*(?:[^*]|$)/g, function(match, p1) {
        let prefix = match.startsWith("*") ? "" : match.charAt(0)
        let suffix = match.endsWith("*") ? "" : match.charAt(match.length - 1)
        return prefix + spanOpen + "*" + spanClose + "<i>" + p1 + "</i>" + spanOpen + "*" + spanClose + suffix
      })
      text = text.replace(/\x01U\x01([\s\S]*?)\x01\/U\x01/g, spanOpen + "&lt;u&gt;" + spanClose + "<u>$1</u>" + spanOpen + "&lt;/u&gt;" + spanClose)
      text = text.replace(/\x01U\x01/g, "&lt;u&gt;").replace(/\x01\/U\x01/g, "&lt;/u&gt;")
    } else {
      // Unfocused mode: render formatted styling WITHOUT syntax markers
      text = text.replace(/\x01U\x01/g, "<u>").replace(/\x01\/U\x01/g, "</u>")
      text = text.replace(/\*\*(.+?)\*\*/g, "<b>$1</b>")
      text = text.replace(/~~(.+?)~~/g, "<s>$1</s>")
      text = text.replace(/(?:^|[^*])\*([^*\n]+?)\*(?:[^*]|$)/g, function(match, p1) {
        let prefix = match.startsWith("*") ? "" : match.charAt(0)
        let suffix = match.endsWith("*") ? "" : match.charAt(match.length - 1)
        return prefix + "<i>" + p1 + "</i>" + suffix
      })
    }

    text = text.replace(/\n/g, "<br/>")

    // Preserve whitespace in RichText so multiple consecutive spaces don't collapse
    text = text.replace(/  +/g, function(match) {
      return " " + "&nbsp;".repeat(match.length - 1)
    })

    return text
  }

  function renderedToRawIndex(raw, renderedIndex) {
    if (!raw || renderedIndex <= 0) return 0
    let tokens = findSyntaxTokens(raw)
    let rIdx = 0
    let rawIdx = 0
    let tIdx = 0
    while (rawIdx < raw.length) {
      if (tIdx < tokens.length && rawIdx === tokens[tIdx].start) {
        rawIdx = tokens[tIdx].end
        tIdx++
        continue
      }
      if (rIdx === renderedIndex) {
        return rawIdx
      }
      rIdx++
      rawIdx++
    }
    return raw.length
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
      regex = /<u>([\s\S]*?)<\/u>/gi
    } else if (type === "strike") {
      openTag = "~~"; closeTag = "~~"
      regex = /~~(.+?)~~/g
    }

    const otLen = openTag.length
    const ctLen = closeTag.length

    if (sel.toLowerCase().startsWith(openTag.toLowerCase()) &&
        sel.toLowerCase().endsWith(closeTag.toLowerCase()) &&
        sel.length >= (otLen + ctLen)) {
      return true
    }
    if (s >= otLen && e + ctLen <= raw.length) {
      if (raw.substring(s - otLen, s).toLowerCase() === openTag.toLowerCase() &&
          raw.substring(e, e + ctLen).toLowerCase() === closeTag.toLowerCase()) {
        return true
      }
    }

    if (regex) {
      let match
      while ((match = regex.exec(raw)) !== null) {
        let mStart = match.index
        let mEnd = match.index + match[0].length
        if (type === "italic") {
          if (!match[0].startsWith("*")) mStart += 1
          if (!match[0].endsWith("*")) mEnd -= 1
        }
        if (s >= mStart && e <= mEnd) {
          return true
        }
      }
    }
    return false
  }

  function toggleFormat(type) {
    const raw = editor.text
    const s = Math.min(editor.selectionStart, editor.selectionEnd)
    const e = Math.max(editor.selectionStart, editor.selectionEnd)
    if (s === e) return

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
      regex = /<u>([\s\S]*?)<\/u>/gi
    } else if (type === "strike") {
      openTag = "~~"; closeTag = "~~"
      regex = /~~(.+?)~~/g
    }

    const otLen = openTag.length
    const ctLen = closeTag.length
    let newText = ""
    let newStart = s
    let newEnd = e

    // 1. Selection starts and ends with the tag
    if (sel.toLowerCase().startsWith(openTag.toLowerCase()) &&
        sel.toLowerCase().endsWith(closeTag.toLowerCase()) &&
        sel.length >= (otLen + ctLen)) {
      const inner = sel.substring(otLen, sel.length - ctLen)
      newText = raw.substring(0, s) + inner + raw.substring(e)
      newStart = s
      newEnd = s + inner.length
    }
    // 2. Surrounding characters are the tag
    else if (s >= otLen && e + ctLen <= raw.length &&
             raw.substring(s - otLen, s).toLowerCase() === openTag.toLowerCase() &&
             raw.substring(e, e + ctLen).toLowerCase() === closeTag.toLowerCase()) {
      newText = raw.substring(0, s - otLen) + sel + raw.substring(e + ctLen)
      newStart = s - otLen
      newEnd = e - otLen
    }
    // 3. Selection is inside an existing span
    else if (regex) {
      let matchedSpan = null
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
        if (s >= contentStart && e <= contentEnd) {
          matchedSpan = { mStart: mStart, mEnd: mEnd, contentStart: contentStart, contentEnd: contentEnd }
          break
        }
      }

      if (matchedSpan) {
        if (s === matchedSpan.contentStart && e === matchedSpan.contentEnd) {
          const inner = raw.substring(matchedSpan.contentStart, matchedSpan.contentEnd)
          newText = raw.substring(0, matchedSpan.mStart) + inner + raw.substring(matchedSpan.mEnd)
          newStart = matchedSpan.mStart
          newEnd = matchedSpan.mStart + inner.length
        } else {
          const before = raw.substring(matchedSpan.contentStart, s)
          const after = raw.substring(e, matchedSpan.contentEnd)
          let rebuilt = ""
          if (before.length > 0) rebuilt += openTag + before + closeTag
          rebuilt += sel
          newStart = matchedSpan.mStart + (before.length > 0 ? (otLen + before.length + ctLen) : 0)
          newEnd = newStart + sel.length
          if (after.length > 0) rebuilt += openTag + after + closeTag
          newText = raw.substring(0, matchedSpan.mStart) + rebuilt + raw.substring(matchedSpan.mEnd)
        }
      } else {
        // Wrap
        newText = raw.substring(0, s) + openTag + sel + closeTag + raw.substring(e)
        newStart = s
        newEnd = e + otLen + ctLen
      }
    } else {
      // Wrap
      newText = raw.substring(0, s) + openTag + sel + closeTag + raw.substring(e)
      newStart = s
      newEnd = e + otLen + ctLen
    }

    editor.text = newText
    editor.select(newStart, newEnd)
    if (root.model && root.blockId.length > 0)
      root.model.updateBlockText(root.blockId, newText)
    editor.notifySelection()
  }

  signal enterPressed()
  signal backspacePressed()
  signal focusRequested(int position)
  signal undoRequested()
  signal redoRequested()
  signal moveFocusRequested(int direction, real xPosition)
  signal commandMenuRequested(string query, real xPosition, real yPosition, real cursorHeight)
  signal commandMenuDismissed()
  signal commandMenuAccepted()
  signal commandMenuNavigated(int direction)
  signal caretMoved(int position, real cursorGlobalY, real cursorHeight)
  signal selectionChanged(bool hasSelection, real xPosition, real yPosition, real cursorHeight)

  property bool commandMenuActive: false

  // Search highlight ranges for this block: [{ start, length, active }].
  property var searchHighlights: []

  // Set while the editor is in global-search navigation mode. In that mode
  // Up/Down cycle matches and Escape removes the highlights.
  property bool searchNavigationActive: false

  signal searchNavigated(int direction)
  signal searchDismissed()
  signal searchEditStarted()

  function focusEditor(position) {
    editor.focus = true
    editor.forceActiveFocus()

    if (position < 0)
      editor.cursorPosition = editor.length
    else
      editor.cursorPosition = position

    // A freshly created TextArea can receive focus one event-loop turn after
    // the delegate is created. Re-apply the caret state after that handoff.
    Qt.callLater(function() {
      if (editor.activeFocus) {
        editor.cursorVisible = true
        if (position < 0)
          editor.cursorPosition = editor.length
        else
          editor.cursorPosition = position
      }
    })
  }

  function focusEditorAtX(xPosition) {
    editor.focus = true
    editor.forceActiveFocus()

    const x = Math.max(0, xPosition)
    editor.cursorPosition = editor.positionAt(x, editor.cursorRectangle.y)
    editor.cursorVisible = true
  }

  function focusEditorRange(start, end) {
    editor.focus = true
    editor.forceActiveFocus()

    editor.cursorPosition = start
    editor.select(start, end)
    editor.cursorVisible = true
  }

  function refreshFromModel() {
    editor.text = root.content
  }

  onContentChanged: {
    // The dispatcher assigns the content after the loader has created
    // this item, so keep the editor in sync with the model.
    if (editor.text === root.content)
      return

    root.syncingFromModel = true
    editor.text = root.content
    // Content changes coming from the model are undo/redo (or other
    // programmatic updates), so place the caret after the restored text.
    editor.cursorPosition = editor.length
    root.syncingFromModel = false
    root.refreshRenderedHtml()
  }

  // A recycled delegate can be moved to a different block; drop stale caret
  // and selection state from the previous block.
  onBlockIdChanged: {
    if (!editor)
      return

    editor.cursorPosition = 0
    editor.deselect()
    root.renderCacheValid = false
    root.refreshRenderedHtml()
  }

  onSearchHighlightsChanged: searchHighlightsCanvas.requestPaint()

  Connections {
    target: Theme

    // The dimmed syntax markers embed the muted text colour, so a theme
    // change needs one re-render of the cached HTML.
    function onTextMutedChanged() {
      root.renderCacheValid = false
      root.refreshRenderedHtml()
    }
  }

  implicitHeight: contentColumn.implicitHeight + root.verticalPadding * 2

  // Clicking the block's padding/gutter should move the caret into this block
  // instead of leaving focus on the previously edited block.
  MouseArea {
    anchors.fill: parent
    acceptedButtons: Qt.LeftButton

    onClicked: {
      editor.forceActiveFocus()
      editor.cursorPosition = editor.length
    }
  }

  Column {
    id: contentColumn
    anchors.fill: parent
    anchors.leftMargin: root.horizontalPadding
    anchors.rightMargin: root.horizontalPadding
    anchors.topMargin: root.verticalPadding
    anchors.bottomMargin: root.verticalPadding
    spacing: (root.showBullet || root.showNumbered) ? 0 : 5

    Row {
      width: parent.width
      height: Math.max(root.rowHeight, editor.implicitHeight)
      spacing: 0

      Item {
        // The bullet/number is drawn in its own gutter so the textarea can be
        // offset by the same amount for every block type, keeping the
        // text of all block types aligned.
        id: bulletGutter
        width: 24
        height: root.rowHeight > 0 ? root.rowHeight : editor.implicitHeight

        Text {
          visible: root.showBullet
          anchors.centerIn: parent

          text: "•"
          color: Theme.textMuted
          font.pixelSize: 22

          horizontalAlignment: Text.AlignHCenter
          verticalAlignment: Text.AlignVCenter
        }

        Text {
          visible: root.showNumbered
          anchors.centerIn: parent

          text: root.numberPrefix
          color: Theme.textMuted
          font.pixelSize: 14

          horizontalAlignment: Text.AlignHCenter
          verticalAlignment: Text.AlignVCenter
        }
      }

      Item {
        id: editorLine
        width: parent.width - bulletGutter.width
        height: parent.height
        clip: false

        // Painted behind the text. Canvas lets a single match span several
        // wrapped lines without creating one item per character.
        Canvas {
          id: searchHighlightsCanvas

          anchors.fill: parent
          visible: root.searchHighlights.length > 0
          onWidthChanged: requestPaint()
          onHeightChanged: requestPaint()
          onVisibleChanged: if (visible) requestPaint()

          Connections {
            target: editor

            function onTextChanged() {
              if (searchHighlightsCanvas.visible)
                searchHighlightsCanvas.requestPaint()
            }
          }

          Connections {
            target: Theme

            function onAccentChanged() {
              if (searchHighlightsCanvas.visible)
                searchHighlightsCanvas.requestPaint()
            }
          }

          onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            const highlights = root.searchHighlights

            if (!highlights || highlights.length === 0)
              return

            for (let h = 0; h < highlights.length; ++h) {
              const highlight = highlights[h]

              ctx.fillStyle = highlight.active
                  ? Theme.tint(Theme.accent, 0.40)
                  : Theme.tint(Theme.accent, 0.16)

              let runLeft = -1
              let runRight = -1
              let runTop = 0
              let runHeight = 0

              function flushRun() {
                if (runLeft < 0)
                  return

                ctx.fillRect(runLeft, runTop,
                             Math.max(1, runRight - runLeft), runHeight)
                runLeft = -1
                runRight = -1
              }

              // positionToRectangle() returns a 1px caret rect, not a full
              // character cell. Use the next caret position to find the right
              // edge of each character and merge cells that share a line.
              for (let i = highlight.start;
                   i < highlight.start + highlight.length; ++i) {
                if (editor.text.charAt(i) === "\n")
                  continue

                const rect = editor.positionToRectangle(i)
                const nextRect = editor.positionToRectangle(i + 1)
                const sameLine = Math.abs(nextRect.y - rect.y) < 1
                const cellRight = sameLine
                    ? Math.max(rect.x + 1, nextRect.x)
                    // The final character on a soft-wrapped line extends to
                    // the right edge of the text area.
                    : Math.max(rect.x + 1,
                               editor.width - editor.rightPadding)

                if (runLeft >= 0 && Math.abs(rect.y - runTop) < 1) {
                  runRight = Math.max(runRight, cellRight)
                  runHeight = Math.max(runHeight, rect.height)
                } else {
                  flushRun()

                  runLeft = rect.x
                  runRight = cellRight
                  runTop = rect.y
                  runHeight = rect.height
                }
              }

              flushRun()
            }
          }
        }

        Text {
          id: renderedViewer
          anchors.fill: parent
          anchors.leftMargin: editor.leftPadding
          anchors.rightMargin: editor.rightPadding
          anchors.topMargin: editor.topPadding
          anchors.bottomMargin: editor.bottomMargin
          visible: editor.text.length > 0
          wrapMode: Text.Wrap
          textFormat: Text.RichText
          text: root.renderedHtml
          color: Theme.text
          font.pixelSize: root.fontPixelSize
          font.weight: root.fontBold ? Font.Bold : Font.Normal
          z: 2

          MouseArea {
            anchors.fill: parent
            cursorShape: Qt.IBeamCursor
            preventStealing: false
            propagateComposedEvents: true

            onPressed: function(mouse) {
              const mapped = mapToItem(editor, mouse.x, mouse.y)
              const clickPos = editor.positionAt(mapped.x, mapped.y)
              editor.forceActiveFocus()
              editor.cursorPosition = clickPos
              // Allow mouse event propagation so drag-selection in editor continues
              mouse.accepted = false
            }
          }
        }

        TextArea {
          id: editor
          anchors.fill: parent
          z: 3

          wrapMode: TextArea.Wrap
          placeholderText: root.showPlaceholder ? "start typing here" : ""
          placeholderTextColor: Theme.textFaint

          color: "transparent"
          selectionColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35)
          selectedTextColor: "transparent"
          selectByMouse: true

          // The default caret follows the application palette, which stays
          // dark in dark mode and nearly disappears on the theme background.
          // Use the theme's text colour so it is clearly visible in both modes.
          //
          // Qt only hides a custom cursor delegate when the item moves in the
          // viewport, not when the editor loses focus, so the delegate root is
          // an empty Item and the visible bar is its child, gated on focus to
          // avoid leaving a stale caret behind in every visited block.
          cursorDelegate: Item {
            width: 2

            Rectangle {
              anchors.fill: parent
              radius: 1
              color: Theme.text
              visible: editor.activeFocus && editor.cursorVisible
            }
          }

          font.pixelSize: root.fontPixelSize
          font.weight: root.fontBold ? Font.Bold : Font.Normal

          background: null

          hoverEnabled: true

          function notifySelection() {
            if (editor.selectionStart === editor.selectionEnd) {
              root.selectionChanged(false, 0, 0, 0)
              return
            }
            if (!editor.activeFocus) {
              // Delay notifying false so clicking toolbar controls or popups doesn't immediately dismiss the selection
              return
            }
            const cRect = editor.cursorRectangle
            const mapped = editor.mapToItem(root, cRect.x + cRect.width / 2, cRect.y)
            root.selectionChanged(true, mapped.x, mapped.y, cRect.height)
          }

          onSelectionStartChanged: notifySelection()
          onSelectionEndChanged: notifySelection()

          onCursorPositionChanged: {
            notifySelection()
            if (editor.activeFocus) {
              const cRect = editor.cursorRectangle
              const mapped = editor.mapToItem(null, cRect.x, cRect.y)
              root.caretMoved(editor.cursorPosition, mapped ? mapped.y : 0, cRect.height)
            }
          }

          onActiveFocusChanged: {
            root.refreshRenderedHtml()
            notifySelection()
            if (editor.activeFocus) {
              const cRect = editor.cursorRectangle
              const mapped = editor.mapToItem(null, cRect.x, cRect.y)
              root.caretMoved(editor.cursorPosition, mapped ? mapped.y : 0, cRect.height)
            }
          }

          Component.onCompleted: {
            editor.text = root.content
            root.refreshRenderedHtml()
          }

          onTextChanged: {
            // Keep the formatted overlay in sync even while the model is
            // being updated programmatically.
            root.refreshRenderedHtml()

            if (root.transformingShortcut || root.syncingFromModel)
              return

            // The first real edit ends the global-search navigation session.
            // The signal clears the highlight state before the model sees the
            // new text.
            if (root.searchNavigationActive)
              root.searchEditStarted()

            if (root.blockType === 0 && text.startsWith("/") && text.indexOf(" ") < 0) {
              const slashRect = editor.positionToRectangle(0)
              const mapped = editor.mapToItem(root, slashRect.x, slashRect.y)
              root.commandMenuRequested(text.substring(1), mapped.x, mapped.y, slashRect.height)
            } else if (root.commandMenuActive)
              root.commandMenuDismissed()

            // paragraph -> bullet
            if (root.blockType === 0 && text.startsWith("- ")) {
              root.transformingShortcut = true

              const bulletText = text.substring(2)

              root.model.changeBlockType(
                root.blockId,
                2,
                0
              )

              root.model.updateBlockText(
                root.blockId,
                bulletText
              )

              root.transformingShortcut = false

              // Changing the type makes the dispatcher reload the block
              // renderer, so focus has to move to the new editor.
              root.focusRequested(bulletText.length)
              return
            }

            // paragraph -> numbered list
            const numberedMatch = /^(\d+)\.\s/.exec(text)
            if (root.blockType === 0 && numberedMatch) {
              root.transformingShortcut = true

              const prefixLen = numberedMatch[0].length
              const numberedText = text.substring(prefixLen)

              root.model.changeBlockType(
                root.blockId,
                3,
                0
              )

              root.model.updateBlockText(
                root.blockId,
                numberedText
              )

              root.transformingShortcut = false

              root.focusRequested(numberedText.length)
              return
            }

            // paragraph -> heading
            if (root.blockType === 0) {
              const headingMatch = /^(#{1,6}) /.exec(text)

              if (headingMatch) {
                root.transformingShortcut = true

                const headingLevel = headingMatch[1].length
                const headingText = text.substring(headingLevel + 1)

                root.model.changeBlockType(
                  root.blockId,
                  1,
                  headingLevel
                )

                root.model.updateBlockText(
                  root.blockId,
                  headingText
                )

                root.transformingShortcut = false

                // Changing the type makes the dispatcher reload the block
                // renderer, so focus has to move to the new editor.
                root.focusRequested(headingText.length)
                return
              }
            }

            // Every normal text change is sent to the model immediately so
            // undo/redo can operate on characters/words instead of whole
            // blocks. BlockModel groups consecutive keystrokes for us.
            if (root.model && root.blockId.length > 0)
              root.model.updateBlockText(root.blockId, text)
          }

          Keys.onPressed: function(event) {
            if (root.commandMenuActive) {
              if (event.key === Qt.Key_Escape) {
                event.accepted = true
                root.commandMenuDismissed()
                return
              }
              if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                event.accepted = true
                root.commandMenuAccepted()
                return
              }
              if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                event.accepted = true
                root.commandMenuNavigated(event.key === Qt.Key_Up ? -1 : 1)
                return
              }
            }
            // Global-search navigation keeps focus in the document, so the
            // block itself has to own the match navigation keys.
            if (root.searchNavigationActive) {
              if (event.key === Qt.Key_Escape) {
                event.accepted = true
                // Drop the selection the active match left behind while
                // keeping the caret where it was.
                editor.deselect()
                root.searchDismissed()
                return
              }
              if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                event.accepted = true
                root.searchNavigated(event.key === Qt.Key_Up ? -1 : 1)
                return
              }
              if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                root.searchEditStarted()
            }

            // Keyboard shortcuts for inline markdown formatting
            if (event.modifiers & Qt.ControlModifier) {
              if (event.key === Qt.Key_B) {
                event.accepted = true
                root.toggleFormat("bold")
                return
              }
              if (event.key === Qt.Key_I) {
                event.accepted = true
                root.toggleFormat("italic")
                return
              }
              if (event.key === Qt.Key_U) {
                event.accepted = true
                root.toggleFormat("underline")
                return
              }
              if ((event.modifiers & Qt.ShiftModifier) && event.key === Qt.Key_X) {
                event.accepted = true
                root.toggleFormat("strike")
                return
              }
            }

            // TextArea has its own per-editor undo stack, which would only
            // undo the block that currently has focus. Intercept the standard
            // shortcuts and use the document-wide history in BlockModel
            // instead so undo/redo works across every block.
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

            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
              event.accepted = true
              if ((event.modifiers & Qt.ShiftModifier) && root.blockType !== 1) {
                // Shift + Enter inside non-heading blocks inserts a newline within the same block
                editor.insert(editor.cursorPosition, "\n")
              } else {
                // Regular Enter, or Shift + Enter on Headings, splits into next block
                root.enterPressed()
              }
              return
            }

            if (event.key === Qt.Key_Backspace && editor.cursorPosition === 0) {
              event.accepted = true
              root.backspacePressed()
              return
            }

            // Alt+Up/Down is reserved for moving the block itself, so let the
            // application shortcut handle it instead of moving focus.
            if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down)
                && !(event.modifiers & Qt.AltModifier)) {
              const cursorRect = editor.cursorRectangle
              const direction = event.key === Qt.Key_Up ? -1 : 1

              event.accepted = true
              root.moveFocusRequested(direction, cursorRect.x)
              return
            }
          }
        }
      }
    }
  }
}
