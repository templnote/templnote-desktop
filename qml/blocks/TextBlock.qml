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
  // Document-scoped link reference definitions, supplied by the editor.
  property var linkDefinitions: ({})
  property var footnoteDefinitions: ({})
  property var editorApi: null

  // Appearance overrides for each concrete block type.
  property bool showBullet: false
  property bool showNumbered: false
  property bool showTodo: false
  property bool showQuote: false
  property bool isChecked: false
  property string calloutType: "note"
  property var blockMetadata: null
  property bool showPlaceholder: false
  property string numberPrefix: numberIndex + "."
  property int verticalPadding: 0
  property int horizontalPadding: 12
  property int rowHeight: 0
  property real fontPixelSize: 16
  property bool fontBold: false
  readonly property real firstLineHeight: Math.max(rowHeight, Math.round(fontPixelSize * 1.35))
  readonly property int textAlignment: (root.showBullet || root.showNumbered || root.showTodo || root.showQuote)
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

  // Block-level markdown markers are view-only decorations. They are shown
  // dimmed while the block is focused and hidden otherwise; the stored text
  // never contains them. Code and callout blocks render their own raw source.
  readonly property string blockMarker: {
    if (root.blockType === 1)
      return "#".repeat(Math.max(1, Math.min(6, root.headingLevel))) + " "
    if (root.showNumbered)
      return root.numberIndex + ". "
    if (root.showTodo)
      return root.isChecked ? "- [x] " : "- [ ] "
    if (root.showBullet)
      return "- "
    // Quotes render their own `>` inside quoteBar (which also draws the accent
    // bar). Returning one here would show a second marker in bulletGutter.
    return ""
  }
  readonly property bool showBlockMarker: root.blockMarker.length > 0
                                          && editor.activeFocus
  readonly property int listIndent: {
    const value = root.blockMetadata && root.blockMetadata.listIndent
    const level = parseInt(value, 10) || 0
    return Math.max(0, Math.min(5, level))
  }

  property bool transformingShortcut: false
  property bool syncingFromModel: false

  // The formatted RichText is computed imperatively and cached. Focus changes
  // rewrite the same text with/without syntax dimming, so caching avoids a
  // redundant parse + relayout when nothing textual changed.
  property string renderedHtml: ""

  // Content font from Preferences. Kept here so every text block (including
  // headings, lists and quotes, which derive from TextBlock) picks it up.
  readonly property string contentFontFamily: {
    if (Preferences.fontFamily === "Mono")
      return "monospace"
    if (Preferences.fontFamily === "Serif")
      return "Georgia, 'Times New Roman', serif"
    return Qt.application.font.family
  }

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
  // Caret X from before the last alignment change. The viewer already reports
  // the new X once onAlignmentChanged runs, so the slide needs a cached start.
  property real lastCaretX: 0
  property real alignmentStartX: -1
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
    interval: 16
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

  // A link/footnote definition can live in any other block. Only re-render
  // blocks whose content actually contains brackets that could reference them.
  onLinkDefinitionsChanged: {
    if (root.content && root.content.indexOf("[") >= 0) {
      renderCacheValid = false
      refreshRenderedHtml()
    }
  }

  onFootnoteDefinitionsChanged: {
    if (root.content && root.content.indexOf("[") >= 0) {
      renderCacheValid = false
      refreshRenderedHtml()
    }
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

  // Colour and font size are stored as distinctive inline markers in the
  // block text so they survive save/reload without a second, parallel
  // rich-text state. MarkdownSyntax passes the sentinel tokens through as
  // plain text; after rendering we swap the matched ones for real spans. The
  // tokens are hidden in both focus modes during caret mapping, like the
  // <u>/<sub>/<sup> markers handled by MarkdownSyntax.hiddenTokens().
  function styleTokens(text) {
    const tokens = []
    // Colours are stored as ⟦tn-color:0xff0000⟧ (never #rrggbb): a bare
    // `#digits` inside the sentinel is parsed as a GitHub issue link by
    // MarkdownSyntax and injects <a>/<span> markup into the marker. Sizes are
    // unaffected: ⟦tn-size:16px⟧, close token ⟦tn-/style⟧.
    const re = /⟦tn-(color|size):([^⟧\n]+)⟧|⟦tn-\/style⟧/g
    let match

    while ((match = re.exec(text)) !== null) {
      if (match[1]) {
        tokens.push({
          start: match.index,
          end: match.index + match[0].length,
          style: (match[1] === "color" ? "color:" : "font-size:") + match[2],
          closing: false
        })
      } else {
        tokens.push({
          start: match.index,
          end: match.index + match[0].length,
          style: "",
          closing: true
        })
      }
    }
    return tokens
  }

  // Builds the stored open token for a validated `color:...` / `font-size:...`
  // style string. The close token is always STYLE_CLOSE.
  readonly property string styleCloseToken: "⟦tn-/style⟧"
  function openStyleToken(style) {
    if (style.indexOf("color:") === 0)
      return "⟦tn-color:" + style.substring(6) + "⟧"
    if (style.indexOf("font-size:") === 0)
      return "⟦tn-size:" + style.substring(10) + "⟧"
    return ""
  }

  // Two style strings target the same property when the part before the colon
  // matches ("color:0xff0000" / "color:0x00ff00", "font-size:14px" / "20px").
  function styleKind(style) {
    const colon = style.indexOf(":")
    return colon >= 0 ? style.substring(0, colon) : style
  }

  // Pairs each close token with the most recent unmatched open token. Any
  // unpaired token is left as literal text instead of becoming formatting.
  function matchedStylePairs(tokens) {
    const stack = []
    const pairs = []
    for (let i = 0; i < tokens.length; ++i) {
      const token = tokens[i]
      if (!token.closing) {
        stack.push(token)
      } else if (stack.length > 0) {
        pairs.push({ open: stack.pop(), close: token })
      }
    }
    return pairs
  }

  // The stored colour value is 0xRRGGBB; the CSS span needs #RRGGBB. Decode
  // only a validated six-digit hex value so a hand-edited or malformed sentinel
  // can never inject arbitrary CSS into the generated markup.
  function cssStyle(style) {
    return style.replace(/^color:0x([0-9a-fA-F]{6})$/, function(match, digits) {
      return "color:#" + digits
    })
  }

  function styleMarkerRanges(raw) {
    const result = []
    const pairs = root.matchedStylePairs(root.styleTokens(raw))
    for (let i = 0; i < pairs.length; ++i) {
      result.push({ start: pairs[i].open.start, end: pairs[i].open.end })
      result.push({ start: pairs[i].close.start, end: pairs[i].close.end })
    }
    result.sort(function(a, b) { return a.start - b.start })
    return result
  }

  function restoreStyleMarkers(html) {
    const pairs = root.matchedStylePairs(root.styleTokens(html))
    if (pairs.length === 0)
      return html

    const replacements = []
    for (let i = 0; i < pairs.length; ++i) {
      replacements.push({
        start: pairs[i].open.start,
        end: pairs[i].open.end,
        html: '<span style="' + root.cssStyle(pairs[i].open.style) + '">'
      })
      replacements.push({
        start: pairs[i].close.start,
        end: pairs[i].close.end,
        html: "</span>"
      })
    }
    replacements.sort(function(a, b) { return a.start - b.start })

    let out = ""
    let pos = 0
    for (let i = 0; i < replacements.length; ++i) {
      out += html.substring(pos, replacements[i].start) + replacements[i].html
      pos = replacements[i].end
    }
    return out + html.substring(pos)
  }

  function markdownToHtml(raw, includeSyntax) {
    if (!raw)
      return ""

    let text = MarkdownSyntax.render(raw, includeSyntax, linkDefinitions, footnoteDefinitions)
    text = root.restoreStyleMarkers(text)
    if (root.showTodo && root.isChecked)
      text = "<s>" + text + "</s>"
    return text
  }

  // The rendered viewer owns the visible layout, so every position query has
  // to go through it. Its text contains the dimmed syntax markers while the
  // block is focused, which keeps rendered positions aligned with the raw
  // editor positions.
  function viewerCaretRect(position) {
    if (!renderedViewer || !editor)
      return Qt.rect(0, 0, 1, 0)

    // The focused render can be longer than the raw text (emoji shortcodes
    // also emit the glyph), so map through the viewer's plain text instead of
    // clamping to editor.length.
    const maxPosition = renderedViewer.length
    const mapped = root.rawToViewerIndex(editor.text, position)
    const clamped = Math.max(0, Math.min(mapped, maxPosition))
    if (renderedViewer.length === 0)
      return Qt.rect(renderedViewer.x, renderedViewer.y, 1,
                     Math.max(1, renderedViewer.contentHeight))

    const rect = renderedViewer.positionToRectangle(clamped)
    return Qt.rect(renderedViewer.x + rect.x,
                   renderedViewer.y + rect.y,
                   Math.max(1, rect.width), rect.height)
  }

  // Visual (wrapped) line checks. A newline count is not enough here: a long
  // paragraph may span several visual lines, and Up/Down must move the caret
  // inside it before jumping to the neighbouring block.
  function isCursorOnFirstVisualLine() {
    if (!renderedViewer || !editor)
      return true

    const limit = renderedViewer.length

    if (limit === 0)
      return true

    const caret = renderedViewer.positionToRectangle(Math.min(
      root.rawToViewerIndex(editor.text, editor.cursorPosition), limit))
    const start = renderedViewer.positionToRectangle(0)
    return Math.abs(caret.y - start.y) < 2
  }

  function isCursorOnLastVisualLine() {
    if (!renderedViewer || !editor)
      return true

    const limit = renderedViewer.length

    if (limit === 0)
      return true

    const caret = renderedViewer.positionToRectangle(Math.min(
      root.rawToViewerIndex(editor.text, editor.cursorPosition), limit))
    const end = renderedViewer.positionToRectangle(limit)
    return Math.abs(caret.y - end.y) < 2
  }

  function caretLocalX() {
    if (!renderedViewer || !editor)
      return 0

    const position = root.rawToViewerIndex(editor.text, editor.cursorPosition)
    const clamped = Math.max(0, Math.min(position, renderedViewer.length))
    return renderedViewer.positionToRectangle(clamped).x
  }

  // Called by the editor before it changes the alignment role. Reading the
  // viewer afterwards would already yield the new (aligned) caret position.
  function captureAlignmentStart() {
    root.alignmentStartX = root.caretLocalX()
  }

  function updateCaretGeometry() {
    if (!renderedViewer || !editor)
      return

    const rect = root.viewerCaretRect(editor.cursorPosition)
    caretOffsetX = rect.x
    caretOffsetY = rect.y
    caretHeight = rect.height
    lastCaretX = root.caretLocalX()
  }

  function viewerPositionAt(x, y) {
    if (!renderedViewer)
      return 0

    const viewerIndex = renderedViewer.positionAt(
      x - renderedViewer.x, y - renderedViewer.y)
    const raw = editor ? editor.text : ""
    const rawIndex = root.viewerToRawIndex(raw, viewerIndex)
    return Math.max(0, Math.min(rawIndex, raw.length))
  }

  // textMouseArea sits above renderedViewer and consumes presses, so the
  // viewer's onLinkActivated never sees a click. Resolve the same link from
  // the MarkdownSyntax token that produced the <a> element: inline/reference
  // links, autolinks, @mentions and footnote references all carry a URL.
  // Returns "" for ordinary text so caret placement is unaffected.
  function linkUrlAtViewerPosition(viewerPosition) {
    if (!editor || !renderedViewer)
      return ""

    const raw = editor.text
    if (!raw || raw.length === 0)
      return ""

    const rawPos = root.viewerToRawIndex(
      raw, Math.max(0, Math.min(viewerPosition, renderedViewer.length)))
    const defs = MarkdownSyntax.mergedDefinitions(raw, linkDefinitions)
    const tokens = MarkdownSyntax.collectTokens(raw, defs)

    for (let i = 0; i < tokens.length; ++i) {
      const token = tokens[i]
      if (rawPos < token.start || rawPos >= token.end)
        continue

      let url = ""
      if (token.kind === "link" || token.kind === "autolink"
          || token.kind === "mention") {
        url = token.data.url
      } else if (token.kind === "footnoteRef"
                 && footnoteDefinitions
                 && footnoteDefinitions[token.data.label]) {
        url = "#fn-" + token.data.label
      }
      if (url && url.length > 0)
        return MarkdownSyntax.safeUrl(url)
    }
    return ""
  }

  // Paint a raw-text range using the rendered viewer's rich-text geometry.
  // Both the search highlights and the custom selection highlight use this so
  // scaled inline formatting (such as <sub>/<sup>) stays aligned with the
  // visible text instead of the invisible TextArea's raw layout.
  function paintRange(ctx, start, end, color) {
    if (!renderedViewer || end <= start)
      return

    // Search/selection ranges are raw-text offsets. rawToViewerIndex accounts
    // for hidden markdown/style markers in unfocused blocks and for the extra
    // emoji glyph in the focused render, so highlights stay over the right
    // characters in both modes.
    const raw = editor.text
    const rangeStart = root.rawToViewerIndex(raw, start)
    const rangeEnd = root.rawToViewerIndex(raw, end)

    if (rangeEnd <= rangeStart)
      return

    const plain = renderedViewer.getText(0, renderedViewer.length)

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
    // character and merge cells that share a line. Each iteration's "next"
    // rect becomes the current rect of the following character, so the whole
    // range costs one layout query per character instead of two.
    let rect = renderedViewer.positionToRectangle(rangeStart)

    for (let i = rangeStart; i < rangeEnd; ++i) {
      const nextRect = renderedViewer.positionToRectangle(i + 1)
      const character = plain.charAt(i)

      if (character === "\n" || character === "\u2028") {
        rect = nextRect
        continue
      }

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

      rect = nextRect
    }

    flushRun()
  }

  // MarkdownSyntax.renderedToRawIndex() and this wrapper were removed: the
  // live path is viewerToRawIndex() below, which relies on the focus-aware
  // viewerTokens() list. The dead helper only knew the syntax-hidden mapping.

  // Maps a raw editor position to the position in renderedViewer's plain text.
  // Unfocused blocks hide markdown markers and matched inline tags, so raw
  // search offsets must be shifted by the hidden characters before they can be
  // drawn against the rendered layout.
  function rawToRenderedIndex(raw, rawIndex) {
    return MarkdownSyntax.rawToRenderedIndex(raw, rawIndex, linkDefinitions, footnoteDefinitions)
  }

  // Cached token list. caret/selection/highlight painting asks for these
  // several times per move; recomputing scanned the whole block every time.
  property var viewerTokensCache: null

  // Positions inside renderedViewer's plain text. MarkdownSyntax supplies the
  // unfocused (syntax-hidden) mapping. While focused, raw text is shown
  // verbatim except that emoji shortcodes also emit their glyph, so model the
  // glyph as a zero-width raw token with content. Our style markers are hidden
  // in both modes and are added to either mapping.
  function viewerTokens(raw) {
    const focused = !!(editor && editor.activeFocus)
    const cache = root.viewerTokensCache
    if (cache && cache.raw === raw && cache.focused === focused
        && cache.linkDefs === linkDefinitions
        && cache.footnoteDefs === footnoteDefinitions) {
      return cache.tokens
    }

    const tokens = []

    if (focused) {
      if (typeof EmojiDatabase !== "undefined" && EmojiDatabase) {
        // MarkdownSyntax only renders the glyph for a shortcode that is its
        // own token. A shortcode inside inline code, math, a link definition
        // or an autolink stays literal, so adding a glyph token here would
        // drift the caret. Use the same token collection as the renderer to
        // skip shortcodes enclosed by a larger token.
        // These kinds render their contents literally (or are invisible), so
        // no glyph is emitted for a shortcode inside them. Other containers
        // such as emphasis and link labels render recursively and do emit it.
        const literalContainers = [
          "code", "math", "definition", "footnoteDefinition", "autolink"
        ]
        const defs = MarkdownSyntax.mergedDefinitions(raw, linkDefinitions)
        const syntaxTokens = MarkdownSyntax.collectTokens(raw, defs)
        const emojiRe = /:([a-zA-Z0-9_+-]{1,40}):/g
        let match
        while ((match = emojiRe.exec(raw)) !== null) {
          const emoji = EmojiDatabase.shortcodeToEmoji(match[1])
          if (!emoji)
            continue

          const start = match.index
          const end = start + match[0].length
          let nested = false
          for (let t = 0; t < syntaxTokens.length; ++t) {
            const token = syntaxTokens[t]
            if (literalContainers.indexOf(token.kind) >= 0
                && token.start <= start && token.end >= end) {
              nested = true
              break
            }
          }
          if (nested)
            continue

          const at = end
          tokens.push({ start: at, end: at, contentLen: emoji.length })
        }
      }
    } else {
      const hidden = MarkdownSyntax.hiddenTokens(
        raw, linkDefinitions, footnoteDefinitions)
      for (let i = 0; i < hidden.length; ++i) {
        tokens.push({ start: hidden[i].start, end: hidden[i].end,
                      contentLen: hidden[i].contentLen })
      }
    }

    const markers = root.styleMarkerRanges(raw)
    for (let i = 0; i < markers.length; ++i) {
      tokens.push({ start: markers[i].start, end: markers[i].end,
                    contentLen: 0 })
    }

    tokens.sort(function(a, b) { return a.start - b.start })
    root.viewerTokensCache = {
      raw: raw,
      focused: focused,
      linkDefs: linkDefinitions,
      footnoteDefs: footnoteDefinitions,
      tokens: tokens
    }
    return tokens
  }

  function rawToViewerIndex(raw, rawIndex) {
    if (!raw || rawIndex <= 0)
      return 0

    const tokens = root.viewerTokens(raw)
    let delta = 0

    for (let i = 0; i < tokens.length; ++i) {
      const token = tokens[i]
      // A zero-width token (focused emoji glyph) applies from its offset on;
      // an ordinary token only starts shifting after its first character.
      if (rawIndex <= token.start && token.end > token.start)
        break
      if (rawIndex < token.end) {
        const within = rawIndex - token.start
        return token.start - delta + Math.min(within, token.contentLen)
      }
      delta += (token.end - token.start) - token.contentLen
    }

    return rawIndex - delta
  }

  function viewerToRawIndex(raw, viewerIndex) {
    if (!raw || viewerIndex < 0)
      return 0

    const tokens = root.viewerTokens(raw)
    let renderedPos = 0
    let rawPos = 0
    let index = 0

    while (rawPos < raw.length) {
      const token = tokens[index]
      if (token && rawPos === token.start) {
        const tokenViewEnd = renderedPos + token.contentLen
        // A leading zero-width marker (the opening `**` of `**bold**`, a style
        // sentinel, an invisible link definition, ...) has no rendered column.
        // Rendered position 0 is the first *visible* character, so consume
        // such markers and land past them, inside the format. Interior and
        // trailing markers keep the <= behaviour below.
        if (token.contentLen === 0 && renderedPos === 0
            && viewerIndex === 0) {
          rawPos = token.end
          ++index
          continue
        }
        // A token with content occupies rendered positions up to (but not
        // including) its end: at that boundary the token has been consumed
        // and the next raw character applies. Zero-width tokens have no such
        // boundary, so they keep the <= behaviour and map their start.
        if (viewerIndex < tokenViewEnd
            || (token.contentLen === 0 && viewerIndex === tokenViewEnd))
          return token.start + Math.max(0, viewerIndex - renderedPos)
        renderedPos += token.contentLen
        rawPos = token.end
        ++index
        continue
      }
      if (renderedPos === viewerIndex)
        return rawPos
      ++renderedPos
      ++rawPos
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

  // Replaces the typed ":<query>" range with the chosen glyph.
  function insertEmoji(start, end, emoji) {
    const raw = editor.text
    const from = Math.max(0, Math.min(start, raw.length))
    const to = Math.max(from, Math.min(end, raw.length))
    const newText = raw.substring(0, from) + emoji + raw.substring(to)
    commitEditorText(newText, from + emoji.length, from + emoji.length)
    editor.forceActiveFocus()
  }

  // The toolbar's colour/size controls write validated inline style markers
  // around the active selection. Wrapping in stored markdown keeps undo,
  // saving and the existing renderer in charge; no parallel rich-text state
  // has to be synced with the editor.
  function applyColor(color) {
    const match = /^#(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{3})$/.exec(
      String(color || "").trim())
    if (!match)
      return
    let value = match[0]
    if (value.length === 4) {
      value = "#" + value[1] + value[1] + value[2] + value[2] +
              value[3] + value[3]
    }
    // Persist as 0xRRGGBB. A bare #000000 would be rewritten as an issue link
    // by MarkdownSyntax before restoreStyleMarkers() sees the sentinel.
    root.applyInlineStyle("color:0x" + value.substring(1))
  }

  function applyFontSize(size) {
    const parsed = parseInt(size, 10)
    if (isNaN(parsed) || parsed < 4 || parsed > 120)
      return
    root.applyInlineStyle("font-size:" + parsed + "px")
  }

  function applyInlineStyle(style) {
    const raw = editor.text
    const s = Math.min(editor.selectionStart, editor.selectionEnd)
    const e = Math.max(editor.selectionStart, editor.selectionEnd)
    if (s === e)
      return

    const open = root.openStyleToken(style)
    if (open.length === 0)
      return
    const close = root.styleCloseToken
    const sel = raw.substring(s, e)
    const kind = root.styleKind(style)

    // Selecting the same styled range again removes the marker (whole-span
    // removal). Keep this ahead of pair replacement so an explicit toggle
    // off still wins.
    if (sel.length >= open.length + close.length &&
        sel.substring(0, open.length) === open &&
        sel.substring(sel.length - close.length) === close) {
      const inner = sel.substring(open.length, sel.length - close.length)
      commitEditorText(raw.substring(0, s) + inner + raw.substring(e),
                       s, s + inner.length)
      return
    }

    // Repeated colour/size edits must replace the existing pair instead of
    // wrapping a new one around it. Colour and size spans nest legitimately,
    // so the selection rarely lines up with a pair's exact boundaries: find
    // the innermost same-kind pair whose content ENCLOSES the selection and
    // rewrite that pair in place. Whole spans and sub-selections both match;
    // other properties are left alone so real nesting still works.
    const pairs = root.matchedStylePairs(root.styleTokens(raw))
    let target = null
    for (let i = 0; i < pairs.length; ++i) {
      const pair = pairs[i]
      if (root.styleKind(pair.open.style) !== kind)
        continue
      const contentContains = pair.open.end <= s && e <= pair.close.start
      const wholeSpan = pair.open.start === s && pair.close.end === e
      if (!contentContains && !wholeSpan)
        continue
      // A larger open offset is a deeper (more recently opened) pair.
      if (!target || pair.open.end > target.open.end)
        target = pair
    }

    if (target) {
      const inner = raw.substring(target.open.end, target.close.start)
      const spanStart = target.open.start
      const spanEnd = target.close.end
      const next = raw.substring(0, spanStart) + open + inner + close
                   + raw.substring(spanEnd)
      commitEditorText(next, spanStart,
                       spanStart + open.length + inner.length + close.length)
      return
    }

    commitEditorText(raw.substring(0, s) + open + sel + close + raw.substring(e),
                     s, e + open.length + close.length)
  }

  function toggleFormat(type) {
    const raw = editor.text
    const s = Math.min(editor.selectionStart, editor.selectionEnd)
    const e = Math.max(editor.selectionStart, editor.selectionEnd)

    // Inline code and links use a small dedicated path because their markers
    // are not symmetric and links need a caret position inside the URL.
    if (type === "code" || type === "link") {
      if (s === e)
        return

      const sel = raw.substring(s, e)
      let replacement = sel
      let newStart = s
      let newEnd = e

      if (type === "code") {
        if (s >= 1 && e + 1 <= raw.length &&
            raw.charAt(s - 1) === "`" && raw.charAt(e) === "`") {
          newStart = s - 1
          newEnd = e - 1
        } else {
          replacement = "`" + sel + "`"
          newStart = s
          newEnd = e + 2
        }
      } else {
        const linkMatch = /^\[([^\]]+)\]\(([^)]*)\)$/.exec(sel)
        if (linkMatch) {
          replacement = linkMatch[1]
          newEnd = s + replacement.length
        } else {
          replacement = "[" + sel + "](url)"
          newStart = s + 1 + sel.length + 2
          newEnd = newStart + 3
        }
      }

      commitEditorText(raw.substring(0, s) + replacement + raw.substring(e),
                       newStart, newEnd)
      return
    }

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
  signal focusDelegateRequested(string blockId, int position)
  signal undoRequested()
  signal redoRequested()
  signal moveFocusRequested(int direction, real xPosition)
  signal tabRequested(int direction)
  signal commandMenuRequested(string query, real xPosition, real yPosition, real cursorHeight)
  signal commandMenuDismissed()
  signal commandMenuAccepted()
  signal commandMenuNavigated(int direction)
  signal emojiPickerRequested(string blockId, string query, int queryStart,
                              int queryEnd, real xPosition, real yPosition,
                              real cursorHeight)
  signal emojiPickerDismissed()
  signal emojiPickerAccepted()
  signal emojiPickerNavigated(int direction)
  signal caretMoved(int position, real cursorGlobalY, real cursorHeight)
  signal selectionChanged(bool hasSelection, real xPosition, real yPosition, real cursorHeight)

  property bool commandMenuActive: false
  property bool emojiPickerActive: false

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
    const oldCaretX = root.alignmentStartX >= 0
                      ? root.alignmentStartX : root.lastCaretX
    root.alignmentStartX = -1
    root.updateCaretGeometry()
    caretGeometryTimer.restart()
    selectionCanvas.requestPaint()
    root.notifySelection()

    if (renderedViewer) {
      const newCaretX = root.caretLocalX()
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
    // The viewer's text binding settles before the timer fires; drop the redundant immediate call.
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
    spacing: (root.showBullet || root.showNumbered || root.showTodo || root.showQuote) ? 0 : 5

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
        id: quoteBar
        width: root.showQuote ? (editor.activeFocus ? 26 : 16) : 0
        visible: root.showQuote
        height: parent.height

        Rectangle {
          width: 3
          height: parent.height
          anchors.left: parent.left
          anchors.leftMargin: 2
          color: Theme.accent
          radius: 1.5
        }

        Text {
          visible: editor.activeFocus
          anchors.left: parent.left
          anchors.leftMargin: 10
          anchors.top: parent.top
          anchors.topMargin: editor.topPadding
          text: ">"
          color: Theme.textMuted
          font.pixelSize: root.fontPixelSize
          font.family: root.contentFontFamily
          font.weight: root.fontBold ? Font.Bold : Font.Normal
          height: root.firstLineHeight
          verticalAlignment: Text.AlignVCenter
        }
      }

      Item {
        id: bulletGutter
        width: (root.showBlockMarker
                ? Math.max(24, markerText.implicitWidth + 8)
                : ((root.showBullet || root.showNumbered || root.showTodo) ? 24 : 0))
               + root.listIndent * 16
        visible: width > 0
        height: root.firstLineHeight

        Text {
          id: markerText
          visible: root.showBlockMarker
          anchors.left: parent.left
          anchors.leftMargin: root.listIndent * 16
          anchors.verticalCenter: parent.verticalCenter
          text: root.blockMarker
          color: Theme.textMuted
          font.pixelSize: root.fontPixelSize
          font.family: root.contentFontFamily
          font.weight: root.fontBold ? Font.Bold : Font.Normal

          horizontalAlignment: Text.AlignLeft
          verticalAlignment: Text.AlignVCenter
        }

        Text {
          visible: root.showBullet && !root.showBlockMarker
          anchors.left: parent.left
          anchors.leftMargin: root.listIndent * 16
          anchors.verticalCenter: parent.verticalCenter

          text: "•"
          color: Theme.textMuted
          font.pixelSize: 22

          horizontalAlignment: Text.AlignLeft
          verticalAlignment: Text.AlignVCenter
        }

        Text {
          visible: root.showNumbered && !root.showBlockMarker
          anchors.left: parent.left
          anchors.leftMargin: root.listIndent * 16
          anchors.verticalCenter: parent.verticalCenter

          text: root.numberPrefix
          color: Theme.textMuted
          font.pixelSize: 14

          horizontalAlignment: Text.AlignLeft
          verticalAlignment: Text.AlignVCenter
        }

        Item {
          id: todoGutterItem
          visible: root.showTodo && !root.showBlockMarker
          anchors.left: parent.left
          anchors.leftMargin: root.listIndent * 16
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
        width: parent.width - bulletGutter.width - quoteBar.width
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
          font.family: root.contentFontFamily
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
          font.family: root.contentFontFamily
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

          onLinkActivated: function(url) {
            // While focused the user is editing the raw markdown; only the
            // rendered (unfocused) view behaves like a browser link.
            if (!url || url.length === 0)
              return
            if (url.indexOf("#fn-") === 0) {
              if (root.editorApi && root.editorApi.focusFootnote)
                root.editorApi.focusFootnote(url.substring(4))
              return
            }
            if (!editor.activeFocus)
              Qt.openUrlExternally(url)
          }
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
            // Resolve a rendered link before doing anything that changes
            // focus. textMouseArea covers the viewer, so this is the only
            // place that sees the click.
            const viewerIndex = renderedViewer
                ? renderedViewer.positionAt(mouse.x - renderedViewer.x,
                                            mouse.y - renderedViewer.y)
                : 0
            const linkUrl = root.linkUrlAtViewerPosition(viewerIndex)

            if (linkUrl.indexOf("#fn-") === 0) {
              anchorPosition = -1
              if (root.editorApi && root.editorApi.focusFootnote)
                root.editorApi.focusFootnote(linkUrl.substring(4))
              return
            }
            // External links are only active in the rendered view. While the
            // block is being edited, clicks inside a link still place the caret.
            if (linkUrl.length > 0 && !editor.activeFocus) {
              anchorPosition = -1
              Qt.openUrlExternally(linkUrl)
              return
            }

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
          font.family: root.contentFontFamily
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

            // Colon-triggered emoji picker. Like the slash-command menu it is
            // a paragraph-only shortcut: headings, lists and quotes contain
            // colons as ordinary text and must not open the picker.
            // The query is the text between the last colon and the caret;
            // whitespace or a second colon closes it.
            const caret = Math.min(editor.cursorPosition, text.length)
            const beforeCaret = text.substring(0, caret)
            const colonIndex = beforeCaret.lastIndexOf(":")
            const emojiQuery = colonIndex >= 0
                ? beforeCaret.substring(colonIndex + 1) : ""
            const colonAtWordStart = colonIndex === 0 ||
                /\s/.test(beforeCaret.charAt(colonIndex - 1))
            if (root.blockType === 0 && colonIndex >= 0 && colonAtWordStart &&
                emojiQuery.length <= 32 && !/[\s:]/.test(emojiQuery)) {
              const emojiRect = root.viewerCaretRect(caret)
              const emojiPos = slideLayer.mapToItem(root,
                  emojiRect.x, emojiRect.y)
              root.emojiPickerRequested(root.blockId, emojiQuery,
                                        colonIndex, caret,
                                        emojiPos.x, emojiPos.y,
                                        emojiRect.height)
            } else if (root.emojiPickerActive) {
              root.emojiPickerDismissed()
            }

            // paragraph -> setext heading (Title\n=== or Title\n---)
            // Checked before the divider trigger so `Title\n---` becomes a
            // level-2 heading instead of a horizontal rule.
            const setextMatch = root.blockType === 0
                ? /^(.+)\n(=+|-+)[ \t]*$/.exec(text) : null
            if (setextMatch) {
              root.transformingShortcut = true
              const headingText = setextMatch[1]
              const level = setextMatch[2].charAt(0) === "=" ? 1 : 2
              root.model.changeBlockType(root.blockId, 1, level)
              root.model.updateBlockText(root.blockId, headingText)
              root.transformingShortcut = false
              root.focusRequested(headingText.length)
              return
            }

            // paragraph -> bullet (`- `, `* ` or `+ `), with 2-space nesting
            const bulletMatch = root.blockType === 0
                ? /^((?: {2})*)([-*+])\s/.exec(text) : null
            if (bulletMatch) {
              root.transformingShortcut = true

              const indentLevel = bulletMatch[1].length / 2
              const bulletText = text.substring(bulletMatch[0].length)

              root.model.changeBlockType(
                root.blockId,
                2,
                0
              )

              const bulletMeta = Object.assign({}, root.blockMetadata || {})
              bulletMeta.listIndent = indentLevel
              root.model.setBlockMetadata(root.blockId, bulletMeta)

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

            // paragraph -> callout (> [!NOTE], > [!TIP], > [!WARNING], > [!DANGER], etc.)
            const calloutMatch = /^>\s*\[!([a-zA-Z]+)\]\s*/.exec(text)
            if (root.blockType === 0 && calloutMatch) {
              root.transformingShortcut = true

              const kind = calloutMatch[1].toLowerCase()
              const calloutContent = text.substring(calloutMatch[0].length)

              root.model.changeBlockType(
                root.blockId,
                10, // BlockType::Callout
                0
              )

              if (root.model.setBlockCalloutType) {
                root.model.setBlockCalloutType(root.blockId, kind)
              }

              root.model.updateBlockText(
                root.blockId,
                calloutContent
              )

              root.transformingShortcut = false
              // Focus at the end of the callout (after the prefix or existing text)
              root.focusRequested(-1)
              return
            }

            // quote -> callout ([!NOTE], [!TIP], [!WARNING], etc. typed inside Quote)
            const quoteCalloutMatch = /^\[!([a-zA-Z]+)\]\s*/.exec(text)
            if (root.blockType === 5 && quoteCalloutMatch) {
              root.transformingShortcut = true

              const kind = quoteCalloutMatch[1].toLowerCase()
              const calloutContent = text.substring(quoteCalloutMatch[0].length)

              root.model.changeBlockType(
                root.blockId,
                10, // BlockType::Callout
                0
              )

              if (root.model.setBlockCalloutType) {
                root.model.setBlockCalloutType(root.blockId, kind)
              }

              root.model.updateBlockText(
                root.blockId,
                calloutContent
              )

              root.transformingShortcut = false
              root.focusRequested(-1)
              return
            }

            // paragraph -> code block (``` or ~~~, with optional info string)
            const codeFenceMatch = root.blockType === 0
                ? /^(`{3,}|~{3,})([a-zA-Z0-9_+#.-]*)(?:\s|\n|$)/.exec(text)
                : null
            if (codeFenceMatch && (text.endsWith(" ") || text.endsWith("\n")
                                   || text === codeFenceMatch[1])) {
              root.transformingShortcut = true

              const fence = codeFenceMatch[1]
              const lang = codeFenceMatch[2].trim()
              const remaining = text.substring(codeFenceMatch[0].length)

              root.model.changeBlockType(
                root.blockId,
                6, // BlockType::Code
                0
              )

              const codeMeta = Object.assign({}, root.blockMetadata || {})
              codeMeta.codeFence = fence
              root.model.setBlockMetadata(root.blockId, codeMeta)

              if (root.model.setBlockCodeLanguage && lang.length > 0) {
                root.model.setBlockCodeLanguage(root.blockId, lang)
              }

              root.model.updateBlockText(
                root.blockId,
                remaining
              )

              root.transformingShortcut = false
              root.focusRequested(0)
              return
            }

            // paragraph -> quote
            if (root.blockType === 0 && text.startsWith("> ")) {
              root.transformingShortcut = true

              const quoteText = text.substring(2)

              root.model.changeBlockType(
                root.blockId,
                5, // BlockType::Quote
                0
              )

              root.model.updateBlockText(
                root.blockId,
                quoteText
              )

              root.transformingShortcut = false

              // Changing the type makes the dispatcher reload the block
              // renderer, so focus has to move to the new editor.
              root.focusRequested(quoteText.length)
              return
            }

            // paragraph -> divider (---, *** or ___)
            if (root.blockType === 0 && (text === "---" || text === "***" || text === "___")) {
              root.transformingShortcut = true

              root.model.updateBlockText(root.blockId, "")
              root.model.changeBlockType(
                root.blockId,
                7, // BlockType::Divider
                0
              )

              // Insert a new paragraph below the divider so the user can continue typing
              const newId = root.model.insertBlockAfter(root.blockId, 0, "")
              if (newId.length > 0) {
                root.transformingShortcut = false
                // Pass the id: source rows and visible rows diverge once the
                // document contains a column layout.
                root.focusDelegateRequested(newId, 0)
                return
              }

              root.transformingShortcut = false
              return
            }

            // paragraph -> todo list ([], [ ], - [ ], - [x])
            const todoMatch = root.blockType === 0
                ? /^((?: {2})*)(?:-\s+\[([ xX])\]|\[ ?\])\s/.exec(text) : null
            if (todoMatch) {
              root.transformingShortcut = true

              const prefixLen = todoMatch[0].length
              const todoText = text.substring(prefixLen)
              const indentLevel = todoMatch[1].length / 2
              const checked = !!todoMatch[2]
                              && todoMatch[2].toLowerCase() === "x"

              root.model.changeBlockType(
                root.blockId,
                4, // BlockType::Todo
                0
              )

              const todoMeta = Object.assign({}, root.blockMetadata || {})
              todoMeta.listIndent = indentLevel
              root.model.setBlockMetadata(root.blockId, todoMeta)

              root.model.updateBlockText(
                root.blockId,
                todoText
              )

              if (checked && root.model.setBlockChecked)
                root.model.setBlockChecked(root.blockId, true)

              root.transformingShortcut = false

              root.focusRequested(todoText.length)
              return
            }

            // paragraph -> numbered list (1. or 1), with preserved start)
            const numberedMatch = root.blockType === 0
                ? /^((?: {2})*)(\d+)[.)]\s/.exec(text) : null
            if (numberedMatch) {
              root.transformingShortcut = true

              const prefixLen = numberedMatch[0].length
              const numberedText = text.substring(prefixLen)
              const indentLevel = numberedMatch[1].length / 2
              const start = parseInt(numberedMatch[2], 10)

              root.model.changeBlockType(
                root.blockId,
                3,
                0
              )

              const listMeta = Object.assign({}, root.blockMetadata || {})
              listMeta.listStart = start
              listMeta.listIndent = indentLevel
              root.model.setBlockMetadata(root.blockId, listMeta)

              root.model.updateBlockText(
                root.blockId,
                numberedText
              )

              root.transformingShortcut = false

              root.focusRequested(numberedText.length)
              return
            }

            // paragraph -> indented code block (four leading spaces that are
            // not a nested list marker)
            if (root.blockType === 0 &&
                /^ {4}(?![ \t]*[-*+]\s|(?:\d+[.)]\s)|\[)/.test(text)) {
              root.transformingShortcut = true
              const codeText = text.substring(4)
              root.model.changeBlockType(root.blockId, 6, 0)
              root.model.updateBlockText(root.blockId, codeText)
              root.transformingShortcut = false
              root.focusRequested(0)
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
            if (root.emojiPickerActive) {
              if (event.key === Qt.Key_Escape) {
                event.accepted = true
                root.emojiPickerDismissed()
                return
              }
              if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                event.accepted = true
                root.emojiPickerAccepted()
                return
              }
              if (event.key === Qt.Key_Up || event.key === Qt.Key_Down ||
                  event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                event.accepted = true
                root.emojiPickerNavigated(
                  (event.key === Qt.Key_Up || event.key === Qt.Key_Left) ? -1 : 1)
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
              if (event.key === Qt.Key_E) {
                event.accepted = true
                root.toggleFormat("code")
                return
              }
              if (event.key === Qt.Key_K) {
                event.accepted = true
                root.toggleFormat("link")
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

            // Tab moves between editable blocks. Without this the TextEdit's
            // default handling just switches QML focus and skips the editor.
            if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
              event.accepted = true
              root.tabRequested(event.key === Qt.Key_Backtab ? -1 : 1)
              return
            }

            // Alt+Up/Down is reserved for moving the block itself, so let the
            // application shortcut handle it instead of moving focus.
            if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down)
                && !(event.modifiers & Qt.AltModifier)) {
              const movingUp = event.key === Qt.Key_Up

              // Let TextArea handle vertical caret motion inside a wrapped
              // block, and let it extend an active selection normally.
              if (editor.selectionStart !== editor.selectionEnd)
                return

              if (movingUp ? !root.isCursorOnFirstVisualLine()
                           : !root.isCursorOnLastVisualLine())
                return

              const cursorRect = root.viewerCaretRect(editor.cursorPosition)
              const direction = movingUp ? -1 : 1

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
