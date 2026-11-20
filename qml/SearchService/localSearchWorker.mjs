// Worker-side of LocalSearchService. The GUI thread sends a lowercased text
// snapshot once per document revision, then only needles/tokens per keystroke.

let cachedCacheId = -1
let cachedTexts = []
let cachedIds = []

function findMatches(texts, ids, needle) {
  const matches = []
  const length = needle.length

  if (length === 0)
    return matches

  for (let row = 0; row < texts.length; ++row) {
    const haystack = texts[row]
    const blockId = ids[row]

    if (haystack.length === 0 || blockId.length === 0)
      continue

    let from = 0
    while (from <= haystack.length - length) {
      const start = haystack.indexOf(needle, from)
      if (start < 0)
        break

      matches.push({ row: row, blockId: blockId, start: start, length: length })
      from = start + length
    }
  }

  return matches
}

function findTermMatches(texts, ids, terms) {
  const matches = []

  for (let row = 0; row < texts.length; ++row) {
    const haystack = texts[row]
    const blockId = ids[row]

    if (haystack.length === 0 || blockId.length === 0)
      continue

    let ranges = []

    for (let t = 0; t < terms.length; ++t) {
      const needle = terms[t]
      let from = 0

      while (from <= haystack.length - needle.length) {
        const start = haystack.indexOf(needle, from)
        if (start < 0)
          break

        ranges.push({ start: start, length: needle.length })
        from = start + needle.length
      }
    }

    if (ranges.length === 0)
      continue

    ranges.sort(function(left, right) {
      if (left.start !== right.start)
        return left.start - right.start
      return right.length - left.length
    })

    const merged = []

    for (let i = 0; i < ranges.length; ++i) {
      const range = ranges[i]
      const last = merged.length > 0 ? merged[merged.length - 1] : null

      if (last && range.start <= last.start + last.length) {
        const end = Math.max(last.start + last.length,
                             range.start + range.length)
        last.length = end - last.start
      } else {
        merged.push({ start: range.start, length: range.length })
      }
    }

    for (let i = 0; i < merged.length; ++i) {
      matches.push({
        row: row,
        blockId: blockId,
        start: merged[i].start,
        length: merged[i].length
      })
    }
  }

  return matches
}

WorkerScript.onMessage = function(message) {
  if (message.kind === "cache") {
    cachedCacheId = message.cacheId
    cachedTexts = message.texts
    cachedIds = message.ids
    return
  }

  const hasCache = message.cacheId === cachedCacheId

  if (message.kind === "search") {
    WorkerScript.sendMessage({
      kind: "search",
      token: message.token,
      matches: hasCache ? findMatches(cachedTexts, cachedIds, message.needle)
                        : []
    })
    return
  }

  if (message.kind === "terms") {
    WorkerScript.sendMessage({
      kind: "terms",
      token: message.token,
      matches: hasCache ? findTermMatches(cachedTexts, cachedIds, message.terms)
                        : []
    })
  }
}
