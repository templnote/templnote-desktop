import QtQuick
import QtQuick.Controls

// Models section of Settings. Lists every discovered model grouped by its
// provider so the user can enable/disable them and pick the default model.
Column {
  id: root
  objectName: "aiModelsRoot"

  width: parent ? parent.width : 0
  spacing: 18

  // Reading the registry revision inside the JS bindings below makes them
  // re-evaluate when connection state or model toggles change.
  readonly property int registryRevision: providerManager ? providerManager.revision : 0

  property string searchText: ""
  property var expandedProviders: ({})
  property var providerList: {
    const rev = root.registryRevision
    return providerManager ? providerManager.providers : []
  }

  // All models, grouped by provider. Models carry their own providerName from
  // ProviderRegistry, so no second lookup is needed.
  property var modelGroups: {
    const rev = root.registryRevision
    const groups = []
    const byId = ({})
    const models = providerManager ? providerManager.models : []
    for (const model of models) {
      const id = model.providerId
      if (!byId[id]) {
        byId[id] = {id: id, name: model.providerName, models: []}
        groups.push(byId[id])
      }
      byId[id].models.push(model)
    }
    // A connected provider with no models yet still deserves a row (it may
    // support manually added model ids).
    for (const provider of root.providerList) {
      if (provider.connected && !byId[provider.id]) {
        byId[provider.id] = {id: provider.id, name: provider.name, models: []}
        groups.push(byId[provider.id])
      }
    }
    return groups
  }

  property var filteredGroups: {
    const query = root.searchText.trim().toLowerCase()
    if (query.length === 0)
      return root.modelGroups
    const result = []
    for (const group of root.modelGroups) {
      const providerMatches = group.name.toLowerCase().indexOf(query) >= 0
      const models = providerMatches ? group.models : group.models.filter(function(model) {
        return model.label.toLowerCase().indexOf(query) >= 0 ||
               model.modelId.toLowerCase().indexOf(query) >= 0
      })
      if (models.length > 0)
        result.push({id: group.id, name: group.name, models: models})
    }
    return result
  }

  function isExpanded(id) { return !!root.expandedProviders[id] }

  function toggleProvider(id) {
    const next = Object.assign({}, root.expandedProviders)
    next[id] = !next[id]
    root.expandedProviders = next
  }

  Rectangle {
    width: parent.width
    implicitHeight: modelsColumn.implicitHeight + 24
    radius: 8
    color: Theme.surfaceAlt
    border.width: 1
    border.color: Theme.border

    Column {
      id: modelsColumn
      anchors {
        left: parent.left
        right: parent.right
        margins: 12
      }
      spacing: 10

      Text {
        width: parent.width
        text: "Enabled models appear in the prompt panel's model picker. " +
              "Connect a provider first to discover its models."
        color: Theme.textMuted
        font.pixelSize: 12
        wrapMode: Text.Wrap
      }

      // Search field, styled with the app palette and a hand-drawn glass icon
      // so the settings module does not depend on asset resources.
      Rectangle {
        width: parent.width
        height: 36
        radius: 8
        color: Theme.surface
        border.width: 1
        border.color: searchField.activeFocus ? Theme.accent : Theme.border

        Item {
          anchors {
            left: parent.left
            leftMargin: 12
            verticalCenter: parent.verticalCenter
          }
          width: 14
          height: 14

          Rectangle {
            width: 9
            height: 9
            radius: 5
            color: "transparent"
            border.color: Theme.textFaint
            border.width: 1
          }
          Rectangle {
            width: 6
            height: 1.5
            radius: 1
            color: Theme.textFaint
            anchors {
              right: parent.right
              bottom: parent.bottom
              rightMargin: 1
              bottomMargin: 1
            }
            rotation: 45
          }
        }

        TextField {
          id: searchField
          objectName: "modelsSearchField"
          anchors {
            left: parent.left
            leftMargin: 34
            right: parent.right
            rightMargin: 12
            verticalCenter: parent.verticalCenter
          }
          placeholderText: "Search models"
          placeholderTextColor: Theme.textFaint
          color: Theme.text
          font.pixelSize: 12
          padding: 0
          background: Item {}
          onTextChanged: root.searchText = text
        }
      }

      Column {
        width: parent.width
        spacing: 6

        Repeater {
          model: root.filteredGroups

          delegate: Rectangle {
            objectName: "modelGroupRow"
            required property var modelData
            width: parent.width
            // `header` has an explicit height, so its implicitHeight is 0.
            // Use height here or every collapsed provider row would vanish.
            implicitHeight: header.height +
                            (root.isExpanded(modelData.id) ? body.implicitHeight + 6 : 0)
            radius: 8
            color: Theme.surface
            border.width: 1
            border.color: Theme.border

            Item {
              id: header
              anchors {
                left: parent.left
                right: parent.right
                margins: 8
              }
              height: 30

              MouseArea {
                objectName: "modelGroupExpander"
                anchors.fill: parent
                onClicked: root.toggleProvider(modelData.id)
              }

              Text {
                anchors {
                  left: parent.left
                  leftMargin: 4
                  verticalCenter: parent.verticalCenter
                }
                text: root.isExpanded(modelData.id) ? "⌄" : "›"
                color: Theme.textMuted
                font.pixelSize: 20
              }

              Text {
                anchors {
                  left: parent.left
                  leftMargin: 26
                  verticalCenter: parent.verticalCenter
                }
                text: modelData.name
                color: Theme.text
                font.pixelSize: 13
                font.bold: true
              }

              Text {
                anchors {
                  right: parent.right
                  rightMargin: 4
                  verticalCenter: parent.verticalCenter
                }
                text: modelData.models.length + (modelData.models.length === 1
                                                 ? " model" : " models")
                color: Theme.textMuted
                font.pixelSize: 11
              }
            }

            Column {
              id: body
              visible: root.isExpanded(modelData.id)
              anchors {
                left: parent.left
                right: parent.right
                margins: 8
              }
              anchors.top: header.bottom
              spacing: 4

              Repeater {
                model: modelData.models

                delegate: Item {
                  required property var modelData
                  width: parent.width
                  implicitHeight: Math.max(48, modelToggle.implicitHeight)

                  SettingToggle {
                    id: modelToggle
                    objectName: "modelToggle"
                    anchors {
                      left: parent.left
                      right: actions.left
                      rightMargin: 8
                      verticalCenter: parent.verticalCenter
                    }
                    title: modelData.label
                    description: modelData.modelId
                    checked: modelData.enabled
                    onToggled: function(value) {
                      if (providerManager)
                        providerManager.setModelEnabled(modelData.providerId,
                                                        modelData.modelId, value)
                    }
                  }

                  Row {
                    id: actions
                    anchors {
                      right: parent.right
                      verticalCenter: parent.verticalCenter
                    }
                    spacing: 8

                    Text {
                      anchors.verticalCenter: parent.verticalCenter
                      text: providerManager &&
                            providerManager.defaultModelKey === modelData.key
                            ? "Default" : ""
                      color: Theme.accent
                      font.pixelSize: 11
                    }

                    SettingsButton {
                      anchors.verticalCenter: parent.verticalCenter
                      text: "Use"
                      accessibleName: "Use " + modelData.label + " as default"
                      enabled: modelData.enabled && modelData.connected
                      onClicked: {
                        if (providerManager)
                          providerManager.defaultModelKey = modelData.key
                      }
                    }
                  }
                }
              }

              Row {
                visible: modelData.id === "custom" ||
                         modelData.id === "openrouter" ||
                         modelData.id === "google"
                spacing: 8

                TextField {
                  id: manualModelField
                  width: Math.max(120, Math.min(300, body.width - 90))
                  height: 36
                  placeholderText: "Add model id manually"
                  placeholderTextColor: Theme.textFaint
                  color: Theme.text
                  font.pixelSize: 12
                  leftPadding: 12
                  rightPadding: 12
                  background: Rectangle {
                    radius: 8
                    color: Theme.surfaceAlt
                    border.width: 1
                    border.color: manualModelField.activeFocus
                                  ? Theme.accent : Theme.border
                  }
                }
                SettingsButton {
                  text: "Add"
                  primary: true
                  accessibleName: "Add model"
                  enabled: manualModelField.text.trim().length > 0
                  onClicked: {
                    if (providerManager)
                      providerManager.addModel(modelData.id,
                                               manualModelField.text.trim())
                    manualModelField.text = ""
                  }
                }
              }
            }
          }
        }

        Text {
          visible: root.filteredGroups.length === 0
          width: parent.width
          text: root.searchText.length > 0
                ? "No models match “" + root.searchText + "”."
                : "No models yet. Connect a provider in AI Providers to discover its models."
          color: Theme.textMuted
          font.pixelSize: 12
          wrapMode: Text.Wrap
        }
      }
    }
  }
}
