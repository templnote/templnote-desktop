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
  property string alignment: "left"
  property bool isChecked: false
  property string calloutType: "note"
  property string codeLanguage: ""
  property string imageSource: ""
  property real imageWidthRatio: 1.0
  property bool isBlockHovered: false
  property bool showPlaceholder: false
  property var model: null

  readonly property var block: loader.item
  readonly property int cursorPosition: block ? block.cursorPosition : 0
  readonly property string editorText: block ? block.editorText : ""
  readonly property bool isFocused: block ? block.isFocused : false
  readonly property real firstLineHeight: block ? block.firstLineHeight : 24

  signal enterPressed()
  signal backspacePressed()
  signal focusRequested(int position)
  signal undoRequested()
  signal redoRequested()
  signal moveFocusRequested(int direction, real xPosition)
  signal caretMoved(int position, real cursorGlobalY, real cursorHeight)
  signal commandMenuRequested(string query, real xPosition, real yPosition, real cursorHeight)
  signal commandMenuDismissed()
  signal commandMenuAccepted()
  signal commandMenuNavigated(int direction)
  signal selectionChanged(bool hasSelection, real xPosition, real yPosition, real cursorHeight)
  property bool commandMenuActive: false

  // Search highlight ranges for this block: [{ start, length, active }].
  property var searchHighlights: []

  // Set while the editor is in global-search navigation mode.
  property bool searchNavigationActive: false

  signal searchNavigated(int direction)
  signal searchDismissed()
  signal searchEditStarted()
  signal focusDelegateRequested(int row, int position)

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

  function toggleFormat(type) {
    if (block && loader.status === Loader.Ready && typeof block.toggleFormat === "function")
      block.toggleFormat(type)
  }

  function toggleScriptFormat(type) {
    if (block && loader.status === Loader.Ready && typeof block.toggleScriptFormat === "function")
      block.toggleScriptFormat(type)
  }

  function isFormatActive(type) {
    if (block && loader.status === Loader.Ready && typeof block.isFormatActive === "function")
      return block.isFormatActive(type)
    return false
  }

  function notifySelection() {
    if (block && loader.status === Loader.Ready && typeof block.notifySelection === "function")
      block.notifySelection()
  }

  function requestFocus(position) {
    pendingFocus = position

    if (loader.status === Loader.Ready && loader.item)
      applyPendingFocus()
    root.focusRequested(position)
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
    case 4: // BlockType::Todo
      return todoComponent
    case 5: // BlockType::Quote
      return quoteComponent
    case 6: // BlockType::Code
      return codeComponent
    case 7: // BlockType::Divider
      return dividerComponent
    case 8: // BlockType::Image
      return imageComponent
    case 10: // BlockType::Callout
      return calloutComponent
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

  Component {
    id: todoComponent
    TodoBlock {}
  }

  Component {
    id: quoteComponent
    QuoteBlock {}
  }

  Component {
    id: codeComponent
    CodeBlock {}
  }

  Component {
    id: dividerComponent
    DividerBlock {}
  }

  Component {
    id: imageComponent
    ImageBlock {}
  }

  Component {
    id: calloutComponent
    CalloutBlock {}
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
      item.alignment = Qt.binding(function() { return root.alignment })
      item.isChecked = Qt.binding(function() { return root.isChecked })
      if ("calloutType" in item)
        item.calloutType = Qt.binding(function() { return root.calloutType })
      if ("codeLanguage" in item)
        item.codeLanguage = Qt.binding(function() { return root.codeLanguage })
      if ("imageSource" in item)
        item.imageSource = Qt.binding(function() { return root.imageSource })
      if ("imageWidthRatio" in item)
        item.imageWidthRatio = Qt.binding(function() { return root.imageWidthRatio })
      if ("isBlockHovered" in item)
        item.isBlockHovered = Qt.binding(function() { return root.isBlockHovered })
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
      item.selectionChanged.connect(root.selectionChanged)
      // Not every renderer hosts the slash-command menu (callouts and code
      // blocks do not), so only bind the property when it exists.
      if ("commandMenuActive" in item)
        item.commandMenuActive = Qt.binding(function() { return root.commandMenuActive })
      item.searchNavigated.connect(root.searchNavigated)
      item.searchDismissed.connect(root.searchDismissed)
      item.searchEditStarted.connect(root.searchEditStarted)
      if (item.focusDelegateRequested)
        item.focusDelegateRequested.connect(root.focusDelegateRequested)

      root.applyPendingFocus()
    }
  }
}
