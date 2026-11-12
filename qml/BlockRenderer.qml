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

  color: "#191C20"
  radius: 6

  Column {
    id: contentColumn
    anchors.fill: parent
    anchors.margins: 12
    spacing: 5

    // Text {
    //   text: "ID: " + root.blockId
    //   color: "#8B8B98"
    //   font.pixelSize: 10
    // }

    TextArea {
      id: editor

      width: parent.width

      color: "#F4F4F5"
      selectionColor: "#4F46E5"
      wrapMode: TextArea.Wrap

      font.pixelSize: root.blockType === 1 ? 30 : 16
      font.weight: root.blockType === 1 ? Font.Bold : Font.Normal

      background: null

      Component.onCompleted: {
        editor.text = root.content
      }


      onEditingFinished: {
        root.model.updateBlockText(root.blockId, text)
      }

      Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
          event.accepted = true
          root.enterPressed()
        }

        if (event.key === Qt.Key_Backspace && editor.cursorPosition === 0) {
          event.accepted = true
          root.backspacePressed()
        }
      }
    }

    // Text {
    //   text: "TYPE: " + root.blockType
    //   color: "#A78BFA"
    //   font.pixelSize: 10
    // }
  }

  Component.onCompleted: {
    console.log("ID:", root.blockId)
    console.log("CONTENT:", root.content)
    console.log("TYPE:", root.blockType)
  }
}
