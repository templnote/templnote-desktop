import QtQuick
import QtQuick.Controls

Item {
  id: root

  // Provided by the BlockRenderer dispatcher.
  property string blockId: ""
  property int blockType: 0
  property string content: ""
  property int headingLevel: 0
  property int numberIndex: 1
  property var model: null

  // Appearance overrides for each concrete block type.
  property bool showBullet: false
  property bool showNumbered: false
  property string numberPrefix: numberIndex + "."
  property int verticalPadding: 12
  property int horizontalPadding: 12
  property int rowHeight: 0
  property real fontPixelSize: 16
  property bool fontBold: false

  readonly property int cursorPosition: editor.cursorPosition
  readonly property string editorText: editor.text

  property bool transformingShortcut: false
  property bool syncingFromModel: false

  signal enterPressed()
  signal backspacePressed()
  signal focusRequested(int position)
  signal undoRequested()
  signal redoRequested()

  function focusEditor(position) {
    editor.focus = true
    editor.forceActiveFocus()

    if (position < 0)
      editor.cursorPosition = editor.length
    else
      editor.cursorPosition = position

    // A freshly created TextArea can receive focus one event-loop turn after
    // the delegate is created. Re-apply the caret state after that handoff.
    Qt.callLater(function() {
      if (editor.activeFocus) {
        editor.cursorVisible = true
        if (position < 0)
          editor.cursorPosition = editor.length
        else
          editor.cursorPosition = position
      }
    })
  }

  function refreshFromModel() {
    editor.text = root.content
  }

  onContentChanged: {
    // The dispatcher assigns the content after the loader has created
    // this item, so keep the editor in sync with the model.
    if (editor.text === root.content)
      return

    root.syncingFromModel = true
    editor.text = root.content
    // Content changes coming from the model are undo/redo (or other
    // programmatic updates), so place the caret after the restored text.
    editor.cursorPosition = editor.length
    root.syncingFromModel = false
  }

  implicitHeight: contentColumn.implicitHeight + root.verticalPadding * 2

  // Clicking the block's padding/gutter should move the caret into this block
  // instead of leaving focus on the previously edited block.
  MouseArea {
    anchors.fill: parent
    acceptedButtons: Qt.LeftButton

    onClicked: {
      editor.forceActiveFocus()
      editor.cursorPosition = editor.length
    }
  }

  Column {
    id: contentColumn
    anchors.fill: parent
    anchors.leftMargin: root.horizontalPadding
    anchors.rightMargin: root.horizontalPadding
    anchors.topMargin: root.verticalPadding
    anchors.bottomMargin: root.verticalPadding
    spacing: (root.showBullet || root.showNumbered) ? 0 : 5

    Row {
      width: parent.width
      height: root.rowHeight > 0 ? root.rowHeight : editor.implicitHeight
      spacing: 0

      Item {
        // The bullet/number is drawn in its own gutter so the textarea can be
        // offset by the same amount for every block type, keeping the
        // text of all block types aligned.
        id: bulletGutter
        width: 24
        height: parent.height

        Text {
          visible: root.showBullet
          anchors.centerIn: parent

          text: "•"
          color: Theme.textMuted
          font.pixelSize: 22

          horizontalAlignment: Text.AlignHCenter
          verticalAlignment: Text.AlignVCenter
        }

        Text {
          visible: root.showNumbered
          anchors.centerIn: parent

          text: root.numberPrefix
          color: Theme.textMuted
          font.pixelSize: 14

          horizontalAlignment: Text.AlignHCenter
          verticalAlignment: Text.AlignVCenter
        }
      }

      Item {
        id: editorLine
        width: parent.width - bulletGutter.width
        height: parent.height
        clip: false

        TextArea {
          id: editor
          anchors.fill: parent

          wrapMode: TextArea.Wrap
          placeholderText: "start typing here"
          placeholderTextColor: Theme.textFaint

          color: Theme.text
          selectionColor: Theme.accent
          selectedTextColor: Theme.accentText
          selectByMouse: true

          font.pixelSize: root.fontPixelSize
          font.weight: root.fontBold ? Font.Bold : Font.Normal

          background: null

          Component.onCompleted: {
            editor.text = root.content
          }

          onTextChanged: {
            if (root.transformingShortcut || root.syncingFromModel)
              return

            // paragraph -> bullet
            if (root.blockType === 0 && text.startsWith("- ")) {
              root.transformingShortcut = true

              const bulletText = text.substring(2)

              root.model.changeBlockType(
                root.blockId,
                2,
                0
              )

              root.model.updateBlockText(
                root.blockId,
                bulletText
              )

              root.transformingShortcut = false

              // Changing the type makes the dispatcher reload the block
              // renderer, so focus has to move to the new editor.
              root.focusRequested(bulletText.length)
              return
            }

            // paragraph -> numbered list
            const numberedMatch = /^(\d+)\.\s/.exec(text)
            if (root.blockType === 0 && numberedMatch) {
              root.transformingShortcut = true

              const prefixLen = numberedMatch[0].length
              const numberedText = text.substring(prefixLen)

              root.model.changeBlockType(
                root.blockId,
                3,
                0
              )

              root.model.updateBlockText(
                root.blockId,
                numberedText
              )

              root.transformingShortcut = false

              root.focusRequested(numberedText.length)
              return
            }

            // paragraph -> heading
            if (root.blockType === 0) {
              const headingMatch = /^(#{1,6}) /.exec(text)

              if (headingMatch) {
                root.transformingShortcut = true

                const headingLevel = headingMatch[1].length
                const headingText = text.substring(headingLevel + 1)

                root.model.changeBlockType(
                  root.blockId,
                  1,
                  headingLevel
                )

                root.model.updateBlockText(
                  root.blockId,
                  headingText
                )

                root.transformingShortcut = false

                // Changing the type makes the dispatcher reload the block
                // renderer, so focus has to move to the new editor.
                root.focusRequested(headingText.length)
                return
              }
            }

            // Every normal text change is sent to the model immediately so
            // undo/redo can operate on characters/words instead of whole
            // blocks. BlockModel groups consecutive keystrokes for us.
            if (root.model && root.blockId.length > 0)
              root.model.updateBlockText(root.blockId, text)
          }

          Keys.onPressed: function(event) {
            // TextArea has its own per-editor undo stack, which would only
            // undo the block that currently has focus. Intercept the standard
            // shortcuts and use the document-wide history in BlockModel
            // instead so undo/redo works across every block.
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
    }
  }
}
