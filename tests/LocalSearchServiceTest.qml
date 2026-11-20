import QtQuick
import TemplnoteSearchTest

Item {
  id: root

  property var model: null
  property var serviceObj: service

  signal finished(var token, var matches)

  LocalSearchService {
    id: service
  }

  Connections {
    target: service

    function onSearchFinished(token, matches) {
      root.finished(token, matches)
    }
  }

  function run(query, token) {
    service.searchAsync(root.model, query, token)
  }

  function syncRun(query) {
    return service.search(root.model, query)
  }
}
