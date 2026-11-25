import QtQuick
import QtQuick.Controls
import Templnote

// Display math block. The raw LaTeX stays in the document; a lightweight
// renderer produces styled text for the common notation subset.
Item {
  id: root

  property string blockId: ""
  property int blockType: 14
  property string content: ""
  property int headingLevel: 0
  property int numberIndex: 1
  property string alignment: "left"
  property bool isChecked: false
  property bool showPlaceholder: false
  property var blockMetadata: null
  property bool isBlockHovered: false
  property var model: null
  property var searchHighlights: []
  property bool searchNavigationActive: false

  readonly property string editorText: root.content
  readonly property bool isFocused: sourceArea.activeFocus || root.activeFocus
  readonly property real firstLineHeight: 32

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
  signal focusRequested(int position)
  signal searchNavigated(int direction)
  signal searchDismissed()
  signal searchEditStarted()

  implicitWidth: parent ? parent.width : 600
  implicitHeight: editing ? sourceFlickable.height + 34 : previewText.implicitHeight + 40

  property bool editing: false
  property string draft: ""

  function focusEditor(position) {
    editing = true
    draft = root.content
    sourceArea.text = draft
    sourceArea.cursorPosition = position >= 0 ? Math.min(position, sourceArea.length) : sourceArea.length
    sourceArea.forceActiveFocus()
  }

  function refreshFromModel() {
    if (!editing)
      draft = root.content
  }

  function commit() {
    const value = sourceArea.text
    if (value !== root.content && root.model)
      root.model.updateBlockText(blockId, value)
    editing = false
  }

  Rectangle {
    anchors.fill: parent
    radius: 8
    color: Theme.surfaceAlt
    border.width: 1
    border.color: Theme.border
  }

  Row {
    id: toolbar
    anchors { top: parent.top; left: parent.left; margins: 8 }
    spacing: 8

    Text {
      anchors.verticalCenter: parent.verticalCenter
      text: "MATH"
      color: Theme.textFaint
      font.pixelSize: 10
      font.bold: true
      font.letterSpacing: 1.2
    }

    Button {
      id: editButton
      width: 24
      height: 20
      padding: 0
      flat: true
      text: editing ? "✓" : "✎"
      font.pixelSize: 12
      Accessible.name: editing ? "Apply math" : "Edit math"
      onClicked: {
        if (editing)
          root.commit()
        else
          root.focusEditor(-1)
      }
    }
  }

  Text {
    id: previewText
    anchors {
      top: toolbar.bottom
      left: parent.left
      right: parent.right
      margins: 12
    }
    visible: !editing
    text: MathText.toHtml(root.content)
    textFormat: Text.RichText
    wrapMode: Text.Wrap
    font.pixelSize: 18
    color: Theme.text
    horizontalAlignment: root.alignment === "center"
                         ? Text.AlignHCenter
                         : (root.alignment === "right" ? Text.AlignRight : Text.AlignLeft)
    MouseArea {
      anchors.fill: parent
      cursorShape: Qt.IBeamCursor
      onClicked: root.focusEditor(-1)
    }
  }

  Flickable {
    id: sourceFlickable
    anchors {
      top: toolbar.bottom
      left: parent.left
      right: parent.right
      bottom: parent.bottom
      margins: 12
    }
    visible: editing
    contentHeight: Math.max(40, sourceArea.implicitHeight)
    boundsBehavior: Flickable.StopAtBounds

    TextArea {
      id: sourceArea
      width: parent.width
      font.pixelSize: 14
      font.family: "monospace"
      color: Theme.text
      wrapMode: TextEdit.Wrap
      selectByMouse: true
      placeholderText: "LaTeX, e.g. \\frac{a}{b} + x^2"
      onEditingFinished: root.commit()
      Keys.onEscapePressed: {
        draft = root.content
        editing = false
      }
    }
  }
}
