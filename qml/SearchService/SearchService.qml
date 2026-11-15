import QtQuick

// Base type for the fuzzy finding services in this folder.
//
// Every service implements search(model, query) and returns an array of
// match objects shaped as:
//   { row, blockId, start, length }
// `row` is only a convenience for scrolling. `blockId` is the durable
// reference because rows shift whenever blocks are inserted or removed.
QtObject {
  id: root

  function search(model, query) {
    return []
  }
}
