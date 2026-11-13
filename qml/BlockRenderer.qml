import QtQuick
import QtQuick.Controls

Rectangle {
  id: root

  required property string blockId
  required property int blockType
  required property string content
  required property int headingLevel
  required property var model
  readonly property int cursorPosition: editor.cursorPosition
  readonly property string editorText: editor.text

  property bool transformingShortcut: false

  
  signal enterPressed()
  signal backspacePressed()

  function focusEditor(position) {
    editor.forceActiveFocus()

    if (position < 0)
      editor.cursorPosition = editor.length
    else
      editor.cursorPosition = position
    }

    function refreshFromModel() {
      editor.text = root.content
    }

  width: parent ? parent.width : 600
  implicitHeight: contentColumn.implicitHeight + 24

  // color: "#191C20"
  radius: 6

  Column {
    id: contentColumn
    anchors.fill: parent
    anchors.margins: 12
    spacing: 5

    TextArea {
      id: editor

      width: parent.width

      // color: "#F4F4F5"
      // selectionColor: "#4F46E5"
      wrapMode: TextArea.Wrap

      font.pixelSize: root.blockType === 1 ? Math.max(20, 34 - ((root.headingLevel - 1) * 4)) : 16
      font.weight: root.blockType === 1 ? Font.Bold : Font.Normal

      background: null

      Component.onCompleted: {
        editor.text = root.content
      }

      onTextChanged: {
        if (root.transformingShortcut)
        return

        if (root.blockType !== 0)
          return

        const headingMatch = /^(#{1,6}) /.exec(text)

        if (!headingMatch)
          return

        root.transformingShortcut = true

        const headingLevel =
          headingMatch[1].length

        const headingText =
          text.substring(headingLevel + 1)

        root.model.changeBlockType(
          root.blockId,
          1,
          headingLevel
        )

        editor.text = headingText
        editor.cursorPosition = headingText.length

        root.model.updateBlockText(
          root.blockId,
          headingText
        )

        root.transformingShortcut = false
      }


      onEditingFinished: {
        root.model.updateBlockText(root.blockId, text)
      }



      Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
          event.accepted = true
          root.enterPressed()
          return
        }

        if (event.key === Qt.Key_Backspace && editor.cursorPosition === 0) {
          event.accepted = true
          root.backspacePressed()
          return
        }
      }
    }

  }

  Component.onCompleted: {
    console.log("ID:", root.blockId)
    console.log("CONTENT:", root.content)
    console.log("TYPE:", root.blockType)
  }
}
