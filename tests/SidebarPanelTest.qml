import QtQuick
import QtQuick.Controls
import Templnote

// Smoke harness for the right-sidebar prompt panel. It loads the real plugin
// decoration at the size the host gives it and checks the composer, picker and
// review card stay wired with stub services.
ApplicationWindow {
  width: 1000
  height: 760
  visible: true

  Rectangle {
    id: host
    width: 400
    height: 700
    anchors.centerIn: parent
    color: "#202020"

    PromptPanelSidebarDecoration {
      anchors.fill: parent
    }
  }
}
