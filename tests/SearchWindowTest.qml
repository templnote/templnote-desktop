import QtQuick
import QtQuick.Controls
import TemplnoteSearchTest
import "qml"

ApplicationWindow {
  id: window

  width: 900
  height: 700

  visible: true
  title: "Templnote global search tests"
  color: Theme.background

  BlockModel {
    id: blockModel
  }

  // Records the calls SearchWindow makes after Enter. This avoids pulling the
  // complete editor into the search regression test.
  QtObject {
    id: editorStub

    objectName: "editorStub"

    property var lastMatches: []
    property int lastActive: -1
    property var focusedMatch: null
    property bool navigationActive: false

    function setSearchMatches(matches, activeIndex) {
      lastMatches = matches
      lastActive = activeIndex
    }

    function setSearchNavigationActive(active) {
      navigationActive = active
    }

    function focusSearchMatch(match) {
      focusedMatch = match
    }
  }

  SearchWindow {
    id: searchWindow

    objectName: "searchWindow"

    blockModel: blockModel
    documentList: null
    editor: editorStub
    resultsModel: fakeResults
    searchIndex: null
  }
}
