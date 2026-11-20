import QtQuick
import QtQml

// Case-insensitive substring search over the current document model.
//
// Matches are returned in document order and do not overlap. A block with the
// text "cat cat" contributes two matches for the query "cat".
//
// search()/searchTerms() remain synchronous for callers that need an immediate
// answer. The async variants run the scan on a WorkerScript so typing never
// blocks the GUI thread.
Item {
  id: root

  // Role ids from Templnote::BlockModel::Roles
  // (src/model/BlockModel.h). QAbstractItemModel::roleNames() is not exposed
  // to QML, so the search service uses the stable role numbers directly.
  readonly property int blockIdRole: 257 // Qt.UserRole + 1
  readonly property int contentRole: 260 // Qt.UserRole + 4

  signal searchFinished(var token, var matches)
  signal searchTermsFinished(var token, var matches)

  property var cachedModel: null
  property int cachedRevision: -1
  property int cacheId: 0
  property var cachedBlockIds: []
  property var cachedTexts: []

  WorkerScript {
    id: worker

    source: "localSearchWorker.mjs"

    onMessage: function(message) {
      if (message.kind === "search")
        root.searchFinished(message.token, message.matches)
      else if (message.kind === "terms")
        root.searchTermsFinished(message.token, message.matches)
    }
  }

  // Lowercasing every block on every keystroke used to dominate local search.
  // The model publishes a revision number, so the cache is rebuilt only when
  // the document actually changes, and the snapshot is sent to the worker once.
  function ensureCache(model) {
    if (!model)
      return false

    const revision = model.contentRevision === undefined ? -2 : model.contentRevision
    if (root.cachedModel === model && root.cachedRevision === revision)
      return true

    const rowCount = model.count === undefined ? 0 : model.count
    const ids = new Array(rowCount)
    const texts = new Array(rowCount)

    for (let row = 0; row < rowCount; ++row) {
      const index = model.index(row, 0)
      const rawContent = model.data(index, root.contentRole)
      const rawBlockId = model.data(index, root.blockIdRole)
      ids[row] = rawBlockId === undefined || rawBlockId === null ? "" : String(rawBlockId)
      texts[row] = rawContent === undefined || rawContent === null ? "" : String(rawContent).toLowerCase()
    }

    root.cachedModel = model
    root.cachedRevision = revision
    root.cachedBlockIds = ids
    root.cachedTexts = texts
    ++root.cacheId

    worker.sendMessage({
      kind: "cache",
      cacheId: root.cacheId,
      texts: texts,
      ids: ids
    })
    return true
  }

  function search(model, query) {
    const results = []

    if (!model || query === undefined || query === null || query.length === 0)
      return results
    if (!root.ensureCache(model))
      return results

    const needle = String(query).toLowerCase()
    const texts = root.cachedTexts
    const ids = root.cachedBlockIds

    for (let row = 0; row < texts.length; ++row) {
      const haystack = texts[row]
      const blockId = ids[row]

      if (haystack.length === 0 || blockId.length === 0)
        continue

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

  function searchAsync(model, query, token) {
    if (!model || !root.ensureCache(model)) {
      root.searchFinished(token, [])
      return
    }

    const needle = String(query === undefined || query === null ? "" : query).toLowerCase()

    if (needle.length === 0) {
      root.searchFinished(token, [])
      return
    }

    worker.sendMessage({
      kind: "search",
      token: token,
      cacheId: root.cacheId,
      needle: needle
    })
  }

  // Multi-term variant used after opening a global search result. Every
  // whitespace-separated term is highlighted and overlapping ranges are
  // merged so the editor paints each character once.
  function searchTerms(model, query) {
    const results = []

    if (!model || query === undefined || query === null)
      return results
    if (!root.ensureCache(model))
      return results

    const terms = String(query).toLowerCase().split(/\s+/).filter(function(term) {
      return term.length > 0
    })

    if (terms.length === 0)
      return results

    const texts = root.cachedTexts
    const ids = root.cachedBlockIds

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

  function searchTermsAsync(model, query, token) {
    if (!model || !root.ensureCache(model)) {
      root.searchTermsFinished(token, [])
      return
    }

    const terms = String(query === undefined || query === null ? "" : query)
                       .toLowerCase()
                       .split(/\s+/)
                       .filter(function(term) {
                         return term.length > 0
                       })

    if (terms.length === 0) {
      root.searchTermsFinished(token, [])
      return
    }

    worker.sendMessage({
      kind: "terms",
      token: token,
      cacheId: root.cacheId,
      terms: terms
    })
  }
}
