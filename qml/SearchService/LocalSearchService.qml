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
  // Lazily built copy used only by the synchronous API kept for tests.
  property var loweredTexts: []
  property int loweredCacheId: -1

  // Last async find-bar query. dataChanged patches the worker snapshot but
  // produces no searchFinished of its own, so an open find bar would keep
  // showing stale matches until the next keystroke without a re-run.
  property string activeQuery: ""
  property var activeToken: null
  property var activeModel: null

  Timer {
    id: recheckTimer
    interval: 120
    repeat: false
    onTriggered: root.recheckActiveQuery()
  }

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

  // Normal text edits emit dataChanged for a single row. Patch just that row
  // in the worker snapshot instead of re-sending (and re-lowercasing) the
  // whole corpus on every revision. Structural changes cannot be patched, so
  // they mark the cache stale and the next query rebuilds it.
  // Only create the connections once there is a real model; a Connections with
  // a null target logs a warning at component creation time.
  Component {
    id: cachedModelConnections

    Connections {
      target: root.cachedModel

      function onDataChanged(topLeft, bottomRight, roles) {
        root.updateCachedRows(root.cachedModel, topLeft.row, bottomRight.row,
                              roles)
      }
      function onRowsInserted() { root.invalidateCache() }
      function onRowsRemoved() { root.invalidateCache() }
      function onModelReset() { root.invalidateCache() }
    }
  }

  Loader {
    active: root.cachedModel !== null
    sourceComponent: cachedModelConnections
  }

  function invalidateCache() {
    root.cachedModel = null
    root.cachedRevision = -1
    root.loweredCacheId = -1
    // A structural change also invalidates what an open find bar is showing.
    if (root.activeQuery.length > 0)
      recheckTimer.restart()
  }

  function clearActiveQuery() {
    recheckTimer.stop()
    root.activeQuery = ""
    root.activeToken = null
    root.activeModel = null
  }

  // Re-runs the most recent find query after the worker snapshot changed.
  // The caller's token is reused so its existing reply handler accepts the
  // result; a newer query replaces activeToken and makes this one obsolete.
  function recheckActiveQuery() {
    if (root.activeQuery.length === 0 || root.activeToken === null ||
        !root.activeModel)
      return

    // ensureCache() rebuilds (and re-sends) the snapshot when a structural
    // change invalidated it. WorkerScript messages keep their send order, so
    // the search below runs against the fresh cache.
    root.ensureCache(root.activeModel)
    worker.sendMessage({
      kind: "search",
      token: root.activeToken,
      cacheId: root.cacheId,
      needle: root.activeQuery.toLowerCase()
    })
  }

  function updateCachedRows(model, first, last, roles) {
    if (root.cachedModel !== model || root.cachedTexts.length === 0)
      return

    if (roles && roles.length > 0) {
      let relevant = false
      for (let r = 0; r < roles.length; ++r) {
        if (roles[r] === root.contentRole || roles[r] === root.blockIdRole) {
          relevant = true
          break
        }
      }
      if (!relevant)
        return
    }

    last = Math.min(last, root.cachedTexts.length - 1)
    if (first < 0 || last < first)
      return

    const ids = []
    const texts = []

    for (let row = first; row <= last; ++row) {
      const index = model.index(row, 0)
      const rawContent = model.data(index, root.contentRole)
      const rawBlockId = model.data(index, root.blockIdRole)
      const id = rawBlockId === undefined || rawBlockId === null
                 ? "" : String(rawBlockId)
      const text = rawContent === undefined || rawContent === null
                   ? "" : String(rawContent)

      root.cachedBlockIds[row] = id
      root.cachedTexts[row] = text
      ids.push(id)
      texts.push(text)
    }

    // Keep the revision in step so ensureCache() does not rebuild the whole
    // snapshot on the next keystroke, and invalidate the sync-only copy.
    if (model.contentRevision !== undefined)
      root.cachedRevision = model.contentRevision
    root.loweredCacheId = -1

    worker.sendMessage({
      kind: "updateRows",
      cacheId: root.cacheId,
      first: first,
      texts: texts,
      ids: ids
    })

    // The snapshot changed; update the matches of a query that is still open.
    recheckTimer.restart()
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
      // Send raw text to the worker; it lowercases its own copy there. The GUI
      // thread must not pay O(document) case folding on every revision.
      texts[row] = rawContent === undefined || rawContent === null ? "" : String(rawContent)
    }

    root.cachedModel = model
    root.cachedRevision = revision
    root.cachedBlockIds = ids
    root.cachedTexts = texts
    ++root.cacheId
    root.loweredCacheId = -1

    worker.sendMessage({
      kind: "cache",
      cacheId: root.cacheId,
      texts: texts,
      ids: ids
    })
    return true
  }

  // Only the synchronous API (used by tests) needs lowercased text on this
  // thread, and even then it is built at most once per document revision.
  function loweredCache() {
    if (root.loweredCacheId === root.cacheId)
      return root.loweredTexts

    root.loweredTexts = root.cachedTexts.map(function(text) {
      return String(text).toLowerCase()
    })
    root.loweredCacheId = root.cacheId
    return root.loweredTexts
  }

  function search(model, query) {
    const results = []

    if (!model || query === undefined || query === null || query.length === 0)
      return results
    if (!root.ensureCache(model))
      return results

    const needle = String(query).toLowerCase()
    const texts = root.loweredCache()
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
    // Remember the live query so a later dataChanged patch can refresh it.
    // Stop any re-run scheduled for an older query to avoid a duplicate reply.
    recheckTimer.stop()
    root.activeQuery = String(query === undefined || query === null ? "" : query)
    root.activeToken = token
    root.activeModel = model

    if (!model || !root.ensureCache(model)) {
      root.searchFinished(token, [])
      return
    }

    const needle = root.activeQuery.toLowerCase()

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

    const texts = root.loweredCache()
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
