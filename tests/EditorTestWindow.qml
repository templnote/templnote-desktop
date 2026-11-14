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

    anchors.fill: parent
    model: blockModel
  }
}
