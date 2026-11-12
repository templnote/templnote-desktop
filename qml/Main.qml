import QtQuick
import QtQuick.Controls
import Templnote

ApplicationWindow {
  id: window

  width: 1100
  height: 760

  visible: true
  title: "Templnote"
  color: "#101112"

  BlockModel {
    id: blockModel
  }

  NavigationRail {
    property bool explorerExpanded: true
    id: navigationRail

    anchors {
      top: parent.top
      left: parent.left
      bottom: parent.bottom
    }
    onExplorerToggleRequested: {
      fileExplorer.expanded = !fileExplorer.expanded
      navigationRail.explorerExpanded = fileExplorer.expanded
    }
  }

  FileExplorer {
    id: fileExplorer

    anchors {
      top: parent.top
      left: navigationRail.right
      bottom: parent.bottom
    }
  }

  Item {
    id: workspace

    anchors {
      top: parent.top
      left: fileExplorer.right
      right: parent.right
      bottom: parent.bottom
    }

    Editor {
      id: editor

      anchors {
        top: parent.top
        left: parent.left
        right: parent.right
        bottom: promptPanel.top
      }

      model: blockModel
    }

    PromptPanel {
      id: promptPanel

      anchors {
        left: parent.left
        right: parent.right
        bottom: parent.bottom
      }
    }
  }
}
