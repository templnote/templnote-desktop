import QtQuick

// Dispatcher only: picks the concrete block renderer from qml/blocks and
// forwards the properties, signals and helper functions used by the editor.
Item {
  id: root

  property string blockId: ""
  property int blockType: 0
  property string content: ""
  property int headingLevel: 0
  property int numberIndex: 1
  property bool showPlaceholder: false
  property var model: null

  readonly property var block: loader.item
  readonly property int cursorPosition: block ? block.cursorPosition : 0
  readonly property string editorText: block ? block.editorText : ""

  signal enterPressed()
  signal backspacePressed()
  signal undoRequested()
  signal redoRequested()
  signal moveFocusRequested(int direction, real xPosition)
  signal caretMoved(int position)
  signal commandMenuRequested(string query, real xPosition, real yPosition, real cursorHeight)
  signal commandMenuDismissed()
  signal commandMenuAccepted()
  signal commandMenuNavigated(int direction)
  property bool commandMenuActive: false

  // Search highlight ranges for this block: [{ start, length, active }].
  property var searchHighlights: []

  // Set while the editor is in global-search navigation mode.
  property bool searchNavigationActive: false

  signal searchNavigated(int direction)
  signal searchDismissed()
  signal searchEditStarted()

  property int pendingFocus: -1

  function focusEditor(position) {
    if (block && loader.status === Loader.Ready)
      block.focusEditor(position)
    else
      requestFocus(position)
  }

  function focusEditorAtX(xPosition) {
    if (block && loader.status === Loader.Ready)
      block.focusEditorAtX(xPosition)
  }

  function focusEditorRange(start, end) {
    if (block && loader.status === Loader.Ready)
      block.focusEditorRange(start, end)
  }

  function requestFocus(position) {
    pendingFocus = position

    if (loader.status === Loader.Ready && loader.item)
      applyPendingFocus()
  }

  function applyPendingFocus() {
    if (pendingFocus < 0 || !loader.item)
      return

    const position = pendingFocus
    pendingFocus = -1
    loader.item.focusEditor(position)
  }

  function refreshFromModel() {
    if (block)
      block.refreshFromModel()
  }

  function componentForType(type) {
    switch (type) {
    case 1: // BlockType::Heading
      return headingComponent
    case 2: // BlockType::Bullet
      return bulletComponent
    case 3: // BlockType::NumberedList
      return numberedComponent
    default: // BlockType::Paragraph, and future types until implemented
      return paragraphComponent
    }
  }

  implicitHeight: loader.item ? loader.item.implicitHeight : 0

  Component {
    id: paragraphComponent
    ParagraphBlock {}
  }

  Component {
    id: headingComponent
    HeadingBlock {}
  }

  Component {
    id: bulletComponent
    BulletBlock {}
  }

  Component {
    id: numberedComponent
    NumberedBlock {}
  }

  Loader {
    id: loader
    anchors.fill: parent
    sourceComponent: root.componentForType(root.blockType)

    onLoaded: {
      item.blockId = Qt.binding(function() { return root.blockId })
      item.blockType = Qt.binding(function() { return root.blockType })
      item.content = Qt.binding(function() { return root.content })
      item.headingLevel = Qt.binding(function() { return root.headingLevel })
      item.numberIndex = Qt.binding(function() { return root.numberIndex })
      item.showPlaceholder = Qt.binding(function() { return root.showPlaceholder })
      item.model = Qt.binding(function() { return root.model })
      item.searchHighlights = Qt.binding(function() { return root.searchHighlights })
      item.searchNavigationActive = Qt.binding(function() { return root.searchNavigationActive })

      item.enterPressed.connect(root.enterPressed)
      item.backspacePressed.connect(root.backspacePressed)
      item.focusRequested.connect(root.requestFocus)
      item.caretMoved.connect(root.caretMoved)
      item.undoRequested.connect(root.undoRequested)
      item.redoRequested.connect(root.redoRequested)
      item.moveFocusRequested.connect(root.moveFocusRequested)
      item.commandMenuRequested.connect(root.commandMenuRequested)
      item.commandMenuDismissed.connect(root.commandMenuDismissed)
      item.commandMenuAccepted.connect(root.commandMenuAccepted)
      item.commandMenuNavigated.connect(root.commandMenuNavigated)
      item.commandMenuActive = Qt.binding(function() { return root.commandMenuActive })
      item.searchNavigated.connect(root.searchNavigated)
      item.searchDismissed.connect(root.searchDismissed)
      item.searchEditStarted.connect(root.searchEditStarted)

      root.applyPendingFocus()
    }
  }
}
