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
  signal moveFocusRequested(int direction, real xPosition)
  signal commandMenuRequested(string query, real xPosition, real yPosition)
  signal commandMenuDismissed()
  signal commandMenuAccepted()
  signal commandMenuNavigated(int direction)
  signal caretMoved(int position)

  property bool commandMenuActive: false

  // Search highlight ranges for this block: [{ start, length, active }].
  property var searchHighlights: []

  // Set while the editor is in global-search navigation mode. In that mode
  // Up/Down cycle matches and Escape removes the highlights.
  property bool searchNavigationActive: false

  signal searchNavigated(int direction)
  signal searchDismissed()
  signal searchEditStarted()

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

  function focusEditorAtX(xPosition) {
    editor.focus = true
    editor.forceActiveFocus()

    const x = Math.max(0, xPosition)
    editor.cursorPosition = editor.positionAt(x, editor.cursorRectangle.y)
    editor.cursorVisible = true
  }

  function focusEditorRange(start, end) {
    editor.focus = true
    editor.forceActiveFocus()

    editor.cursorPosition = start
    editor.select(start, end)
    editor.cursorVisible = true
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

  onSearchHighlightsChanged: searchHighlightsCanvas.requestPaint()

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

        // Painted behind the text. Canvas lets a single match span several
        // wrapped lines without creating one item per character.
        Canvas {
          id: searchHighlightsCanvas

          anchors.fill: parent
          visible: root.searchHighlights.length > 0
          onWidthChanged: requestPaint()
          onHeightChanged: requestPaint()
          onVisibleChanged: if (visible) requestPaint()

          Connections {
            target: editor

            function onTextChanged() {
              if (searchHighlightsCanvas.visible)
                searchHighlightsCanvas.requestPaint()
            }
          }

          Connections {
            target: Theme

            function onAccentChanged() {
              if (searchHighlightsCanvas.visible)
                searchHighlightsCanvas.requestPaint()
            }
          }

          onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            const highlights = root.searchHighlights

            if (!highlights || highlights.length === 0)
              return

            for (let h = 0; h < highlights.length; ++h) {
              const highlight = highlights[h]

              ctx.fillStyle = highlight.active
                  ? Theme.tint(Theme.accent, 0.40)
                  : Theme.tint(Theme.accent, 0.16)

              let runLeft = -1
              let runRight = -1
              let runTop = 0
              let runHeight = 0

              function flushRun() {
                if (runLeft < 0)
                  return

                ctx.fillRect(runLeft, runTop,
                             Math.max(1, runRight - runLeft), runHeight)
                runLeft = -1
                runRight = -1
              }

              // positionToRectangle() returns a 1px caret rect, not a full
              // character cell. Use the next caret position to find the right
              // edge of each character and merge cells that share a line.
              for (let i = highlight.start;
                   i < highlight.start + highlight.length; ++i) {
                if (editor.text.charAt(i) === "\n")
                  continue

                const rect = editor.positionToRectangle(i)
                const nextRect = editor.positionToRectangle(i + 1)
                const sameLine = Math.abs(nextRect.y - rect.y) < 1
                const cellRight = sameLine
                    ? Math.max(rect.x + 1, nextRect.x)
                    // The final character on a soft-wrapped line extends to
                    // the right edge of the text area.
                    : Math.max(rect.x + 1,
                               editor.width - editor.rightPadding)

                if (runLeft >= 0 && Math.abs(rect.y - runTop) < 1) {
                  runRight = Math.max(runRight, cellRight)
                  runHeight = Math.max(runHeight, rect.height)
                } else {
                  flushRun()

                  runLeft = rect.x
                  runRight = cellRight
                  runTop = rect.y
                  runHeight = rect.height
                }
              }

              flushRun()
            }
          }
        }

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

          onCursorPositionChanged: {
            if (editor.activeFocus)
              root.caretMoved(editor.cursorPosition)
          }

          onActiveFocusChanged: {
            if (editor.activeFocus)
              root.caretMoved(editor.cursorPosition)
          }

          Component.onCompleted: {
            editor.text = root.content
          }

          onTextChanged: {
            if (root.transformingShortcut || root.syncingFromModel)
              return

            // The first real edit ends the global-search navigation session.
            // The signal clears the highlight state before the model sees the
            // new text.
            if (root.searchNavigationActive)
              root.searchEditStarted()

            if (root.blockType === 0 && text.startsWith("/") && text.indexOf(" ") < 0)
              root.commandMenuRequested(text.substring(1), editor.cursorRectangle.x, editor.cursorRectangle.y)
            else if (root.commandMenuActive)
              root.commandMenuDismissed()

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
            if (root.commandMenuActive) {
              if (event.key === Qt.Key_Escape) {
                event.accepted = true
                root.commandMenuDismissed()
                return
              }
              if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                event.accepted = true
                root.commandMenuAccepted()
                return
              }
              if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                event.accepted = true
                root.commandMenuNavigated(event.key === Qt.Key_Up ? -1 : 1)
                return
              }
            }
            // Global-search navigation keeps focus in the document, so the
            // block itself has to own the match navigation keys.
            if (root.searchNavigationActive) {
              if (event.key === Qt.Key_Escape) {
                event.accepted = true
                // Drop the selection the active match left behind while
                // keeping the caret where it was.
                editor.deselect()
                root.searchDismissed()
                return
              }
              if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                event.accepted = true
                root.searchNavigated(event.key === Qt.Key_Up ? -1 : 1)
                return
              }
              if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                root.searchEditStarted()
            }

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

            if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
              const cursorRect = editor.cursorRectangle
              const direction = event.key === Qt.Key_Up ? -1 : 1

              event.accepted = true
              root.moveFocusRequested(direction, cursorRect.x)
              return
            }
          }
        }
      }
    }
  }
}
