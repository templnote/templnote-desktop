import QtQuick
import QtQuick.Controls
import Templnote

// Smoke harness: load the sidebar prompt-panel decoration with stub services so
// the review card, error banner and model picker are exercised by CTest.
ApplicationWindow {
  width: 900
  height: 700
  visible: true

  Rectangle {
    id: host
    width: 700
    height: 420
    anchors.centerIn: parent
    color: "#202020"

    PromptPanelSidebarDecoration {
      anchors.bottom: parent.bottom
      anchors.horizontalCenter: parent.horizontalCenter
    }
  }

  // Mimics the Loader in Main.qml: no explicit width, the loaded item derives
  // its width from parent.width. Guards against a circular zero-width binding.
  Loader {
    id: bareLoader
    objectName: "bareLoader"
    width: 400
    height: 600
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    property int resolvedWidth: item ? item.width : -1
    property int resolvedHeight: item ? item.height : -1
    source: Qt.resolvedUrl("PromptPanelSidebarDecoration.qml")
  }
}
