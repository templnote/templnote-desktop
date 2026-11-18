import QtQuick
import Templnote

Item {
  id: root

  property string blockId: ""
  property int blockType: 7
  property string content: ""
  property int headingLevel: 0
  property int numberIndex: 1
  property string alignment: "left"
  property bool isChecked: false
  property bool showPlaceholder: false
  property var model: null
  property var searchHighlights: []

  readonly property int cursorPosition: editor.cursorPosition
  readonly property string editorText: editor.text
  readonly property bool isFocused: editor.activeFocus
  readonly property real firstLineHeight: 24

  signal enterPressed()
  signal backspacePressed()
  signal undoRequested()
  signal redoRequested()
  signal moveFocusRequested(int direction, real xPosition)
  signal caretMoved(int position, real cursorGlobalY, real cursorHeight)
  signal commandMenuRequested(string query, real xPosition, real yPosition, real cursorHeight)
  signal commandMenuDismissed()
  signal commandMenuAccepted()
  signal commandMenuNavigated(int direction)
  signal selectionChanged(bool hasSelection, real xPosition, real yPosition, real cursorHeight)
  signal focusDelegateRequested(int row, int position)
  signal focusRequested(int position)
  signal searchNavigated(int direction)
  signal searchDismissed()
  signal searchEditStarted()
  property bool searchNavigationActive: false

  function focusEditor(position) {
    editor.focus = true
    editor.forceActiveFocus()
    const safePos = (position < 0) ? editor.length : Math.max(0, Math.min(position, editor.length))
    editor.cursorPosition = safePos
    updateCaretGeometry()
  }

  function focusEditorAtX(xPosition) {
    editor.focus = true
    editor.forceActiveFocus()
    const localX = Math.max(0, xPosition - editor.x)
    if (editor.text.length > 0) {
      editor.cursorPosition = editor.positionAt(localX, editor.height / 2)
    } else {
      editor.cursorPosition = 0
    }
    updateCaretGeometry()
  }

  function focusEditorRange(start, end) {
    editor.focus = true
    editor.forceActiveFocus()
    editor.select(start, end)
    updateCaretGeometry()
  }

  function refreshFromModel() {
    if (editor.text !== root.content && !editor.activeFocus) {
      editor.text = root.content.length > 0 ? root.content : "---"
    }
  }

  function updateCaretGeometry() {
    if (editor.activeFocus) {
      const cRect = editor.cursorRectangle
      const mapped = editor.mapToItem(null, cRect.x, cRect.y)
      root.caretMoved(editor.cursorPosition, mapped ? mapped.y : 0, Math.max(16, cRect.height))
    }
  }

  onContentChanged: {
    if (!editor.activeFocus) {
      editor.text = root.content.length > 0 ? root.content : "---"
    }
  }

  implicitWidth: parent ? parent.width : 400
  implicitHeight: editor.activeFocus ? Math.max(24, editor.implicitHeight) : 18

  // 1. UNPRINTED / RENDERED VIEW (when unfocused): displays a sleek horizontal line
  Item {
    id: renderedDivider
    anchors.fill: parent
    visible: opacity > 0.0
    opacity: editor.activeFocus ? 0.0 : 1.0
    z: editor.activeFocus ? 1 : 2

    Behavior on opacity {
      NumberAnimation { duration: 120 }
    }

    Rectangle {
      anchors.centerIn: parent
      width: parent.width - 24
      height: 1
      color: Theme.border
    }

    MouseArea {
      anchors.fill: parent
      cursorShape: Qt.PointingHandCursor
      onClicked: {
        editor.forceActiveFocus()
      }
    }
  }

  // 2. FOCUSED VIEW: displays editable syntax "---"
  // Keep visible: true at all times so Qt Quick Focus management never loses the item
  Item {
    id: focusedEditorContainer
    anchors.fill: parent
    anchors.leftMargin: 12
    anchors.rightMargin: 12
    opacity: editor.activeFocus ? 1.0 : 0.0
    z: editor.activeFocus ? 2 : 1

    Behavior on opacity {
      NumberAnimation { duration: 120 }
    }

    TextEdit {
      id: editor
      anchors.fill: parent
      verticalAlignment: TextEdit.AlignVCenter
      font.pixelSize: 15
      font.family: "monospace"
      color: Theme.textMuted
      selectByMouse: true
      selectionColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35)
      selectedTextColor: Theme.text
      cursorVisible: activeFocus

      Keys.priority: Keys.BeforeItem

      onActiveFocusChanged: {
        if (activeFocus) {
          if (editor.text.length === 0) {
            editor.text = root.content.length > 0 ? root.content : "---"
          }
          root.updateCaretGeometry()
        } else {
          // If unfocused and user left it as "---" or "***", ensure model is updated
          if (root.model && root.blockId.length > 0) {
            root.model.updateBlockText(root.blockId, editor.text)
          }
        }
      }

      onCursorPositionChanged: {
        root.updateCaretGeometry()
      }

      onTextChanged: {
        if (!activeFocus) return

        if (root.model && root.blockId.length > 0) {
          root.model.updateBlockText(root.blockId, editor.text)
        }
      }

      Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Enter || event.key === Qt.Key_Return) {
          event.accepted = true
          root.enterPressed()
          return
        }

        if (event.key === Qt.Key_Backspace && (editor.cursorPosition === 0 || editor.text.length === 0)) {
          event.accepted = true
          root.backspacePressed()
          return
        }

        if (event.key === Qt.Key_Up) {
          event.accepted = true
          const cRect = editor.cursorRectangle
          root.moveFocusRequested(-1, cRect.x + editor.x)
          return
        }

        if (event.key === Qt.Key_Down) {
          event.accepted = true
          const cRect = editor.cursorRectangle
          root.moveFocusRequested(1, cRect.x + editor.x)
          return
        }

        if (event.matches(StandardKey.Undo)) {
          event.accepted = true
          root.undoRequested()
          return
        }

        if (event.matches(StandardKey.Redo)) {
          event.accepted = true
          root.redoRequested()
          return
        }
      }

      Component.onCompleted: {
        editor.text = root.content.length > 0 ? root.content : "---"
      }
    }
  }
}
