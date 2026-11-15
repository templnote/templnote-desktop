import QtQuick

// Case-insensitive substring search over the current document model.
//
// Matches are returned in document order and do not overlap. A block with the
// text "cat cat" contributes two matches for the query "cat".
SearchService {
  id: root

  // Role ids from Templnote::BlockModel::Roles
  // (src/model/BlockModel.h). QAbstractItemModel::roleNames() is not exposed
  // to QML, so the search service uses the stable role numbers directly.
  readonly property int blockIdRole: 257 // Qt.UserRole + 1
  readonly property int contentRole: 260 // Qt.UserRole + 4

  function search(model, query) {
    const results = []

    if (!model || query === undefined || query === null || query.length === 0)
      return results

    const needle = String(query).toLowerCase()
    const rowCount = model.count === undefined ? 0 : model.count

    for (let row = 0; row < rowCount; ++row) {
      const index = model.index(row, 0)
      const rawContent = model.data(index, root.contentRole)
      const rawBlockId = model.data(index, root.blockIdRole)

      if (rawContent === undefined || rawContent === null)
        continue
      if (rawBlockId === undefined || rawBlockId === null)
        continue

      const content = String(rawContent)
      const blockId = String(rawBlockId)

      if (content.length === 0 || blockId.length === 0)
        continue

      const haystack = content.toLowerCase()
      let from = 0

      while (from <= haystack.length - needle.length) {
        const start = haystack.indexOf(needle, from)

        if (start < 0)
          break

        results.push({
          row: row,
          blockId: blockId,
          start: start,
          length: needle.length
        })

        from = start + needle.length
      }
    }

    return results
  }

  // Multi-term variant used after opening a global search result. Every
  // whitespace-separated term is highlighted and overlapping ranges are
  // merged so the editor paints each character once.
  function searchTerms(model, query) {
    const results = []

    if (!model || query === undefined || query === null)
      return results

    const terms = String(query).toLowerCase().split(/\s+/).filter(function(term) {
      return term.length > 0
    })

    if (terms.length === 0)
      return results

    const rowCount = model.count === undefined ? 0 : model.count

    for (let row = 0; row < rowCount; ++row) {
      const index = model.index(row, 0)
      const rawContent = model.data(index, root.contentRole)
      const rawBlockId = model.data(index, root.blockIdRole)

      if (rawContent === undefined || rawContent === null)
        continue
      if (rawBlockId === undefined || rawBlockId === null)
        continue

      const content = String(rawContent)
      const blockId = String(rawBlockId)

      if (content.length === 0 || blockId.length === 0)
        continue

      const haystack = content.toLowerCase()
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
        results.push({
          row: row,
          blockId: blockId,
          start: merged[i].start,
          length: merged[i].length
        })
      }
    }

    return results
  }
}
