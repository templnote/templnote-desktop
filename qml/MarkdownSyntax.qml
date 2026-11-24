pragma Singleton

import QtQuick

// Shared GitHub-flavoured Markdown inline syntax engine.
//
// The engine is deliberately view-only: it never mutates block text. It has
// two jobs:
//
//   1. render(text, syntaxVisible, definitions, footnotes) -> rich text HTML
//      When a block is focused, syntax markers are emitted as dimmed text so
//      the user can see and edit them. When it is not focused, markers are
//      removed and only the formatted result remains.
//
//   2. hiddenTokens(...) -> raw ranges whose rendered length differs from the
//      raw length. The editor uses these to keep caret, selection and search
//      highlights aligned in both modes.
//
// Image syntax is parse-only: `![alt](src)` is always rendered as an image,
// never shown as markdown source. Columns are not markdown at all.
QtObject {
  id: root

  readonly property var htmlTagNames: [
    "u", "sub", "sup", "b", "strong", "i", "em", "s", "strike", "del", "code"
  ]

  function escapeHtml(value) {
    return String(value === undefined || value === null ? "" : value)
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
  }

  function escapeAttribute(value) {
    return escapeHtml(value).replace(/"/g, "&quot;")
  }

  // Preserve the editor's whitespace behaviour: hard newlines become line
  // breaks and runs of spaces become non-breaking so RichText does not collapse
  // them.
  function preserve(value) {
    return escapeHtml(value)
        .replace(/\n/g, "<br/>")
        .replace(/  +/g, function(match) {
          return " " + "&nbsp;".repeat(match.length - 1)
        })
  }

  function marker(value) {
    return "<span style=\"color:" + Theme.textMuted +
           "; font-weight:normal; font-style:normal; text-decoration:none;\">" +
           escapeHtml(value) + "</span>"
  }

  function decodeEntity(raw) {
    const inner = raw.substring(1, raw.length - 1)

    if (inner.charAt(0) === "#") {
      let code = 0
      if (inner.charAt(1) === "x" || inner.charAt(1) === "X")
        code = parseInt(inner.substring(2), 16)
      else
        code = parseInt(inner.substring(1), 10)
      if (isNaN(code) || code < 0 || code > 0x10ffff)
        return raw
      try {
        return String.fromCodePoint(code)
      } catch (error) {
        return raw
      }
    }

    const named = Object.assign(Object.create(null), {
      amp: "&", lt: "<", gt: ">", quot: "\"", apos: "'", nbsp: "\u00a0",
      copy: "\u00a9", reg: "\u00ae", trade: "\u2122", hellip: "\u2026",
      mdash: "\u2014", ndash: "\u2013", laquo: "\u00ab", raquo: "\u00bb",
      bull: "\u2022", middot: "\u00b7", times: "\u00d7", divide: "\u00f7",
      euro: "\u20ac", pound: "\u00a3", yen: "\u00a5", cent: "\u00a2",
      deg: "\u00b0", plusmn: "\u00b1", sup2: "\u00b2", sup3: "\u00b3",
      frac12: "\u00bd", frac14: "\u00bc", frac34: "\u00be",
      ltquest: "\u00bf", iexcl: "\u00a1", oelig: "\u0153", OElig: "\u0152",
      scaron: "\u0161", Scaron: "\u0160", yuml: "\u00ff", Yuml: "\u0178"
    })
    // A null prototype alone is not enough for inherited names such as
    // "toString" when the map is replaced by a plain object; always require an
    // own property so `&constructor;` / `&toString;` stay literal text instead
    // of resolving to Object.prototype members and being injected as HTML.
    return Object.prototype.hasOwnProperty.call(named, inner)
           ? named[inner] : raw
  }

  function mightHaveDefinition(text) {
    return !!text && text.indexOf("[") >= 0
  }

  function definitionsFrom(text) {
    // Labels are user-authored keys; a null prototype prevents "__proto__"
    // and friends from poisoning the map we hand back to the editor.
    const found = Object.create(null)
    const re = /^[ \t]{0,3}\[([^\]]+)\]:[ \t]+(\S+)(?:[ \t]+["'(]([^"')]*)["')])?[ \t]*$/gm
    let match
    while ((match = re.exec(text)) !== null) {
      found[match[1].toLowerCase()] = { url: match[2], title: match[3] || "" }
    }
    return found
  }

  // Footnote definition lines are collected document-wide by the editor and
  // rendered in a footer. A single-line body is supported; continuation lines
  // are left as ordinary block text.
  function footnotesFrom(text) {
    const found = Object.create(null)
    const re = /^[ \t]{0,3}\[\^([^\]]+)\]:[ \t]*(.*)$/gm
    let match
    while ((match = re.exec(text)) !== null) {
      found[match[1]] = { label: match[1], text: match[2].trim() }
    }
    return found
  }

  function mergedDefinitions(text, definitions) {
    const result = Object.create(null)
    const external = definitions || {}
    for (const key in external)
      result[key.toLowerCase()] = external[key]
    const local = definitionsFrom(text)
    for (const key in local)
      result[key] = local[key]
    return result
  }

  function looksLikeUrl(value) {
    return /^(?:https?:\/\/|mailto:|ftp:\/\/)/i.test(value)
  }

  function safeUrl(value) {
    const url = String(value || "").trim()
    // Only web/mail schemes are safe to hand to Qt.openUrlExternally().
    // Document content is untrusted: file:, javascript:, data: and other
    // schemes must never be opened from a note.
    if (/^(?:https?:\/\/|mailto:)/i.test(url))
      return url
    // Relative links without a scheme are fine (they are resolved by the
    // preview), but anything with a disallowed scheme is not.
    if (/^[a-z][a-z0-9+.-]*:/i.test(url))
      return "#"
    return url
  }

  // Image sources are validated again here so plugin QML cannot bypass the
  // model-level check. Local file: URLs are allowed only for attachments,
  // which the model has already canonicalized into a safe value.
  function safeImageSource(value) {
    const url = String(value || "").trim()
    if (/^(?:https?:\/\/|qrc:|data:image\/|file:\/\/)/i.test(url))
      return url
    return ""
  }

  // ---------------------------------------------------------------------------
  // Token collection. Tokens are raw-offset ranges; nested tokens are dropped
  // in favour of the outermost one, and the outer token renders its content
  // recursively so nesting still works.
  // ---------------------------------------------------------------------------

  function collectTokens(text, definitions) {
    const tokens = []
    let match

    // Link reference definitions are invisible in the rendered view.
    const definitionRe = /^[ \t]{0,3}\[([^\]]+)\]:[ \t]+(\S+)(?:[ \t]+["'(]([^"')]*)["')])?[ \t]*$/gm
    while ((match = definitionRe.exec(text)) !== null) {
      tokens.push({
        start: match.index,
        end: match.index + match[0].length,
        kind: "definition",
        data: { raw: match[0] }
      })
    }

    // Footnote definitions are invisible where they are written.
    const footnoteDefinitionRe = /^[ \t]{0,3}\[\^([^\]]+)\]:[ \t]*(.*)$/gm
    while ((match = footnoteDefinitionRe.exec(text)) !== null) {
      tokens.push({
        start: match.index,
        end: match.index + match[0].length,
        kind: "footnoteDefinition",
        data: { raw: match[0], label: match[1], text: match[2].trim() }
      })
    }

    // Footnote references: [^label]
    const footnoteRefRe = /\[\^([^\]]+)\]/g
    while ((match = footnoteRefRe.exec(text)) !== null) {
      tokens.push({
        start: match.index,
        end: match.index + match[0].length,
        kind: "footnoteRef",
        data: { raw: match[0], label: match[1] }
      })
    }

    // Emoji shortcodes from the vendored Emojibase dataset.
    if (typeof EmojiDatabase !== "undefined" && EmojiDatabase) {
      const emojiRe = /:([a-zA-Z0-9_+-]{1,40}):/g
      while ((match = emojiRe.exec(text)) !== null) {
        const emoji = EmojiDatabase.shortcodeToEmoji(match[1])
        if (!emoji)
          continue
        tokens.push({
          start: match.index,
          end: match.index + match[0].length,
          kind: "emojiShortcode",
          data: { raw: match[0], emoji: emoji, shortcode: match[1] }
        })
      }
    }

    // GitHub-style @user and #123 references.
    const mentionRe = /(^|[^\w])@([a-zA-Z0-9][a-zA-Z0-9-]{0,38})/g
    while ((match = mentionRe.exec(text)) !== null) {
      const start = match.index + match[1].length
      tokens.push({
        start: start,
        end: start + match[0].length - match[1].length,
        kind: "mention",
        data: {
          user: match[2],
          raw: match[0].substring(match[1].length),
          url: "https://github.com/" + match[2]
        }
      })
    }
    const issueRe = /(^|[^\w])#(\d{1,9})\b/g
    while ((match = issueRe.exec(text)) !== null) {
      const start = match.index + match[1].length
      tokens.push({
        start: start,
        end: start + match[0].length - match[1].length,
        kind: "mention",
        data: {
          user: "",
          raw: match[0].substring(match[1].length),
          url: "https://github.com/issues/" + match[2]
        }
      })
    }

    // Inline and display math: $x$ and $$x$$
    const mathRe = /\$\$([^$]+)\$\$|\$([^$\n]+)\$/g
    while ((match = mathRe.exec(text)) !== null) {
      tokens.push({
        start: match.index,
        end: match.index + match[0].length,
        kind: "math",
        data: {
          content: match[1] !== undefined ? match[1] : match[2],
          delimiter: match[0].charAt(0) === "$" && match[0].charAt(1) === "$"
                     ? "$$" : "$"
        }
      })
    }

    // Nested blockquote markers at the start of a line.
    const quoteRe = /^((?:> ?)+)/gm
    while ((match = quoteRe.exec(text)) !== null) {
      if (match[1].length === 0)
        continue
      tokens.push({
        start: match.index,
        end: match.index + match[1].length,
        kind: "quote",
        data: {
          raw: match[1],
          levels: (match[1].match(/>/g) || []).length
        }
      })
    }

    // Inline code spans. Backtick runs must match in length.
    const tickRe = /(`+)/g
    while ((match = tickRe.exec(text)) !== null) {
      const ticks = match[1]
      const close = text.indexOf(ticks, match.index + ticks.length)
      if (close >= 0) {
        tokens.push({
          start: match.index,
          end: close + ticks.length,
          kind: "code",
          data: {
            ticks: ticks,
            content: text.substring(match.index + ticks.length, close)
          }
        })
        tickRe.lastIndex = close + ticks.length
      }
    }

    // Inline images and links.
    const inlineRe = /(!?)\[([^\]]*)\]\(([^)\s]+)(?:\s+["'(]([^"')]*)["')])?\)/g
    while ((match = inlineRe.exec(text)) !== null) {
      const isImage = match[1] === "!"
      tokens.push({
        start: match.index,
        end: match.index + match[0].length,
        kind: isImage ? "image" : "link",
        data: {
          label: match[2],
          url: match[3],
          title: match[4] || "",
          raw: match[0],
          labelStart: match.index + match[1].length + 1,
          labelEnd: match.index + match[1].length + 1 + match[2].length
        }
      })
    }

    // Full and collapsed reference links.
    const refRe = /(!?)\[([^\]]*)\]\[([^\]]*)\]/g
    while ((match = refRe.exec(text)) !== null) {
      const id = (match[3] || match[2]).toLowerCase()
      const definition = definitions[id]
      if (!definition)
        continue
      tokens.push({
        start: match.index,
        end: match.index + match[0].length,
        kind: match[1] === "!" ? "image" : "link",
        data: {
          label: match[2],
          url: definition.url,
          title: definition.title,
          raw: match[0],
          labelStart: match.index + match[1].length + 1,
          labelEnd: match.index + match[1].length + 1 + match[2].length
        }
      })
    }

    // Shortcut reference links: [label] where a definition exists.
    const shortcutRe = /(!?)\[([^\]]+)\](?![\(\[])/g
    while ((match = shortcutRe.exec(text)) !== null) {
      const definition = definitions[match[2].toLowerCase()]
      if (!definition)
        continue
      tokens.push({
        start: match.index,
        end: match.index + match[0].length,
        kind: match[1] === "!" ? "image" : "link",
        data: {
          label: match[2],
          url: definition.url,
          title: definition.title,
          raw: match[0],
          labelStart: match.index + match[1].length + 1,
          labelEnd: match.index + match[1].length + 1 + match[2].length
        }
      })
    }

    // Angle autolinks and bare URL / www autolinks.
    const angleRe = /<(?:https?:\/\/|ftp:\/\/|mailto:)[^>\s]+>|<\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b>/gi
    while ((match = angleRe.exec(text)) !== null) {
      const value = match[0].substring(1, match[0].length - 1)
      tokens.push({
        start: match.index,
        end: match.index + match[0].length,
        kind: "autolink",
        data: {
          value: value,
          angle: true,
          url: value.indexOf("@") > 0 && !looksLikeUrl(value)
               ? "mailto:" + value : value
        }
      })
    }
    const bareRe = /\b(?:https?:\/\/|www\.)[^\s<>]+/gi
    while ((match = bareRe.exec(text)) !== null) {
      tokens.push({
        start: match.index,
        end: match.index + match[0].length,
        kind: "autolink",
        data: {
          value: match[0],
          angle: false,
          url: match[0].indexOf("www.") === 0 ? "https://" + match[0] : match[0]
        }
      })
    }

    // Inline HTML (safe subset only). Br is self closing, the rest pair up.
    const tagRe = /<\/?([a-zA-Z][a-zA-Z0-9-]*)\b[^>]*>/g
    const stack = []
    while ((match = tagRe.exec(text)) !== null) {
      const name = match[1].toLowerCase()
      if (htmlTagNames.indexOf(name) < 0)
        continue
      const closing = match[0].charAt(1) === "/"
      const selfClosing = name === "br" || /\/>$/.test(match[0])

      if (selfClosing) {
        tokens.push({
          start: match.index,
          end: match.index + match[0].length,
          kind: "html",
          data: { tag: name, selfClosing: true, raw: match[0] }
        })
        continue
      }

      if (!closing) {
        stack.push({ name: name, index: match.index, end: match.index + match[0].length })
      } else if (stack.length > 0 && stack[stack.length - 1].name === name) {
        const open = stack.pop()
        tokens.push({
          start: open.index,
          end: match.index + match[0].length,
          kind: "html",
          data: {
            tag: name,
            selfClosing: false,
            raw: text.substring(open.index, match.index + match[0].length),
            openEnd: open.end,
            closeStart: match.index
          }
        })
      }
    }

    // Backslash at the end of a line is a hard break.
    const hardBreakRe = /\\\n/g
    while ((match = hardBreakRe.exec(text)) !== null) {
      tokens.push({
        start: match.index,
        end: match.index + match[0].length,
        kind: "hardbreak",
        data: {}
      })
    }

    // Backslash escapes of ASCII punctuation.
    const escapeRe = /\\([\\`*_{}\[\]()#+\-.!>~|])/g
    while ((match = escapeRe.exec(text)) !== null) {
      tokens.push({
        start: match.index,
        end: match.index + match[0].length,
        kind: "escape",
        data: { character: match[1] }
      })
    }

    // HTML entities and numeric character references.
    const entityRe = /&(?:#x[0-9a-fA-F]+|#\d+|[a-zA-Z][a-zA-Z0-9]+);/g
    while ((match = entityRe.exec(text)) !== null) {
      tokens.push({
        start: match.index,
        end: match.index + match[0].length,
        kind: "entity",
        data: { raw: match[0], decoded: decodeEntity(match[0]) }
      })
    }

    // Emphasis: **bold**, *italic*, ***both***, __bold__, _italic_, ~~strike~~.
    const delimiterRe = /(\*{1,3}|_{1,2}|~{2})/g
    const opens = { "*": [], "_": [], "~": [] }
    while ((match = delimiterRe.exec(text)) !== null) {
      const run = match[1]
      const char = run.charAt(0)
      const before = match.index > 0 ? text.charAt(match.index - 1) : ""
      const after = match.index + run.length < text.length
                    ? text.charAt(match.index + run.length) : ""
      const prevSpace = before === "" || /\s/.test(before)
      const nextSpace = after === "" || /\s/.test(after)

      let canOpen = !nextSpace
      let canClose = !prevSpace
      if (char === "_") {
        // Underscores inside a word do not open or close emphasis.
        if (!prevSpace && /\w/.test(before)) canOpen = false
        if (!nextSpace && /\w/.test(after)) canClose = false
      }

      if (canOpen && opens[char].length === 0) {
        opens[char].push({ start: match.index, end: match.index + run.length, run: run })
      } else if (canClose && opens[char].length > 0) {
        const open = opens[char].pop()
        const strength = char === "~" ? 2 : Math.min(open.run.length, run.length)
        let kind = "italic"
        if (char === "~") kind = "strike"
        else if (strength >= 3) kind = "boldItalic"
        else if (strength >= 2) kind = "bold"

        tokens.push({
          start: open.start,
          end: match.index + run.length,
          kind: kind,
          data: {
            openStart: open.start,
            openEnd: open.end,
            closeStart: match.index,
            closeEnd: match.index + run.length,
            contentStart: open.end,
            contentEnd: match.index
          }
        })
      }
    }

    tokens.sort(function(a, b) {
      if (a.start !== b.start)
        return a.start - b.start
      return b.end - a.end
    })

    const outer = []
    let coveredUntil = -1
    for (let i = 0; i < tokens.length; ++i) {
      if (tokens[i].start < coveredUntil)
        continue
      outer.push(tokens[i])
      coveredUntil = tokens[i].end
    }
    return outer
  }

  // ---------------------------------------------------------------------------
  // Rendering
  // ---------------------------------------------------------------------------

  function tokenHtml(token, syntaxVisible, definitions, footnotes) {
    const data = token.data

    switch (token.kind) {
    case "definition":
    case "footnoteDefinition":
      return syntaxVisible ? marker(data.raw) : ""

    case "footnoteRef": {
      const note = footnotes && footnotes[data.label]
      if (!note)
        return syntaxVisible ? marker(data.raw) : escapeHtml(data.raw)
      const number = String(note.index)
      const href = "<sup><a href=\"#fn-" + escapeAttribute(data.label) +
                   "\">" + number + "</a></sup>"
      return syntaxVisible ? marker(data.raw) + href : href
    }

    case "quote": {
      const bars = "│ ".repeat(data.levels)
      return syntaxVisible ? marker(data.raw) + bars : bars
    }

    case "emojiShortcode":
      // Glyphs are always rendered; the shortcode is syntax shown only while
      // the block is focused.
      return syntaxVisible ? marker(data.raw) + escapeHtml(data.emoji)
                           : escapeHtml(data.emoji)

    case "mention": {
      const label = escapeHtml(data.raw)
      const link = "<a href=\"" + escapeAttribute(safeUrl(data.url)) + "\">" +
                   "<span style=\"color:" + Theme.accent + "\">" + label +
                   "</span></a>"
      return link
    }

    case "math": {
      const body = "<span style=\"font-family:monospace; font-style:italic\">" +
                   escapeHtml(data.content) + "</span>"
      return syntaxVisible
          ? marker(data.delimiter) + body + marker(data.delimiter)
          : body
    }

    case "code": {
      const content = preserve(data.content)
      const body = "<span style=\"font-family:monospace\">" + content + "</span>"
      return syntaxVisible
          ? marker(data.ticks) + body + marker(data.ticks)
          : body
    }

    case "image": {
      const url = safeUrl(data.url)
      const alt = escapeAttribute(data.label)
      const title = data.title ? " title=\"" + escapeAttribute(data.title) + "\"" : ""
      // Images are parse-only by design: no syntax markers in either mode.
      return "<img src=\"" + escapeAttribute(url) + "\" alt=\"" + alt + "\"" +
             title + " />"
    }

    case "link": {
      const body = render(data.label, syntaxVisible, definitions, footnotes)
      const href = "<a href=\"" + escapeAttribute(safeUrl(data.url)) + "\">" + body + "</a>"
      if (!syntaxVisible)
        return href
      const raw = data.raw || ""
      const labelStart = data.labelStart - token.start
      const labelEnd = data.labelEnd - token.start
      return marker(raw.substring(0, labelStart)) + href +
             marker(raw.substring(labelEnd))
    }

    case "autolink": {
      // Do not re-parse the URL through render(): a bare URL would match the
      // autolink scanner again and recurse forever.
      const body = escapeHtml(data.value)
      const href = "<a href=\"" + escapeAttribute(safeUrl(data.url)) + "\">" + body + "</a>"
      if (!syntaxVisible || !data.angle)
        return href
      return marker("<") + href + marker(">")
    }

    case "html": {
      const tag = data.tag
      if (data.selfClosing)
        return syntaxVisible ? marker(data.raw) + "<br/>" : "<br/>"

      const openTag = "<" + tag + ">"
      const closeTag = "</" + tag + ">"
      const inner = render(
            textSlice(data.openEnd, data.closeStart), syntaxVisible, definitions,
            footnotes)
      if (!syntaxVisible)
        return openTag + inner + closeTag

      const raw = data.raw || ""
      return marker(raw.substring(0, data.openEnd - token.start)) +
             openTag + inner + closeTag +
             marker(raw.substring(raw.length - (token.end - data.closeStart)))
    }

    case "escape":
      return syntaxVisible
          ? marker("\\") + escapeHtml(data.character)
          : escapeHtml(data.character)

    case "hardbreak":
      return syntaxVisible ? marker("\\") + "<br/>" : "<br/>"

    case "entity":
      return syntaxVisible
          ? marker(data.raw)
          : escapeHtml(data.decoded)

    case "bold":
    case "italic":
    case "boldItalic":
    case "strike": {
      const open = syntaxVisible
          ? marker(textSlice(data.openStart, data.openEnd)) : ""
      const close = syntaxVisible
          ? marker(textSlice(data.closeStart, data.closeEnd)) : ""
      const inner = render(textSlice(data.contentStart, data.contentEnd),
                           syntaxVisible, definitions, footnotes)
      let html = inner
      if (token.kind === "boldItalic")
        html = "<b><i>" + html + "</i></b>"
      else if (token.kind === "bold")
        html = "<b>" + html + "</b>"
      else if (token.kind === "italic")
        html = "<i>" + html + "</i>"
      else
        html = "<s>" + html + "</s>"
      return open + html + close
    }
    }

    return ""
  }

  // Recursive calls render a substring. Tokens are still computed against the
  // full document text in the callers; here we only need the slice's own
  // tokens, so re-run collection on the substring with the same definitions.
  property var _renderSource: ""

  function textSlice(start, end) {
    const source = root._renderSource || ""
    return source.substring(start, end)
  }

  function render(text, syntaxVisible, definitions, footnotes) {
    const value = text === undefined || text === null ? "" : String(text)
    if (value.length === 0)
      return ""

    const previousSource = root._renderSource
    root._renderSource = value

    const defs = mergedDefinitions(value, definitions)
    const tokens = collectTokens(value, defs)
    let out = ""
    let pos = 0

    for (let i = 0; i < tokens.length; ++i) {
      const token = tokens[i]
      if (token.start < pos)
        continue
      out += preserve(value.substring(pos, token.start))
      out += tokenHtml(token, !!syntaxVisible, defs, footnotes)
      pos = token.end
    }
    out += preserve(value.substring(pos))

    root._renderSource = previousSource
    return out
  }

  // ---------------------------------------------------------------------------
  // Offset mapping
  // ---------------------------------------------------------------------------

  function hiddenTokens(text, definitions, footnotes) {
    const value = text === undefined || text === null ? "" : String(text)
    if (value.length === 0)
      return []

    const tokens = collectTokens(value, mergedDefinitions(value, definitions))
    const result = []

    for (let i = 0; i < tokens.length; ++i) {
      const token = tokens[i]
      const data = token.data

      switch (token.kind) {
      case "definition":
      case "footnoteDefinition":
        result.push({ start: token.start, end: token.end, contentLen: 0 })
        break
      case "footnoteRef": {
        const note = footnotes && footnotes[data.label]
        result.push({ start: token.start, end: token.end,
                      contentLen: note ? String(note.index).length
                                       : data.raw.length })
        break
      }
      case "quote":
        result.push({ start: token.start, end: token.end,
                      contentLen: data.levels * 2 })
        break
      case "emojiShortcode":
        result.push({ start: token.start, end: token.end,
                      contentLen: data.emoji.length })
        break
      case "mention":
        result.push({ start: token.start, end: token.end,
                      contentLen: data.raw.length })
        break
      case "math": {
        const delimLen = data.delimiter.length
        result.push({ start: token.start, end: token.start + delimLen,
                      contentLen: 0 })
        result.push({ start: token.end - delimLen, end: token.end,
                      contentLen: 0 })
        break
      }
      case "image":
        result.push({ start: token.start, end: token.end, contentLen: 0 })
        break
      case "code":
        result.push({ start: token.start, end: data.ticks.length + token.start,
                      contentLen: 0 })
        result.push({ start: token.end - data.ticks.length, end: token.end,
                      contentLen: 0 })
        break
      case "link":
      case "autolink": {
        const contentLen = token.kind === "link"
            ? data.label.length
            : data.value.length
        result.push({ start: token.start, end: data.labelStart !== undefined
                      ? data.labelStart : token.start + 1, contentLen: 0 })
        result.push({ start: data.labelEnd !== undefined
                      ? data.labelEnd : token.end - 1,
                      end: token.end, contentLen: 0 })
        break
      }
      case "html":
        if (data.selfClosing) {
          result.push({ start: token.start, end: token.end, contentLen: 4 })
        } else {
          result.push({ start: token.start, end: data.openEnd, contentLen: 0 })
          result.push({ start: data.closeStart, end: token.end, contentLen: 0 })
        }
        break
      case "escape":
      case "hardbreak":
        result.push({ start: token.start, end: token.start + 1, contentLen: 0 })
        break
      case "entity":
        result.push({ start: token.start, end: token.end,
                      contentLen: data.decoded.length })
        break
      case "bold":
      case "italic":
      case "boldItalic":
      case "strike":
        result.push({ start: data.openStart, end: data.openEnd, contentLen: 0 })
        result.push({ start: data.closeStart, end: data.closeEnd, contentLen: 0 })
        break
      }
    }

    result.sort(function(a, b) { return a.start - b.start })
    return result
  }

  function rawToRenderedIndex(raw, rawIndex, definitions, footnotes) {
    if (!raw || rawIndex <= 0)
      return 0

    const tokens = hiddenTokens(raw, definitions, footnotes)
    let delta = 0

    for (let i = 0; i < tokens.length; ++i) {
      const token = tokens[i]
      if (rawIndex <= token.start)
        break
      if (rawIndex < token.end) {
        const within = rawIndex - token.start
        return token.start - delta + Math.min(within, token.contentLen)
      }
      delta += (token.end - token.start) - token.contentLen
    }

    return rawIndex - delta
  }

  // NOTE: renderedToRawIndex() was removed as dead code. The only live caller
  // was TextBlock's equally dead wrapper; caret/selection mapping goes through
  // TextBlock.viewerToRawIndex(), which uses the focus-aware viewerTokens()
  // (emoji glyphs while focused, hidden syntax markers plus style sentinels
  // otherwise). Routing that through hiddenTokens() here would drop the
  // focused-mode mapping and regress the caret, so the duplicate was deleted
  // rather than wired in.
}
