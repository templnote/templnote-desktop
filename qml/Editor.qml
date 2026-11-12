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

      width: documentView.width
      height: renderer.implicitHeight

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
          if (newId.length > 0) {
            console.log("Created new block:", newId)
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
