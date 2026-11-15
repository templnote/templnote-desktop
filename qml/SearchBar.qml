import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import "SearchService"

// Floating find bar for the editor workspace. It owns the query state and
// delegates matching to a search service; the editor only receives the
// resulting match list and the active match index.
Rectangle {
  id: root

  property var model: null
  property var editor: null

  // Match objects are { row, blockId, start, length }.
  property var matches: []
  property int currentIndex: -1
  property var previousFocus: null

  property alias query: searchField.text

  readonly property int matchCount: matches.length

  signal closed()

  LocalSearchService {
    id: localService
  }

  width: 420
  height: 32
  radius: 8
  color: Theme.surface
  border.width: 1
  border.color: Theme.border
  visible: false
  z: 4

  function open() {
    if (root.visible) {
      searchField.forceActiveFocus()
      searchField.selectAll()
      return
    }

    root.previousFocus = Window.window ? Window.window.activeFocusItem : null
    if (root.editor && root.editor.setSearchActive)
      root.editor.setSearchActive(true)
    root.visible = true
    searchField.forceActiveFocus()
    searchField.selectAll()
  }

  // `restoreFocus` is false when the caller is about to move focus into the
  // editor (Enter), and true for Escape/close-button dismissal.
  function close(restoreFocus) {
    if (!root.visible)
      return

    const shouldRestore = restoreFocus === undefined ? true : restoreFocus
    const focusTarget = root.previousFocus

    root.visible = false
    root.previousFocus = null
    searchField.text = ""
    root.matches = []
    root.currentIndex = -1

    if (root.editor)
      root.editor.clearSearch()

    if (root.editor && root.editor.setSearchActive)
      root.editor.setSearchActive(false)

    if (shouldRestore && focusTarget)
      focusTarget.forceActiveFocus()

    root.closed()
  }

  function runSearch() {
    if (!root.visible || !root.model) {
      root.matches = []
      root.currentIndex = -1
      return
    }

    root.matches = localService.search(root.model, root.query)

    if (root.matches.length === 0) {
      root.currentIndex = -1
      if (root.editor)
        root.editor.setSearchMatches([], -1)
      return
    }

    // Start from the first match at or after the caret, wrapping to the top
    // when the caret is past the last occurrence.
    let startIndex = 0

    if (root.editor) {
      const caret = root.editor.caretContext()
      const found = root.matches.findIndex(function(match) {
        return match.row > caret.row
            || (match.row === caret.row && match.start >= caret.position)
      })

      if (found >= 0)
        startIndex = found
    }

    root.selectMatch(startIndex)
  }

  function selectMatch(index) {
    if (root.matches.length === 0) {
      root.currentIndex = -1
      if (root.editor)
        root.editor.setSearchMatches([], -1)
      return
    }

    const count = root.matches.length
    let next = index % count

    if (next < 0)
      next += count

    root.currentIndex = next

    if (root.editor)
      root.editor.setSearchMatches(root.matches, next)
  }

  function move(direction) {
    if (root.matches.length === 0)
      return

    root.selectMatch(root.currentIndex + direction)
  }

  function accept() {
    if (root.currentIndex < 0 || root.currentIndex >= root.matches.length) {
      root.close(true)
      return
    }

    const match = root.matches[root.currentIndex]
    const targetEditor = root.editor

    // Do not restore the old focus: the caret is about to move into the
    // matched block.
    root.close(false)

    if (targetEditor)
      targetEditor.focusSearchMatch(match)
  }

  onVisibleChanged: {
    if (visible)
      root.runSearch()
  }

  IconImage {
    id: searchIcon

    anchors.left: parent.left
    anchors.leftMargin: 10
    anchors.verticalCenter: parent.verticalCenter

    width: 15
    height: 15
    source: "qrc:/qt/qml/Templnote/assets/icons/ic_fluent_search_20_regular.svg"
    sourceSize.width: 15
    sourceSize.height: 15
    color: Theme.textFaint
  }

  Row {
    id: controls

    anchors.right: parent.right
    anchors.rightMargin: 8
    anchors.verticalCenter: parent.verticalCenter

    spacing: 2

    Item {
      width: 20
      height: 20
      opacity: root.matchCount === 0 ? 0.35 : 1

      Text {
        anchors.centerIn: parent
        text: "↑"
        color: previousMouse.containsMouse ? Theme.accent : Theme.textMuted
        font.pixelSize: 14
      }

      MouseArea {
        id: previousMouse
        anchors.fill: parent
        enabled: root.matchCount > 0
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.move(-1)
      }
    }

    Item {
      width: 20
      height: 20
      opacity: root.matchCount === 0 ? 0.35 : 1

      Text {
        anchors.centerIn: parent
        text: "↓"
        color: nextMouse.containsMouse ? Theme.accent : Theme.textMuted
        font.pixelSize: 14
      }

      MouseArea {
        id: nextMouse
        anchors.fill: parent
        enabled: root.matchCount > 0
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.move(1)
      }
    }

    Item {
      width: 20
      height: 20

      Text {
        anchors.centerIn: parent
        text: "✕"
        color: closeMouse.containsMouse ? Theme.accent : Theme.textMuted
        font.pixelSize: 11
      }

      MouseArea {
        id: closeMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.close(true)
      }
    }
  }

  Text {
    id: counter

    anchors.right: controls.left
    anchors.rightMargin: 8
    anchors.verticalCenter: parent.verticalCenter

    width: 68
    visible: searchField.text.length > 0
    horizontalAlignment: Text.AlignRight
    color: Theme.textMuted
    font.pixelSize: 12
    text: root.matchCount === 0
          ? "No results"
          : (root.currentIndex + 1) + " of " + root.matchCount
  }

  TextField {
    id: searchField

    anchors.left: searchIcon.right
    anchors.leftMargin: 8
    anchors.right: counter.left
    anchors.rightMargin: 10
    anchors.verticalCenter: parent.verticalCenter

    height: root.height - 8
    padding: 0
    topPadding: 0
    bottomPadding: 0

    color: Theme.text
    selectionColor: Theme.accent
    selectedTextColor: Theme.accentText
    placeholderText: "Search in document"
    placeholderTextColor: Theme.textFaint
    font.pixelSize: 13
    selectByMouse: true
    background: null

    onTextChanged: root.runSearch()
    onAccepted: root.accept()

    Keys.onUpPressed: function(event) {
      event.accepted = true
      root.move(-1)
    }

    Keys.onDownPressed: function(event) {
      event.accepted = true
      root.move(1)
    }

    Keys.onEscapePressed: function(event) {
      event.accepted = true
      root.close(true)
    }
  }
}
