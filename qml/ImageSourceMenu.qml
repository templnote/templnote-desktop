import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Dialogs

// Editor-level overlay for choosing an image source. Kept outside block
// delegates so it is never clipped by column panes or stacked under other
// blocks, the same way CommandMenu is owned by the editor.
Rectangle {
  id: root
  objectName: "imageSourceMenu"

  property var model: null
  property string blockId: ""
  property bool isOpen: false
  property int activeTab: 0
  property real anchorX: 0
  property real anchorY: 0
  property real anchorBottom: 0
  property bool flipUp: false

  width: 360
  height: 140
  radius: 8
  color: Theme.surface
  border.color: Theme.border
  border.width: 1
  visible: isOpen
  z: 30

  FileDialog {
    id: fileDialog
    title: "Select Image"
    nameFilters: ["Image files (*.png *.jpg *.jpeg *.webp *.svg *.gif *.bmp)"]
    onAccepted: {
      if (selectedFile && root.model) {
        const imported = root.model.importAttachment(selectedFile)
        root.model.setBlockImageSource(root.blockId, imported)
        root.close()
      }
    }
  }

  function openFor(targetBlockId, x, y, bottom) {
    root.blockId = targetBlockId
    root.anchorX = x
    root.anchorY = y
    root.anchorBottom = bottom
    root.activeTab = 0
    root.isOpen = true
    root.reposition()
  }

  function close() {
    root.isOpen = false
    root.blockId = ""
  }

  function reposition() {
    if (!parent)
      return

    const margin = 8
    root.flipUp = (root.anchorBottom + root.height + 10 > parent.height)
                  && (root.anchorY > root.height + 10)

    x = Math.max(margin, Math.min(parent.width - width - margin,
                                  root.anchorX))
    y = root.flipUp ? root.anchorY - height - 6 : root.anchorBottom + 6
  }

  onIsOpenChanged: {
    if (isOpen)
      reposition()
  }

  Column {
    anchors.fill: parent
    anchors.margins: 10
    spacing: 10

    // Tab switcher
    Row {
      spacing: 12

      Rectangle {
        height: 28
        width: 70
        radius: 4
        color: root.activeTab === 0
               ? (Theme.isDark ? "#333842" : "#e9ecef") : "transparent"

        Text {
          anchors.centerIn: parent
          text: "Upload"
          font.pixelSize: 13
          font.weight: root.activeTab === 0 ? Font.DemiBold : Font.Normal
          color: root.activeTab === 0 ? Theme.text : Theme.textMuted
        }

        TapHandler {
          onTapped: root.activeTab = 0
        }
      }

      Rectangle {
        height: 28
        width: 60
        radius: 4
        color: root.activeTab === 1
               ? (Theme.isDark ? "#333842" : "#e9ecef") : "transparent"

        Text {
          anchors.centerIn: parent
          text: "Link"
          font.pixelSize: 13
          font.weight: root.activeTab === 1 ? Font.DemiBold : Font.Normal
          color: root.activeTab === 1 ? Theme.text : Theme.textMuted
        }

        TapHandler {
          onTapped: {
            root.activeTab = 1
            linkTextInput.forceActiveFocus()
          }
        }
      }
    }

    // Upload
    Item {
      visible: root.activeTab === 0
      width: parent.width
      height: 60

      Rectangle {
        anchors.centerIn: parent
        width: parent.width
        height: 36
        radius: 6
        color: uploadBtnHover.hovered
               ? (Theme.isDark ? "#3a3f4b" : "#e2e6ea")
               : (Theme.isDark ? "#2c313a" : "#f1f3f5")
        border.color: Theme.border
        border.width: 1

        HoverHandler {
          id: uploadBtnHover
        }

        TapHandler {
          onTapped: fileDialog.open()
        }

        Text {
          anchors.centerIn: parent
          text: "Upload file"
          font.pixelSize: 13
          font.weight: Font.Medium
          color: Theme.text
        }
      }
    }

    // Link
    Item {
      visible: root.activeTab === 1
      width: parent.width
      height: 60

      Row {
        anchors.fill: parent
        spacing: 8

        Rectangle {
          width: parent.width - 96
          height: 36
          radius: 6
          color: Theme.isDark ? "#1e2227" : "#ffffff"
          border.color: linkTextInput.activeFocus ? Theme.accent : Theme.border
          border.width: 1

          TextInput {
            id: linkTextInput
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.text
            font.pixelSize: 13
            selectByMouse: true

            Text {
              visible: !linkTextInput.text && !linkTextInput.activeFocus
              text: "Paste an image link..."
              color: Theme.textMuted
              font.pixelSize: 13
              anchors.verticalCenter: parent.verticalCenter
              anchors.left: parent.left
            }

            onAccepted: {
              if (text.trim().length > 0 && root.model) {
                root.model.setBlockImageSource(root.blockId, text.trim())
                root.close()
              }
            }
          }
        }

        Rectangle {
          width: 88
          height: 36
          radius: 6
          color: embedBtnHover.hovered
                 ? (Theme.isDark ? "#2563eb" : "#1d4ed8") : Theme.accent

          HoverHandler {
            id: embedBtnHover
          }

          TapHandler {
            onTapped: {
              if (linkTextInput.text.trim().length > 0 && root.model) {
                root.model.setBlockImageSource(root.blockId,
                                               linkTextInput.text.trim())
                root.close()
              }
            }
          }

          Text {
            anchors.centerIn: parent
            text: "Embed image"
            font.pixelSize: 12
            font.weight: Font.Medium
            color: "#ffffff"
          }
        }
      }
    }
  }

  Keys.onEscapePressed: {
    event.accepted = true
    root.close()
  }
}
