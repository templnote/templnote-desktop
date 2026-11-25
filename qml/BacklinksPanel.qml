import QtQuick
import QtQuick.Controls
import Templnote

// Lists documents that link to the current note via [[Wiki Links]].
Item {
  id: root

  property var model: null
  property string title: ""
  signal documentRequested(string path)
  signal closeRequested()

  anchors.fill: parent
  visible: false
  z: 1700

  readonly property var backlinks: {
    if (!root.model || typeof root.model.backlinksFor !== "function"
        || root.title.length === 0)
      return []
    return root.model.backlinksFor(root.title)
  }

  function open(documentTitle) {
    root.title = documentTitle
    visible = true
  }

  function close() {
    visible = false
    closeRequested()
  }

  Rectangle {
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0.82)
  }

  Rectangle {
    anchors {
      top: parent.top
      right: parent.right
      bottom: parent.bottom
    }
    width: Math.min(380, parent.width - 40)
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    Item {
      id: header
      anchors { top: parent.top; left: parent.left; right: parent.right; margins: 12 }
      height: 24

      Text {
        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
        text: "Backlinks"
        color: Theme.text
        font.pixelSize: 15
        font.bold: true
      }
      Button {
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        width: 26
        height: 24
        flat: true
        text: "×"
        font.pixelSize: 14
        Accessible.name: "Close backlinks"
        onClicked: root.close()
      }
    }

    Text {
      anchors { top: header.bottom; left: parent.left; right: parent.right; margins: 12 }
      text: "Documents linking to “" + root.title + "”"
      color: Theme.textMuted
      font.pixelSize: 11
      elide: Text.ElideRight
    }

    ListView {
      anchors {
        top: header.bottom
        left: parent.left
        right: parent.right
        bottom: parent.bottom
        topMargin: 40
        bottomMargin: 8
      }
      model: root.backlinks
      clip: true
      delegate: ItemDelegate {
        width: ListView.view.width
        height: 64
        onClicked: root.documentRequested(modelData.path)
        contentItem: Column {
          spacing: 3
          Text {
            width: parent.width
            text: modelData.title
            color: Theme.text
            font.pixelSize: 13
            font.bold: true
            elide: Text.ElideRight
          }
          Text {
            width: parent.width
            text: modelData.snippet
            color: Theme.textMuted
            font.pixelSize: 11
            elide: Text.ElideRight
          }
        }
      }

      Label {
        anchors.centerIn: parent
        visible: root.backlinks.length === 0
        text: "No backlinks yet"
        color: Theme.textFaint
        font.pixelSize: 12
      }
    }
  }
}
