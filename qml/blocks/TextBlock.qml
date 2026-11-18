import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl

Item {
  id: root

  // Provided by the BlockRenderer dispatcher.
  property string blockId: ""
  property int blockType: 0
  property string content: ""
  property int headingLevel: 0
  property int numberIndex: 1
  property string alignment: "left"
  property var model: null

  // Appearance overrides for each concrete block type.
  property bool showBullet: false
  property bool showNumbered: false
  property bool showTodo: false
  property bool isChecked: false
  property bool showPlaceholder: false
  property string numberPrefix: numberIndex + "."
  property int verticalPadding: 0
  property int horizontalPadding: 12
  property int rowHeight: 0
  property real fontPixelSize: 16
  property bool fontBold: false
  readonly property real firstLineHeight: Math.max(rowHeight, Math.round(fontPixelSize * 1.35))
  readonly property int textAlignment: (root.showBullet || root.showNumbered || root.showTodo)
                                       ? Text.AlignLeft
                                       : (root.alignment === "center"
                                          ? Text.AlignHCenter
                                          : (root.alignment === "right"
                                             ? Text.AlignRight
                                             : Text.AlignLeft))

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
  // Height of the formatted text as shown while the block is not focused.
  // Measured explicitly because a hidden Text's implicitHeight does not
  // reliably notify the delegate's layout before it has been polished.
  property real renderedHeight: 0
  property string renderCacheRaw: ""
  property bool renderCacheSyntax: false
  property bool renderCacheValid: false

  // The last HTML/width combination that was measured. Re-measuring the same
  // content is expensive (forceLayout) and cannot change the height.
  property string measuredHtml: ""
  property real measuredWidth: -1

  // Custom caret geometry, in editorLine coordinates. The real caret belongs
  // to the invisible TextArea, whose raw layout differs from the rendered
  // rich text whenever a <sub>/<sup> span shrinks its contents.
  property real caretOffsetX: 0
  property real caretOffsetY: 0
  property real caretHeight: 0
  property int pendingFocusPosition: 0

  Timer {
    id: measureTimer
    interval: 0
    repeat: false
    onTriggered: root.measureRenderedHeight()
  }

  // Coalesce rapid text changes into one render per event-loop turn so fast
  // typing does not re-parse the whole block for every keystroke.
  Timer {
    id: renderTimer
    interval: 0
    repeat: false
    onTriggered: root.refreshRenderedHtml()
  }

  Timer {
    id: caretGeometryTimer
    interval: 0
    repeat: false
    onTriggered: root.updateCaretGeometry()
  }

  Timer {
    id: focusRestoreTimer
    interval: 0
    repeat: false
    onTriggered: {
      if (!editor.activeFocus)
        return

      editor.cursorVisible = true
      if (root.pendingFocusPosition < 0)
        editor.cursorPosition = editor.length
      else
        editor.cursorPosition = Math.max(
          0, Math.min(root.pendingFocusPosition, editor.length))
      root.updateCaretGeometry()
    }
  }

  function measureRenderedHeight() {
    if (!measureText || !editor || !editorLine)
      return

    function numeric(value) {
      return typeof value === "number" && isFinite(value) ? value : 0
    }

    const leftPadding = numeric(editor.leftPadding)
    const rightPadding = numeric(editor.rightPadding)
    const availableWidth = Math.max(
      0, numeric(editorLine.width) - leftPadding - rightPadding)

    // Measuring before the delegate has a width would produce a single-line
    // height; wait until the layout pass provides one.
    if (availableWidth <= 0)
      return

    if (availableWidth === root.measuredWidth &&
        root.renderedHtml === root.measuredHtml)
      return

    root.measuredWidth = availableWidth
    root.measuredHtml = root.renderedHtml

    measureText.width = availableWidth
    measureText.text = root.renderedHtml
    measureText.forceLayout()

    const height = measureText.contentHeight
                 + numeric(editor.topPadding) + numeric(editor.bottomPadding)

    if (isFinite(height) && height !== root.renderedHeight)
      root.renderedHeight = height
  }

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

    // Measurement is deferred until the current layout pass has settled the
    // available width; measuring here as well would force layout twice.
    measureTimer.restart()
  }

  onIsCheckedChanged: {
    renderCacheValid = false
    refreshRenderedHtml()
  }

  // Supported inline HTML tags. Subscript and superscript keep their real
  // <sub>/<sup> markup so they render at the correct size. Qt shrinks the
  // contents of those tags, which means the visible rich text no longer shares
  // geometry with the raw TextArea. Selection, caret and pointer positions are
  // therefore read from the rendered viewer instead of the TextArea.
  function isInlineTagFormat(type) {
    return type === "underline" || type === "subscript" || type === "superscript"
  }

  function inlineTagType(type) {
    if (type === "underline")
      return "u"
    if (type === "subscript")
      return "sub"
    if (type === "superscript")
      return "sup"
    return ""
  }

  // Nesting-aware scan of <u>, <sub> and <sup>. A close tag only pairs with
  // the most recent matching open tag, so crossed or unclosed tags are left
  // unmatched and rendered as literal text instead of producing invalid HTML.
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

  function buildTagTree(text) {
    const parsed = parseInlineTags(text)
    const spans = parsed.spans.slice()
    spans.sort(function(a, b) {
      if (a.openStart !== b.openStart)
        return a.openStart - b.openStart
      return b.closeEnd - a.closeEnd
    })

    const root = []
    const stack = [{ children: root, end: text.length }]

    for (let i = 0; i < spans.length; ++i) {
      const span = spans[i]
      while (stack.length > 1 && span.openStart >= stack[stack.length - 1].end)
        stack.pop()

      const node = { span: span, children: [] }
      stack[stack.length - 1].children.push(node)
      stack.push({ children: node.children, end: span.closeEnd })
    }

    return { parsed: parsed, root: root }
  }

  function escapeInlineText(text) {
    return text.replace(/[&<>]/g, function(character) {
      return character === "&" ? "&amp;" : character === "<" ? "&lt;" : "&gt;"
    })
  }

  function renderInlineText(text, includeSyntax) {
    if (!includeSyntax)
      return escapeInlineText(text)

    // Unmatched tags are literal text, but still look like dimmed syntax
    // markers while the block is focused.
    const spanOpen = "<span style=\"color:" + Theme.textMuted + "; font-weight:normal; font-style:normal; text-decoration:none;\">"
    const spanClose = "</span>"
    const tagRegex = /<\/?(?:u|sub|sup)>/gi
    let out = ""
    let pos = 0
    let match

    while ((match = tagRegex.exec(text)) !== null) {
      out += escapeInlineText(text.substring(pos, match.index))
      out += spanOpen + escapeInlineText(match[0]) + spanClose
      pos = match.index + match[0].length
    }
    out += escapeInlineText(text.substring(pos))
    return out
  }

  function tagOpenHtml(type, includeSyntax) {
    const spanOpen = "<span style=\"color:" + Theme.textMuted + "; font-weight:normal; font-style:normal; text-decoration:none;\">"
    const spanClose = "</span>"
    const marker = spanOpen + "&lt;" + type + "&gt;" + spanClose

    if (type === "u") {
      return (includeSyntax ? marker : "") + "<u>"
    }
    const openTag = type === "sup" ? "<sup>" : "<sub>"
    return (includeSyntax ? marker : "") + openTag
  }

  function tagCloseHtml(type, includeSyntax) {
    const spanOpen = "<span style=\"color:" + Theme.textMuted + "; font-weight:normal; font-style:normal; text-decoration:none;\">"
    const spanClose = "</span>"
    const marker = spanOpen + "&lt;/" + type + "&gt;" + spanClose
    let closingTag = "</u>"
    if (type === "sub")
      closingTag = "</sub>"
    else if (type === "sup")
      closingTag = "</sup>"
    return closingTag + (includeSyntax ? marker : "")
  }

  function renderTagRange(text, start, end, nodes, includeSyntax) {
    let out = ""
    let pos = start

    for (let i = 0; i < nodes.length; ++i) {
      const node = nodes[i]
      const span = node.span
      out += renderInlineText(text.substring(pos, span.openStart), includeSyntax)
      out += tagOpenHtml(span.type, includeSyntax)
      out += renderTagRange(text, span.contentStart, span.contentEnd,
                            node.children, includeSyntax)
      out += tagCloseHtml(span.type, includeSyntax)
      pos = span.closeEnd
    }

    out += renderInlineText(text.substring(pos, end), includeSyntax)
    return out
  }

  function findSyntaxTokens(text) {
    if (!text) return []
    let tokens = []
    const parsedTags = parseInlineTags(text)
    for (let i = 0; i < parsedTags.tokens.length; ++i) {
      const token = parsedTags.tokens[i]
      tokens.push({ start: token.start, end: token.end })
    }
    let m
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

    // Fast path: without inline tags or markdown markers the tag tree and
    // regex pipeline cannot add any styling, so only escaping, line breaks
    // and whitespace preservation are needed. This is the common case while
    // typing ordinary prose.
    if (raw.indexOf("*") < 0 && raw.indexOf("~") < 0 &&
        raw.indexOf("<") < 0 && raw.indexOf(">") < 0) {
      let plain = escapeInlineText(raw)
      plain = plain.replace(/\n/g, "<br/>")
      plain = plain.replace(/  +/g, function(match) {
        return " " + "&nbsp;".repeat(match.length - 1)
      })
      if (root.showTodo && root.isChecked)
        plain = "<s>" + plain + "</s>"
      return plain
    }

    const spanOpen = "<span style=\"color:" + Theme.textMuted + "; font-weight:normal; font-style:normal; text-decoration:none;\">"
    const spanClose = "</span>"

    // Render the inline HTML tags first so nested <sub>/<sup>/<u> pairs are
    // balanced. Their contents are escaped before the markdown markers are
    // styled below.
    const tree = buildTagTree(raw)
    let text = renderTagRange(raw, 0, raw.length, tree.root, includeSyntax)

    if (includeSyntax) {
      // Focused mode: render formatted styling WITH dimmed syntax markers
      text = text.replace(/\*\*(.+?)\*\*/g, spanOpen + "**" + spanClose + "<b>$1</b>" + spanOpen + "**" + spanClose)
      text = text.replace(/~~(.+?)~~/g, spanOpen + "~~" + spanClose + "<s>$1</s>" + spanOpen + "~~" + spanClose)
      text = text.replace(/(?:^|[^*])\*([^*\n]+?)\*(?:[^*]|$)/g, function(match, p1) {
        let prefix = match.startsWith("*") ? "" : match.charAt(0)
        let suffix = match.endsWith("*") ? "" : match.charAt(match.length - 1)
        return prefix + spanOpen + "*" + spanClose + "<i>" + p1 + "</i>" + spanOpen + "*" + spanClose + suffix
      })
    } else {
      // Unfocused mode: render formatted styling WITHOUT syntax markers
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

    if (root.showTodo && root.isChecked) {
      text = "<s>" + text + "</s>"
    }

    return text
  }

  // The rendered viewer owns the visible layout, so every position query has
  // to go through it. Its text contains the dimmed syntax markers while the
  // block is focused, which keeps rendered positions aligned with the raw
  // editor positions.
  function viewerCaretRect(position) {
    if (!renderedViewer || !editor)
      return Qt.rect(0, 0, 1, 0)

    const maxPosition = Math.min(editor.length, renderedViewer.length)
    const clamped = Math.max(0, Math.min(position, maxPosition))
    if (renderedViewer.length === 0)
      return Qt.rect(renderedViewer.x, renderedViewer.y, 1,
                     Math.max(1, renderedViewer.contentHeight))

    const rect = renderedViewer.positionToRectangle(clamped)
    return Qt.rect(renderedViewer.x + rect.x,
                   renderedViewer.y + rect.y,
                   Math.max(1, rect.width), rect.height)
  }

  function updateCaretGeometry() {
    if (!renderedViewer || !editor)
      return

    const rect = root.viewerCaretRect(editor.cursorPosition)
    caretOffsetX = rect.x
    caretOffsetY = rect.y
    caretHeight = rect.height
  }

  function viewerPositionAt(x, y) {
    if (!renderedViewer)
      return 0

    return renderedViewer.positionAt(x - renderedViewer.x, y - renderedViewer.y)
  }

  // Paint a raw-text range using the rendered viewer's rich-text geometry.
  // Both the search highlights and the custom selection highlight use this so
  // scaled inline formatting (such as <sub>/<sup>) stays aligned with the
  // visible text instead of the invisible TextArea's raw layout.
  function paintRange(ctx, start, end, color) {
    if (!renderedViewer || end <= start)
      return

    ctx.fillStyle = color

    const offsetX = renderedViewer.x
    const offsetY = renderedViewer.y
    const rightEdge = renderedViewer.x + renderedViewer.width
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

    // positionToRectangle() returns a 1px caret rect, not a full character
    // cell. Use the next caret position to find the right edge of each
    // character and merge cells that share a line.
    for (let i = start; i < end; ++i) {
      if (editor.text.charAt(i) === "\n")
        continue

      const rect = renderedViewer.positionToRectangle(i)
      const nextRect = renderedViewer.positionToRectangle(i + 1)
      const sameLine = Math.abs(nextRect.y - rect.y) < 1
      const cellRight = (sameLine
          ? Math.max(rect.x + 1, nextRect.x)
          // The final character on a soft-wrapped line extends to the right
          // edge of the rendered text.
          : Math.max(rect.x + 1, rightEdge - offsetX)) + offsetX

      if (runLeft >= 0 && Math.abs(rect.y + offsetY - runTop) < 1) {
        runRight = Math.max(runRight, cellRight)
        runHeight = Math.max(runHeight, rect.height)
      } else {
        flushRun()

        runLeft = rect.x + offsetX
        runRight = cellRight
        runTop = rect.y + offsetY
        runHeight = rect.height
      }
    }

    flushRun()
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
    if (s >= otLen && e + ctLen <= raw.length) {
      if (raw.substring(s - otLen, s).toLowerCase() === openTag.toLowerCase() &&
          raw.substring(e, e + ctLen).toLowerCase() === closeTag.toLowerCase()) {
        return true
      }
    }

    if (isInlineTagFormat(type)) {
      const tagName = inlineTagType(type)
      const parsed = parseInlineTags(raw)
      for (let i = 0; i < parsed.spans.length; ++i) {
        const span = parsed.spans[i]
        if (span.type === tagName &&
            s >= span.contentStart && e <= span.contentEnd) {
          return true
        }
      }
      return false
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

  function commitEditorText(newText, newStart, newEnd) {
    editor.text = newText
    editor.select(newStart, newEnd)
    if (root.model && root.blockId.length > 0)
      root.model.updateBlockText(root.blockId, newText)
    editor.notifySelection()
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

    // Collapsed caret: subscript/superscript apply to the text typed next by
    // inserting an empty tag pair with the caret inside it. Running the toggle
    // again from between the tags removes the pair again.
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
    // 3. Selection is inside an existing <u>/<sub>/<sup> span
    else if (isInlineTagFormat(type)) {
      const tagName = inlineTagType(type)
      const parsed = parseInlineTags(raw)
      let matchedSpan = null

      // Spans are produced inner-first, so the first containing span is the
      // innermost one.
      for (let i = 0; i < parsed.spans.length; ++i) {
        const span = parsed.spans[i]
        if (span.type === tagName &&
            s >= span.contentStart && e <= span.contentEnd) {
          matchedSpan = span
          break
        }
      }

      if (matchedSpan) {
        if (s === matchedSpan.contentStart && e === matchedSpan.contentEnd) {
          const inner = raw.substring(matchedSpan.contentStart, matchedSpan.contentEnd)
          newText = raw.substring(0, matchedSpan.openStart) + inner +
                    raw.substring(matchedSpan.closeEnd)
          newStart = matchedSpan.openStart
          newEnd = matchedSpan.openStart + inner.length
        } else {
          const before = raw.substring(matchedSpan.contentStart, s)
          const after = raw.substring(e, matchedSpan.contentEnd)
          let rebuilt = ""
          if (before.length > 0)
            rebuilt += openTag + before + closeTag
          rebuilt += sel
          newStart = matchedSpan.openStart +
                     (before.length > 0 ? (otLen + before.length + ctLen) : 0)
          newEnd = newStart + sel.length
          if (after.length > 0)
            rebuilt += openTag + after + closeTag
          newText = raw.substring(0, matchedSpan.openStart) + rebuilt +
                    raw.substring(matchedSpan.closeEnd)
        }
      } else {
        // Wrap
        newText = raw.substring(0, s) + openTag + sel + closeTag + raw.substring(e)
        newStart = s
        newEnd = e + otLen + ctLen
      }
    }
    // 4. Selection is inside an existing markdown span
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

    commitEditorText(newText, newStart, newEnd)
  }

  // Remove all <sub>/<sup> spans of the given type that are fully contained
  // in [s, e]. Used to enforce mutual exclusivity when a selection contains an
  // opposite script span rather than sitting entirely inside it.
  function stripContainedTagFormat(type, s, e) {
    const raw = editor.text
    const tagName = inlineTagType(type)
    if (tagName.length === 0)
      return false

    const parsed = parseInlineTags(raw)
    let removals = []
    let removedLength = 0

    for (let i = 0; i < parsed.spans.length; ++i) {
      const span = parsed.spans[i]
      if (span.type !== tagName || span.openStart < s || span.closeEnd > e)
        continue

      removals.push({ start: span.openStart, end: span.openEnd })
      removals.push({ start: span.closeStart, end: span.closeEnd })
      removedLength += (span.openEnd - span.openStart) +
                       (span.closeEnd - span.closeStart)
    }

    if (removals.length === 0)
      return false

    removals.sort(function(a, b) { return b.start - a.start })
    let newText = raw
    for (let i = 0; i < removals.length; ++i)
      newText = newText.substring(0, removals[i].start) +
                newText.substring(removals[i].end)

    commitEditorText(newText, s, e - removedLength)
    return true
  }

  function toggleScriptFormat(type) {
    const raw = editor.text
    const s = Math.min(editor.selectionStart, editor.selectionEnd)
    const e = Math.max(editor.selectionStart, editor.selectionEnd)

    // Turning the active script format off should not disturb the other one.
    if (s !== e && isFormatActive(type)) {
      toggleFormat(type)
      return
    }

    const other = type === "subscript" ? "superscript" : "subscript"
    if (s !== e) {
      if (isFormatActive(other))
        toggleFormat(other)
      else
        stripContainedTagFormat(other, s, e)
    }

    toggleFormat(type)
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

  function notifySelection() {
    if (editor)
      editor.notifySelection()
  }

  function focusEditor(position) {
    editor.focus = true
    editor.forceActiveFocus()

    const safePosition = position < 0
                         ? editor.length
                         : Math.max(0, Math.min(position, editor.length))

    if (position < 0)
      editor.cursorPosition = editor.length
    else
      editor.cursorPosition = safePosition

    root.updateCaretGeometry()

    // A freshly created TextArea can receive focus one event-loop turn after
    // the delegate is created. Re-apply the caret state after that handoff.
    root.pendingFocusPosition = position
    focusRestoreTimer.restart()
  }

  function focusEditorAtX(xPosition) {
    editor.focus = true
    editor.forceActiveFocus()
    editor.cursorVisible = true

    const x = Math.max(0, xPosition)
    const y = root.viewerCaretRect(editor.cursorPosition).y + 1
    editor.cursorPosition = root.viewerPositionAt(x, y)
    root.updateCaretGeometry()
  }

  function focusEditorRange(start, end) {
    editor.focus = true
    editor.forceActiveFocus()

    const safeStart = Math.max(0, Math.min(start, editor.length))
    const safeEnd = Math.max(safeStart, Math.min(end, editor.length))
    editor.cursorPosition = safeStart
    editor.select(safeStart, safeEnd)
    editor.cursorVisible = true
    root.updateCaretGeometry()
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

  onAlignmentChanged: {
    const oldCaretX = renderedViewer ? renderedViewer.positionToRectangle(0).x : 0
    root.updateCaretGeometry()
    caretGeometryTimer.restart()
    selectionCanvas.requestPaint()
    root.notifySelection()

    if (renderedViewer) {
      const newCaretX = renderedViewer.positionToRectangle(0).x
      const deltaX = oldCaretX - newCaretX
      if (Math.abs(deltaX) > 1) {
        slideAnimation.stop()
        slideAnimation.from = deltaX
        slideAnimation.to = 0
        slideAnimation.start()
      }
    }
  }

  NumberAnimation {
    id: slideAnimation
    target: slideLayer
    property: "x"
    duration: 300
    easing.type: Easing.InOutCubic
    onRunningChanged: {
      if (!running) {
        slideLayer.x = 0
      }
    }
  }

  onSearchHighlightsChanged: searchHighlightsCanvas.requestPaint()

  onRenderedHtmlChanged: {
    root.updateCaretGeometry()
    // The viewer's text binding may settle after this notification.
    caretGeometryTimer.restart()
  }

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
  // instead of leaving focus on the previously edited block. The area extends
  // a little into the left gutter so clicks next to the text still land here.
  MouseArea {
    id: paddingArea

    anchors.fill: parent
    anchors.leftMargin: -24
    acceptedButtons: Qt.LeftButton

    onClicked: {
      editor.forceActiveFocus()
      editor.cursorVisible = true

      // Place the caret on the clicked line, as close to the click as the
      // rendered text allows.
      const point = paddingArea.mapToItem(slideLayer, mouse.x, mouse.y)
      editor.cursorPosition = root.viewerPositionAt(point.x, point.y)
      root.updateCaretGeometry()
    }
  }

  Column {
    id: contentColumn
    anchors.fill: parent
    anchors.leftMargin: root.horizontalPadding
    anchors.rightMargin: root.horizontalPadding
    anchors.topMargin: root.verticalPadding
    anchors.bottomMargin: root.verticalPadding
    spacing: (root.showBullet || root.showNumbered || root.showTodo) ? 0 : 5

    Row {
      width: parent.width
      height: {
        const textH = editor.activeFocus
            ? editor.implicitHeight
            : (editor.text.length > 0
               ? root.renderedHeight
               : editor.implicitHeight)
        return Math.max(root.rowHeight, textH)
      }
      spacing: 0

      Item {
        id: bulletGutter
        width: (root.showBullet || root.showNumbered || root.showTodo) ? 24 : 0
        visible: width > 0
        height: root.firstLineHeight

        Text {
          visible: root.showBullet
          anchors.left: parent.left
          anchors.verticalCenter: parent.verticalCenter

          text: "•"
          color: Theme.textMuted
          font.pixelSize: 22

          horizontalAlignment: Text.AlignLeft
          verticalAlignment: Text.AlignVCenter
        }

        Text {
          visible: root.showNumbered
          anchors.left: parent.left
          anchors.verticalCenter: parent.verticalCenter

          text: root.numberPrefix
          color: Theme.textMuted
          font.pixelSize: 14

          horizontalAlignment: Text.AlignLeft
          verticalAlignment: Text.AlignVCenter
        }

        Item {
          id: todoGutterItem
          visible: root.showTodo
          anchors.left: parent.left
          anchors.verticalCenter: parent.verticalCenter
          width: 18
          height: 18

          IconImage {
            id: checkboxIcon
            anchors.centerIn: parent
            width: 18
            height: 18
            sourceSize.width: 18
            sourceSize.height: 18
            source: root.isChecked
                    ? "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_checkbox_checked_20_filled.svg"
                    : "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_checkbox_unchecked_20_regular.svg"
            color: (root.showTodo && root.isChecked) ? Theme.textMuted : Theme.text
          }

          MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
              if (root.model && root.blockId.length > 0) {
                root.model.setBlockChecked(root.blockId, !root.isChecked)
              }
            }
          }
        }
      }

      Item {
        id: editorLine
        width: parent.width - bulletGutter.width
        height: parent.height
        clip: false

        onWidthChanged: {
          root.measureRenderedHeight()
          root.updateCaretGeometry()
        }

        // Off-layout measuring item for the formatted (syntax-free) text.
        // It mirrors the visible viewer's typography and wrapping width.
        Text {
          id: measureText
          width: 0
          visible: false
          wrapMode: Text.Wrap
          textFormat: Text.RichText
          text: ""
          horizontalAlignment: root.textAlignment
          font.pixelSize: root.fontPixelSize
          font.weight: root.fontBold ? Font.Bold : Font.Normal
        }

        Item {
          id: slideLayer
          anchors.fill: parent
          x: 0

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

          Connections {
            target: root

            function onRenderedHtmlChanged() {
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
              const color = highlight.active
                  ? Theme.tint(Theme.accent, 0.40)
                  : Theme.tint(Theme.accent, 0.16)
              root.paintRange(ctx, highlight.start,
                              highlight.start + highlight.length, color)
            }
          }
        }

        // The native TextArea selection follows the raw layout, which drifts
        // away from the visible text after a shrunk <sub>/<sup>. Draw the
        // selection from the rendered viewer instead.
        Canvas {
          id: selectionCanvas

          anchors.fill: parent
          z: 1
          visible: editor.activeFocus &&
                   editor.selectionStart !== editor.selectionEnd
          onWidthChanged: requestPaint()
          onHeightChanged: requestPaint()
          onVisibleChanged: if (visible) requestPaint()

          Connections {
            target: editor

            function onSelectionStartChanged() {
              if (selectionCanvas.visible)
                selectionCanvas.requestPaint()
            }

            function onSelectionEndChanged() {
              if (selectionCanvas.visible)
                selectionCanvas.requestPaint()
            }

            function onActiveFocusChanged() {
              if (selectionCanvas.visible)
                selectionCanvas.requestPaint()
            }
          }

          Connections {
            target: root

            function onRenderedHtmlChanged() {
              if (selectionCanvas.visible)
                selectionCanvas.requestPaint()
            }
          }

          onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            const start = Math.min(editor.selectionStart, editor.selectionEnd)
            const end = Math.max(editor.selectionStart, editor.selectionEnd)
            if (start === end)
              return

            root.paintRange(ctx, start, end,
                            Qt.rgba(Theme.accent.r, Theme.accent.g,
                                    Theme.accent.b, 0.35))
          }
        }

        // TextEdit is used instead of Text because it exposes positionAt() and
        // positionToRectangle(), which let the overlay map pointer and caret
        // positions through the rich-text layout.
        TextEdit {
          id: renderedViewer
          anchors.fill: parent
          anchors.leftMargin: editor.leftPadding
          anchors.rightMargin: editor.rightPadding
          anchors.topMargin: editor.topPadding
          anchors.bottomMargin: editor.bottomPadding
          visible: editor.text.length > 0
          wrapMode: TextEdit.Wrap
          textFormat: TextEdit.RichText
          text: root.renderedHtml
          horizontalAlignment: root.textAlignment
          color: (root.showTodo && root.isChecked) ? Theme.textMuted : Theme.text
          font.pixelSize: root.fontPixelSize
          font.weight: root.fontBold ? Font.Bold : Font.Normal
          readOnly: true
          selectByMouse: false
          selectByKeyboard: false
          cursorVisible: false
          activeFocusOnPress: false
          padding: 0
          leftPadding: 0
          rightPadding: 0
          topPadding: 0
          bottomPadding: 0
          textMargin: 0
          z: 2

          onContentHeightChanged: root.updateCaretGeometry()
        }

        // The caret belongs to the invisible TextArea, so draw our own at the
        // matching rich-text position.
        // Uses GPU-accelerated smooth flow (subtle spring/ease glide) and
        // smooth sinusoidal breathing pulse when idle.
        Rectangle {
          id: customCaret

          x: root.caretOffsetX
          y: root.caretOffsetY
          width: 2
          height: Math.max(1, root.caretHeight)
          radius: 1
          color: Theme.text
          visible: editor.activeFocus && editor.cursorVisible &&
                   editor.selectionStart === editor.selectionEnd
          z: 5

          // Smooth glide/flow when moving across characters or lines
          Behavior on x {
            enabled: editor.activeFocus
            NumberAnimation {
              duration: 75
              easing.type: Easing.OutCubic
            }
          }

          Behavior on y {
            enabled: editor.activeFocus
            NumberAnimation {
              duration: 90
              easing.type: Easing.OutCubic
            }
          }

          Behavior on height {
            NumberAnimation {
              duration: 90
              easing.type: Easing.OutCubic
            }
          }

          // Smooth sinusoidal breathing pulse (GPU animated opacity)
          SequentialAnimation on opacity {
            id: caretPulseAnim
            running: customCaret.visible
            loops: Animation.Infinite

            // Hold full visibility briefly at peak
            PauseAnimation { duration: 180 }
            // Smooth sinusoidal fade out
            NumberAnimation {
              to: 0.15
              duration: 420
              easing.type: Easing.InOutSine
            }
            // Smooth sinusoidal fade in
            NumberAnimation {
              to: 1.0
              duration: 400
              easing.type: Easing.InOutSine
            }
          }

          // Reset opacity to 1.0 immediately whenever typing or navigating
          Connections {
            target: editor
            function onCursorPositionChanged() {
              customCaret.opacity = 1.0
              caretPulseAnim.restart()
            }
          }
        }

        // The TextArea still owns text input and keyboard navigation, but all
        // pointer selection is mapped through the rendered viewer.
        MouseArea {
          id: textMouseArea

          anchors.fill: parent
          z: 4
          cursorShape: Qt.IBeamCursor
          acceptedButtons: Qt.LeftButton
          property int anchorPosition: -1

          function positionFor(x, y) {
            return root.viewerPositionAt(x, y)
          }

          function wordRangeAt(position) {
            const text = editor.text
            function isWordChar(character) {
              return /[A-Za-z0-9_'\u00C0-\uFFFF]/.test(character)
            }

            let start = Math.max(0, Math.min(position, text.length))
            let end = start

            while (start > 0 && isWordChar(text.charAt(start - 1)))
              start--
            while (end < text.length && isWordChar(text.charAt(end)))
              end++

            if (start === end)
              return { start: position, end: position }

            return { start: start, end: end }
          }

          onPressed: function(mouse) {
            editor.forceActiveFocus()
            editor.cursorVisible = true

            const position = positionFor(mouse.x, mouse.y)
            anchorPosition = position
            editor.select(position, position)
          }

          onPositionChanged: function(mouse) {
            if (!pressed || anchorPosition < 0)
              return

            const position = positionFor(mouse.x, mouse.y)
            editor.select(anchorPosition, position)
          }

          onReleased: anchorPosition = -1

          onDoubleClicked: function(mouse) {
            const range = wordRangeAt(positionFor(mouse.x, mouse.y))
            anchorPosition = -1
            editor.select(range.start, range.end)
          }
        }

        TextArea {
          id: editor
          anchors.fill: parent
          z: 3

          padding: 0
          leftPadding: 0
          rightPadding: 0
          topPadding: 0
          bottomPadding: 0

          wrapMode: TextArea.Wrap
          horizontalAlignment: root.textAlignment
          placeholderText: root.showPlaceholder ? "start typing here" : ""
          placeholderTextColor: Theme.textFaint

          color: "transparent"
          selectionColor: "transparent"
          selectedTextColor: "transparent"
          selectByMouse: false

          // Selection and caret are drawn over the rendered viewer, so the
          // native ones must not paint on top of the rich text.
          cursorDelegate: Item {
            width: 0
            height: 0
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
            const cRect = root.viewerCaretRect(editor.cursorPosition)
            const mapped = slideLayer.mapToItem(
                             root, cRect.x + cRect.width / 2, cRect.y)
            root.selectionChanged(true, mapped.x, mapped.y, cRect.height)
          }

          onSelectionStartChanged: notifySelection()
          onSelectionEndChanged: notifySelection()

          onCursorPositionChanged: {
            root.updateCaretGeometry()
            notifySelection()
            if (editor.activeFocus) {
              const cRect = root.viewerCaretRect(editor.cursorPosition)
              const mapped = slideLayer.mapToItem(null, cRect.x, cRect.y)
              root.caretMoved(editor.cursorPosition, mapped ? mapped.y : 0,
                              cRect.height)
            }
          }

          onActiveFocusChanged: {
            root.refreshRenderedHtml()
            root.updateCaretGeometry()
            notifySelection()
            if (editor.activeFocus) {
              const cRect = root.viewerCaretRect(editor.cursorPosition)
              const mapped = slideLayer.mapToItem(null, cRect.x, cRect.y)
              root.caretMoved(editor.cursorPosition, mapped ? mapped.y : 0,
                              cRect.height)
            }
          }

          Component.onCompleted: {
            editor.text = root.content
            root.refreshRenderedHtml()
          }

          onTextChanged: {
            // Keep the formatted overlay in sync even while the model is
            // being updated programmatically. Coalesce bursts of edits.
            renderTimer.restart()

            if (root.transformingShortcut || root.syncingFromModel)
              return

            // The first real edit ends the global-search navigation session.
            // The signal clears the highlight state before the model sees the
            // new text.
            if (root.searchNavigationActive)
              root.searchEditStarted()

            if (root.blockType === 0 && text.startsWith("/") && text.indexOf(" ") < 0) {
              const slashRect = root.viewerCaretRect(0)
              const mapped = slideLayer.mapToItem(root, slashRect.x, slashRect.y)
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

            // paragraph -> todo list ([] or [ ])
            const todoMatch = /^\[ ?\]\s/.exec(text)
            if (root.blockType === 0 && todoMatch) {
              root.transformingShortcut = true

              const prefixLen = todoMatch[0].length
              const todoText = text.substring(prefixLen)

              root.model.changeBlockType(
                root.blockId,
                4, // BlockType::Todo
                0
              )

              root.model.updateBlockText(
                root.blockId,
                todoText
              )

              root.transformingShortcut = false

              root.focusRequested(todoText.length)
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
              if (event.key === Qt.Key_Comma) {
                event.accepted = true
                root.toggleScriptFormat("subscript")
                return
              }
              if (event.key === Qt.Key_Period) {
                event.accepted = true
                root.toggleScriptFormat("superscript")
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
              const cursorRect = root.viewerCaretRect(editor.cursorPosition)
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
}
