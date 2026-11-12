import QtQuick
import QtQuick.Controls

Item {
  id: root

  required property var model

  Rectangle {
    anchors.fill: parent
    color: "#111315"
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
    spacing: 16
    clip: true

    delegate: Item {
      id: delegateRoot

      // These properties receive the C++ model roles.
      required property string blockId
      required property string content
      required property int blockType
      required property int headingLevel
      required property int index

      width: documentView.width
      height: renderer.implicitHeight

      function focusBlock(position) {
        renderer.focusEditor(position)
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

        onEnterPressed: {
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
              newDelegate.focusBlock(0)
            }
          })
        }

      }
    }

    Component.onCompleted: {
      console.log("Editor initialized")
      console.log("Model count:", root.model.count)
    }
  }
}
