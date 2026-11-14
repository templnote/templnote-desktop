import QtQuick
import QtQuick.Controls

// Floating document title strip that lives in the editor's reserved top band.
// Only the file name is interactive; the folder and timestamp are read-only.
Item {
  id: root

  property var model

  // Inline rename state.
  property bool editing: false
  property string draft: ""
  property string errorText: ""

  implicitHeight: 44

  readonly property bool hasTimestamp: root.model
                                       && !isNaN(root.model.lastModified.getTime())

  function startEditing() {
    if (!root.model || root.model.currentFilePath.length === 0)
      return

    root.draft = root.model.documentTitle
    root.errorText = ""
    root.editing = true

    Qt.callLater(function() {
      nameField.forceActiveFocus()
      nameField.selectAll()
    })
  }

  function commit() {
    if (!root.editing)
      return

    if (root.model.renameDocument(root.draft)) {
      root.editing = false
      root.errorText = ""
    } else {
      root.errorText = root.model.lastError
    }
  }

  function cancel() {
    root.editing = false
    root.errorText = ""
    root.draft = root.model ? root.model.documentTitle : ""
  }

  Row {
    id: row

    anchors {
      top: parent.top
      left: parent.left
    }

    height: 28
    spacing: 8

    Text {
      anchors.verticalCenter: parent.verticalCenter
      visible: root.hasTimestamp
      text: root.hasTimestamp
            ? root.model.lastModified.toLocaleString(Qt.locale(), Locale.ShortFormat)
            : ""
      color: Theme.textFaint
      font.pixelSize: 12
    }

    Rectangle {
      anchors.verticalCenter: parent.verticalCenter
      visible: root.hasTimestamp
      width: 1
      height: 12
      color: Theme.border
    }

    Text {
      id: folderLabel

      anchors.verticalCenter: parent.verticalCenter
      visible: root.model && root.model.documentFolder.length > 0
      width: Math.min(implicitWidth, 180)
      text: root.model ? root.model.documentFolder : ""
      color: Theme.textFaint
      font.pixelSize: 12
      elide: Text.ElideMiddle
    }

    Text {
      anchors.verticalCenter: parent.verticalCenter
      visible: folderLabel.visible
      text: "/"
      color: Theme.textFaint
      font.pixelSize: 12
    }

    Text {
      id: nameLabel

      anchors.verticalCenter: parent.verticalCenter
      visible: !root.editing
      width: Math.min(implicitWidth, 260)
      text: root.model
            ? root.model.documentTitle + (root.model.isModified ? " •" : "")
            : ""
      color: nameHover.containsMouse ? Theme.accent : Theme.text
      font.pixelSize: 13
      font.bold: true
      elide: Text.ElideRight

      MouseArea {
        id: nameHover

        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.startEditing()
      }
    }

    TextField {
      id: nameField

      anchors.verticalCenter: parent.verticalCenter
      visible: root.editing
      width: Math.max(120, Math.min(implicitWidth, 320))
      text: root.draft
      color: Theme.text
      font: nameLabel.font
      padding: 0
      selectByMouse: true
      placeholderText: "Untitled"

      background: Rectangle {
        color: "transparent"

        Rectangle {
          anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
          }

          height: 1
          color: root.errorText.length > 0 ? Theme.danger : Theme.accent
        }
      }

      onTextEdited: root.draft = text
      onAccepted: root.commit()

      Keys.onEscapePressed: function(event) {
        root.cancel()
        event.accepted = true
      }

      onActiveFocusChanged: {
        if (!activeFocus && root.editing)
          root.commit()
      }
    }
  }

  Text {
    id: errorLabel

    anchors {
      top: row.bottom
      left: row.left
      topMargin: 2
    }

    visible: root.errorText.length > 0
    text: root.errorText
    color: Theme.danger
    font.pixelSize: 11
    elide: Text.ElideRight
    width: Math.min(implicitWidth, root.width)
  }
}
