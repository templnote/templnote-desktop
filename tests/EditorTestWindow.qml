import QtQuick
import QtQuick.Controls
import Templnote

ApplicationWindow {
  id: window

  width: 900
  height: 700

  visible: true
  title: "Templnote editor tests"
  color: Theme.background

  BlockModel {
    id: blockModel
  }

  Editor {
    id: editor

    objectName: "editor"

    anchors.fill: parent
    model: blockModel
  }

  function focusBlock(blockId, position) {
    editor.requestFocusBlock(blockId, position)
  }

}
