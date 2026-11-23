import QtQuick
import QtQuick.Controls

// Browse and install signed plugins from the community catalog. The C++
// client verifies catalog, revocation list, SHA-256 and maintainer signature
// before anything is written; QML only renders the result and asks for consent.
Item {
  id: root

  width: parent ? parent.width : 0
  implicitHeight: mainColumn.implicitHeight

  property string searchText: ""
  property string selectedId: ""
  property string pendingInstallId: ""

  function rebuild() {
    filtered.clear()
    const entries = pluginStore.entriesList()
    for (const entry of entries) {
      if (root.searchText.length > 0 &&
          entry.name.toLowerCase().indexOf(root.searchText.toLowerCase()) < 0 &&
          entry.description.toLowerCase().indexOf(root.searchText.toLowerCase()) < 0)
        continue
      filtered.append(entry)
    }
  }

  function entryFor(id) {
    const entries = pluginStore.entriesList()
    for (const entry of entries)
      if (entry.id === id) return entry
    return null
  }

  ListModel { id: filtered }

  Connections {
    target: pluginStore
    function onCatalogRefreshed(count) { root.rebuild() }
    function onInstalled(id) { root.rebuild() }
    function onUninstalled(id) { root.rebuild() }
  }

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

      TextField {
        id: searchField
        width: Math.min(260, mainColumn.width / 3)
        placeholderText: "Search plugins…"
        onTextChanged: root.searchText = text
      }

      Button {
        text: "Refresh"
        enabled: !pluginStore.busy
        onClicked: pluginStore.refresh()
      }
    }

    Text {
      width: parent.width
      text: "Packages are signed by maintainers and verified before installation."
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

    BusyIndicator { visible: pluginStore.busy; running: visible }

    Text {
      width: parent.width
      visible: !pluginStore.busy && filtered.count === 0
      text: "No plugins available. Check your connection and refresh."
      color: Theme.textMuted
      wrapMode: Text.WordWrap
    }

    ListView {
      id: storeList
      width: parent.width
      height: Math.min(contentHeight, 420)
      model: filtered
      clip: true
      spacing: 8

      delegate: Rectangle {
        width: ListView.view.width
        height: 96
        radius: 6
        color: selectedId === model.id ? Theme.tint(Theme.accent, 0.12) : Theme.surfaceAlt
        border.color: selectedId === model.id ? Theme.accent : Theme.border

        MouseArea {
          anchors.fill: parent
          onClicked: selectedId = model.id
        }

        Column {
          anchors.fill: parent
          anchors.margins: 10
          spacing: 4

          Row {
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
            Text {
              visible: model.updateAvailable
              text: "update available"
              color: Theme.accent
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
            spacing: 8
            Text {
              text: model.author
              color: Theme.textFaint
              font.pixelSize: 11
              anchors.verticalCenter: parent.verticalCenter
            }
            Button {
              visible: !model.installed
              enabled: !pluginStore.busy
              text: "Install"
              onClicked: {
                selectedId = model.id
                const entry = root.entryFor(model.id)
                pendingInstallId = (entry && entry.permissions.length > 0)
                    ? model.id : ""
                if (!pendingInstallId)
                  pluginStore.install(model.id)
              }
            }
            Button {
              visible: model.installed && model.updateAvailable
              enabled: !pluginStore.busy
              text: "Update"
              onClicked: pluginStore.install(model.id)
            }
            Button {
              visible: model.installed
              enabled: !pluginStore.busy
              text: "Uninstall"
              onClicked: pluginStore.uninstall(model.id)
            }
          }
        }
      }
    }

    // Detail + consent panel for plugins that request permissions.
    Rectangle {
      width: parent.width
      visible: selectedId.length > 0
      radius: 6
      color: Theme.surface
      border.color: Theme.border
      implicitHeight: detailColumn.implicitHeight + 20

      Column {
        id: detailColumn
        width: parent.width - 20
        x: 10
        y: 10
        spacing: 6

        Text {
          width: parent.width
          text: {
            const entry = root.entryFor(selectedId)
            return entry ? entry.name + " — permissions" : ""
          }
          color: Theme.text
          font.bold: true
          font.pixelSize: 13
        }
        Text {
          width: parent.width
          text: {
            const entry = root.entryFor(selectedId)
            if (!entry) return ""
            return entry.permissions.length > 0
                ? "Requested: " + entry.permissions.join(", ")
                : "No special permissions requested."
          }
          color: Theme.textMuted
          wrapMode: Text.WordWrap
          font.pixelSize: 12
        }
        Text {
          width: parent.width
          text: {
            const entry = root.entryFor(selectedId)
            return entry && entry.dependencies.length > 0
                ? "Requires: " + entry.dependencies.join(", ") : ""
          }
          visible: text.length > 0
          color: Theme.textMuted
          font.pixelSize: 11
        }

        Row {
          spacing: 6
          visible: {
            const entry = root.entryFor(selectedId)
            return entry && entry.dependencies.length > 0
          }
          Text {
            text: "Install dependencies first:"
            color: Theme.textMuted
            font.pixelSize: 11
            anchors.verticalCenter: parent.verticalCenter
          }
          Repeater {
            model: {
              const entry = root.entryFor(selectedId)
              return entry ? entry.dependencies : []
            }
            delegate: Button {
              text: modelData
              enabled: !pluginStore.busy
              onClicked: pluginStore.install(modelData)
            }
          }
        }

        Button {
          visible: pendingInstallId === selectedId
          text: "Allow and install"
          onClicked: {
            pluginStore.install(pendingInstallId)
            pendingInstallId = ""
          }
        }
        Button {
          visible: pendingInstallId === selectedId
          text: "Cancel"
          onClicked: pendingInstallId = ""
        }
      }
    }
  }

  Component.onCompleted: pluginStore.refresh()
}
