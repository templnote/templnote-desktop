import QtQuick

// Small formatting helper shared by the global search results list and the
// preview pane. Ranges are { start, length } offsets into the supplied text.
QtObject {
  id: root

  function escapeHtml(text) {
    return String(text)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;")
  }

  function formatText(text) {
    return root.escapeHtml(text).replace(/\n/g, "<br/>")
  }

  function highlight(text, ranges, highlightColor) {
    if (text === undefined || text === null)
      return ""

    const source = String(text)

    if (!ranges || ranges.length === 0)
      return root.formatText(source)

    const color = highlightColor !== undefined && highlightColor !== null
                ? highlightColor
                : "#ffe082"

    let html = ""
    let cursor = 0

    for (let i = 0; i < ranges.length; ++i) {
      const range = ranges[i]
      const start = Math.max(cursor,
                             Math.min(source.length, Number(range.start)))
      const end = Math.max(start,
                           Math.min(source.length,
                                    Number(range.start) + Number(range.length)))

      if (start >= end)
        continue

      html += root.formatText(source.substring(cursor, start))
      html += '<span style="background-color:' + color + ';">'
              + root.formatText(source.substring(start, end)) + '</span>'
      cursor = end
    }

    html += root.formatText(source.substring(cursor))
    return html
  }

  // QML colors expose float components. Rich text background colors are
  // easier to use as solid #rrggbb values, so blend the accent with the
  // surrounding surface instead of relying on rgba() support.
  function blendColor(foreground, background, amount) {
    const alpha = Math.max(0, Math.min(1, Number(amount)))
    const channel = function(foregroundChannel, backgroundChannel) {
      return Math.round((foregroundChannel * alpha +
                         backgroundChannel * (1 - alpha)) * 255)
    }

    const red = channel(foreground.r, background.r)
    const green = channel(foreground.g, background.g)
    const blue = channel(foreground.b, background.b)

    return "#" + red.toString(16).padStart(2, "0")
               + green.toString(16).padStart(2, "0")
               + blue.toString(16).padStart(2, "0")
  }
}
