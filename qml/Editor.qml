import QtQuick
import QtQuick.Controls

Item {
  id: root

  required property var model
  function focusDelegateAt(row, position) {
    documentView.forceLayout()

    Qt.callLater(function(){
      const delegate = documentView.itemAtIndex(row)

      if (delegate) {
        delegate.refreshBlock()
        delegate.focusBlock(position)
      }
    })
  }

  Rectangle {
    anchors.fill: parent
    color: "#f0f0f8"
  }

  // Document content

  ListView {
    id: documentView

    anchors {
      fill: parent
      leftMargin: 96
      rightMargin: 96
      topMargin: 64
      bottomMargin: 48
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

        onEnterPressed: {

          const currentText = renderer.editorText
          const cursorPosition = renderer.cursorPosition
          
          // commit to visible block text before splitting
          root.model.updateBlockText(
            delegateRoot.blockId,
            currentText
          )
          // for a smoother markdown behaviour
          if ((delegateRoot.blockType === 2 || delegateRoot.blockType === 3) && renderer.editorText.length === 0) {
            if (root.model.changeBlockType(delegateRoot.blockId, 0, 0)) {
              root.focusDelegateAt(delegateRoot.index, 0)
            }
            return
          }

          const newId = root.model.splitBlock(
            delegateRoot.blockId,
            renderer.cursorPosition
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


          root.model.updateBlockText(
            delegateRoot.blockId,
            currentText
          )

          if ( currentType === 2 || currentType === 3 ) {
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

          if (root.model.mergeWithPrevious(delegateRoot.blockId)) {
            root.focusDelegateAt(currentIndex - 1, -1)
          }
        }
      }
    }

    Component.onCompleted: {
      console.log("Editor initialized")
      console.log("Model count:", root.model.count)
    }
  }
}
