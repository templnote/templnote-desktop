import QtQuick
import QtQuick.Controls

Item {
  id: root

  required property var model

  // for a centrally content
  property real contentWidth: 800
  property var activeCommandDelegate: null

  function focusDelegateAt(row, position, attempt) {
    const focusAttempt = attempt === undefined ? 0 : attempt

    if (row < 0 || row >= root.model.count)
      return

    documentView.forceLayout()

    Qt.callLater(function(){
      const delegate = documentView.itemAtIndex(row)

      if (delegate) {
        delegate.refreshBlock()
        delegate.focusBlock(position)
      } else if (focusAttempt < 10) {
        // A model reset can take more than one QML turn to create the
        // delegate. Retry briefly so new-document focus is reliable without
        // affecting normal editor navigation.
        root.focusDelegateAt(row, position, focusAttempt + 1)
      }
    })
  }

  function focusFirstBlock() {
    focusDelegateAt(0, 0)
  }

  Connections {
    target: root.model

    function onDocumentCreated() {
      // The model has finished its reset and save; wait for ListView to
      // materialize the first delegate, then place the caret at its start.
      Qt.callLater(function() {
        root.focusFirstBlock()
      })
    }

    function onDocumentLoaded() {
      Qt.callLater(function() {
        root.focusFirstBlock()
      })
    }
  }

  function restoreFocusAfterHistory(blockId, oldRow, undo, cursorPosition) {
    documentView.forceLayout()

    Qt.callLater(function() {
      let row = root.model.indexOfBlock(blockId)

      // The undo/redo may have removed the focused block (for example when
      // undoing a split). Move to the nearest surviving block in that case.
      if (row < 0) {
        const fallback = undo ? oldRow - 1 : oldRow
        row = Math.max(0, Math.min(fallback, root.model.count - 1))
      }

      if (row < 0)
        return

      documentView.positionViewAtIndex(row, ListView.Contain)
      documentView.forceLayout()

      const delegate = documentView.itemAtIndex(row)

      if (delegate) {
        delegate.refreshBlock()
        // Restore the caret to the position associated with the history step.
        // A negative position means "end of text" (structural steps).
        delegate.focusBlock(cursorPosition)
      }
    })
  }

  function undoFrom(blockId, row) {
    root.model.undo()

    const targetId = root.model.lastHistoryBlockId()
    restoreFocusAfterHistory(targetId.length > 0 ? targetId : blockId,
                             row, true,
                             root.model.lastHistoryCursorPosition())
  }

  function redoFrom(blockId, row) {
    root.model.redo()

    const targetId = root.model.lastHistoryBlockId()
    restoreFocusAfterHistory(targetId.length > 0 ? targetId : blockId,
                             row, false,
                             root.model.lastHistoryCursorPosition())
  }

  // Global entry points used by application-level shortcuts. Undo/redo still
  // focuses the block affected by the history step, even when the document
  // itself did not have focus.
  function undo() {
    if (!root.model.canUndo)
      return

    root.model.undo()

    const targetId = root.model.lastHistoryBlockId()

    if (targetId.length > 0)
      restoreFocusAfterHistory(targetId, 0, true,
                               root.model.lastHistoryCursorPosition())
  }

  function redo() {
    if (!root.model.canRedo)
      return

    root.model.redo()

    const targetId = root.model.lastHistoryBlockId()

    if (targetId.length > 0)
      restoreFocusAfterHistory(targetId, 0, false,
                               root.model.lastHistoryCursorPosition())
  }

  Rectangle {
    anchors.fill: parent
    color: Theme.background
  }

  // Document content

  ListView {
    id: documentView

    width: Math.min(root.contentWidth, parent.width - 48)

    anchors {
      top: parent.top
      bottom: parent.bottom
      horizontalCenter: parent.horizontalCenter

      topMargin: 48
      bottomMargin: 160
    }

    model: root.model
    spacing: 0
    clip: true

    delegate: Item {
      id: delegateRoot

      // These properties receive the C++ model roles.
      required property string blockId
      required property string content
      required property int blockType
      required property int headingLevel
      required property int nextBlockType
      required property int numberIndex
      required property int index

      readonly property bool isBullet: delegateRoot.blockType === 2
      readonly property bool nextIsBullet: delegateRoot.nextBlockType === 2
      readonly property bool isNumbered: delegateRoot.blockType === 3
      readonly property bool nextIsNumbered: delegateRoot.nextBlockType === 3
      readonly property bool isCompactList: (isBullet && nextIsBullet) || (isNumbered && nextIsNumbered)

      width: documentView.width
      height: renderer.implicitHeight + (delegateRoot.isCompactList ? 4 : 16)

      function focusBlock(position) {
        renderer.focusEditor(position)
      }

      function focusBlockAtX(xPosition) {
        renderer.focusEditorAtX(xPosition)
      }

      function applyCommand(command) {
        if (!command)
          return
        root.model.updateBlockText(delegateRoot.blockId, "")
        if (root.model.changeBlockType(delegateRoot.blockId, command.type, command.level))
          root.focusDelegateAt(delegateRoot.index, 0)
      }

      function closeCommandMenu() {
        renderer.commandMenuActive = false
      }

      function refreshBlock() {
        renderer.refreshFromModel()
      }

      BlockRenderer {
        id: renderer

        anchors.left: parent.left
        anchors.right: parent.right

        model: root.model
        blockId: delegateRoot.blockId
        content: delegateRoot.content
        blockType: delegateRoot.blockType
        headingLevel: delegateRoot.headingLevel
        numberIndex: delegateRoot.numberIndex

        onMoveFocusRequested: function(direction, xPosition) {
          const targetIndex = delegateRoot.index + direction

          if (targetIndex < 0 || targetIndex >= root.model.count)
            return

          const targetDelegate = documentView.itemAtIndex(targetIndex)

          if (!targetDelegate)
            return

          targetDelegate.focusBlockAtX(xPosition)
        }

        onCommandMenuRequested: function(query, xPosition, yPosition) {
          root.activeCommandDelegate = delegateRoot
          renderer.commandMenuActive = true
          commandMenu.x = Math.max(8, Math.min(root.width - commandMenu.width - 8,
                                               delegateRoot.x + xPosition))
          commandMenu.y = delegateRoot.y + yPosition + 44
          commandMenu.openFor(query)
        }

        onCommandMenuDismissed: {
          if (root.activeCommandDelegate === delegateRoot) {
            renderer.commandMenuActive = false
            commandMenu.visible = false
            root.activeCommandDelegate = null
          }
        }

        onCommandMenuAccepted: {
          if (root.activeCommandDelegate === delegateRoot) {
            renderer.commandMenuActive = false
            commandMenu.visible = false
            root.activeCommandDelegate.applyCommand(commandMenu.selectedCommand)
            root.activeCommandDelegate = null
          }
        }

        onCommandMenuNavigated: function(direction) {
          if (root.activeCommandDelegate === delegateRoot)
            commandMenu.moveSelection(direction)
        }

        onEnterPressed: {

          const currentText = renderer.editorText
          const cursorPosition = renderer.cursorPosition
          
          // for a smoother markdown behaviour
          if ((delegateRoot.blockType === 2 || delegateRoot.blockType === 3) && renderer.editorText.length === 0) {
            if (root.model.changeBlockType(delegateRoot.blockId, 0, 0)) {
              root.focusDelegateAt(delegateRoot.index, 0)
            }
            return
          }

          const newId = root.model.splitBlock(
            delegateRoot.blockId,
            cursorPosition,
            currentText
          )
          if (newId.length === 0) return

          const newIndex = delegateRoot.index + 1;
          //new block should be at index + 1 since items is at index , ez mate, so don't do wtf

          documentView.forceLayout()

          Qt.callLater(function() {
            const newDelegate = documentView.itemAtIndex(newIndex)

            if (newDelegate) {
              newDelegate.refreshBlock()
              newDelegate.focusBlock(0)
            }
          })
        }

        onBackspacePressed: {
          const currentIndex = delegateRoot.index
          const currentText = renderer.editorText
          const currentType = delegateRoot.blockType

          // don't get confused, it first removes the blockType and then removes the block
          if ( currentText.length === 0 && currentType !== 0 ) {
            if (root.model.changeBlockType(delegateRoot.blockId, 0, 0)) {
              root.focusDelegateAt(currentIndex, 0)
            }
            return
          }

          if (currentIndex === 0)
          return

          if (currentText.length === 0) {
            if ( root.model.removeBlock(delegateRoot.blockId)) {
              root.focusDelegateAt(currentIndex - 1, -1)
            }
            return
          }

          if (root.model.mergeWithPrevious(
                delegateRoot.blockId,
                currentText)) {
            root.focusDelegateAt(currentIndex - 1, -1)
          }
        }

        onUndoRequested: {
          root.undoFrom(delegateRoot.blockId, delegateRoot.index)
        }

        onRedoRequested: {
          root.redoFrom(delegateRoot.blockId, delegateRoot.index)
        }
      }
    }

  CommandMenu {
    id: commandMenu

    onCommandSelected: function(command) {
      if (root.activeCommandDelegate) {
        root.activeCommandDelegate.applyCommand(command)
        root.activeCommandDelegate.closeCommandMenu()
        root.activeCommandDelegate = null
      }
      visible = false
    }
  }

    Component.onCompleted: {
      console.log("Editor initialized")
      console.log("Model count:", root.model.count)
    }
  }
}
