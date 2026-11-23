import QtQuick
import QtQuick.Controls

// Browse and install signed plugins from the community catalog. The C++
// client verifies each package's SHA-256 and maintainer signature before it
// is written into the plugin directory; QML only renders the result.
Item {
  id: root

  width: parent ? parent.width : 0
  implicitHeight: mainColumn.implicitHeight

  Column {
    id: mainColumn
    width: parent.width
    spacing: 12

    Row {
      width: parent.width
      spacing: 10

      Text {
        text: "Community plugin catalog"
        color: Theme.text
        font.pixelSize: 16
        font.bold: true
        anchors.verticalCenter: parent.verticalCenter
      }

      Item { width: 20; height: 1 }

      Button {
        id: refreshButton
        text: "Refresh"
        enabled: !pluginStore.busy
        onClicked: pluginStore.refresh()
      }
    }

    Text {
      width: parent.width
      text: "Packages are signed by the maintainers and verified before installation."
      color: Theme.textMuted
      wrapMode: Text.WordWrap
      font.pixelSize: 12
    }

    Text {
      width: parent.width
      visible: pluginStore.lastError.length > 0
      text: pluginStore.lastError
      color: Theme.danger
      wrapMode: Text.WordWrap
      font.pixelSize: 12
    }

    BusyIndicator {
      visible: pluginStore.busy
      running: visible
    }

    Text {
      width: parent.width
      visible: !pluginStore.busy && storeList.count === 0
      text: "No plugins available. Check your connection and refresh."
      color: Theme.textMuted
      wrapMode: Text.WordWrap
    }

    ListView {
      id: storeList
      width: parent.width
      height: Math.min(contentHeight, 560)
      model: pluginStore.model
      clip: true
      spacing: 8

      delegate: Rectangle {
        width: ListView.view.width
        height: 92
        radius: 6
        color: Theme.surfaceAlt
        border.color: Theme.border

        Column {
          anchors.fill: parent
          anchors.margins: 10
          spacing: 4

          Row {
            width: parent.width
            spacing: 8
            Text {
              text: model.name + "  " + model.version
              color: Theme.text
              font.bold: true
              font.pixelSize: 13
            }
            Text {
              visible: model.category.length > 0
              text: model.category
              color: Theme.textMuted
              font.pixelSize: 11
            }
          }

          Text {
            width: parent.width
            text: model.description
            color: Theme.textMuted
            font.pixelSize: 12
            elide: Text.ElideRight
          }

          Row {
            width: parent.width
            spacing: 8
            Text {
              text: model.author
              color: Theme.textFaint
              font.pixelSize: 11
              anchors.verticalCenter: parent.verticalCenter
            }
            Item { width: 1; height: 1 }

            Button {
              anchors.verticalCenter: parent.verticalCenter
              text: model.installed ? "Installed" : "Install"
              enabled: !model.installed && !pluginStore.busy
              onClicked: pluginStore.install(model.id)
            }
          }
        }
      }
    }
  }

  Component.onCompleted: pluginStore.refresh()
}
