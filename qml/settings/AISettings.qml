import QtQuick
import QtQuick.Controls

// Core AI settings: connected providers and the models discovered from them.
// Local providers (Ollama / LM Studio) appear here automatically after a probe.
Column {
  id: root

  width: parent ? parent.width : 0
  spacing: 18

  property string selectedProviderId: ""
  property string statusMessage: ""
  // Reading the registry revision inside the JS bindings below makes them
  // re-evaluate when connection state or model toggles change.
  readonly property int registryRevision: providerManager ? providerManager.revision : 0

  property var providerList: {
    const rev = root.registryRevision
    return providerManager ? providerManager.providers : []
  }
  property var selectedModels: {
    const rev = root.registryRevision
    return (providerManager && root.selectedProviderId.length > 0)
      ? providerManager.modelsFor(root.selectedProviderId) : []
  }

  function selectedProvider() {
    for (let i = 0; i < root.providerList.length; ++i) {
      if (root.providerList[i].id === root.selectedProviderId)
        return root.providerList[i]
    }
    return null
  }

  Text {
    text: "AI Providers"
    color: Theme.text
    font.pixelSize: 15
    font.bold: true
  }

  Text {
    width: parent.width
    text: "Connect an API key to a cloud provider, or start a local model server. " +
          "Models are listed after a successful connection and can be toggled individually."
    color: Theme.textMuted
    font.pixelSize: 12
    wrapMode: Text.Wrap
  }

  Row {
    spacing: 10

    SettingsButton {
      text: "Refresh local providers"
      accessibleName: "Refresh local providers"
      onClicked: {
        if (providerManager)
          providerManager.detectLocalProviders()
      }
    }

    Text {
      anchors.verticalCenter: parent.verticalCenter
      text: root.statusMessage
      color: root.statusMessage.indexOf("Cannot") === 0 ? Theme.danger : Theme.textMuted
      font.pixelSize: 12
      wrapMode: Text.Wrap
      maximumLineCount: 2
      elide: Text.ElideRight
      width: Math.min(400, root.width - 230)
    }
  }

  // ─── PROVIDERS ───
  Text {
    text: "Available providers"
    color: Theme.text
    font.pixelSize: 13
    font.bold: true
  }

  Column {
    width: parent.width
    spacing: 8

    Repeater {
      model: root.providerList

      delegate: Rectangle {
        required property var modelData
        width: parent.width
        implicitHeight: 60
        radius: 8
        color: modelData.id === root.selectedProviderId
               ? Theme.surfaceHover : Theme.surfaceAlt
        border.width: 1
        border.color: modelData.id === root.selectedProviderId
                      ? Theme.accent : Theme.border

        // Fixed right-hand action column: "Models" always sits at the same x,
        // and "Connect"/"Disconnect" occupies the slot to its left. Local rows
        // simply leave that slot empty instead of shifting "Models" leftwards.
        Item {
          id: row
          anchors {
            left: parent.left
            right: parent.right
            leftMargin: 12
            rightMargin: 12
            verticalCenter: parent.verticalCenter
          }
          implicitHeight: Math.max(labels.implicitHeight, 36)
          height: implicitHeight

          Column {
            id: labels
            anchors {
              left: parent.left
              verticalCenter: parent.verticalCenter
              right: connectButton.visible ? connectButton.left : modelsButton.left
              rightMargin: 12
            }
            spacing: 3

            Text {
              width: parent.width
              text: modelData.name
              color: Theme.text
              font.pixelSize: 14
              font.bold: true
              elide: Text.ElideRight
            }
            Text {
              width: parent.width
              text: modelData.kind === "local"
                    ? (modelData.connected
                       ? "Local • detected • " + modelData.modelCount + " models"
                       : "Local • not detected")
                    : (modelData.connected
                       ? "Connected • " + modelData.modelCount + " models"
                       : "Not connected")
              color: modelData.connected ? Theme.accent : Theme.textMuted
              font.pixelSize: 11
              elide: Text.ElideRight
            }
          }

          SettingsButton {
            id: connectButton
            anchors {
              right: modelsButton.left
              rightMargin: 10
              verticalCenter: parent.verticalCenter
            }
            text: modelData.connected ? "Disconnect" : "Connect"
            accessibleName: text + " " + modelData.name
            visible: modelData.kind !== "local"
            onClicked: {
              root.selectedProviderId = modelData.id
              root.statusMessage = ""
              if (modelData.connected && providerManager)
                providerManager.disconnectProvider(modelData.id)
            }
          }

          SettingsButton {
            id: modelsButton
            anchors {
              right: parent.right
              verticalCenter: parent.verticalCenter
            }
            text: "Models"
            accessibleName: "Show models for " + modelData.name
            enabled: modelData.connected && modelData.modelCount > 0
            onClicked: {
              root.selectedProviderId = modelData.id
              root.statusMessage = ""
            }
          }
        }
      }
    }
  }

  // ─── CONNECT FORM ───
  Rectangle {
    visible: root.selectedProviderId.length > 0 &&
             !(root.selectedProvider() && root.selectedProvider().connected) &&
             !(root.selectedProvider() && root.selectedProvider().kind === "local")
    width: parent.width
    implicitHeight: formColumn.implicitHeight + 24
    radius: 8
    color: Theme.surfaceAlt
    border.width: 1
    border.color: Theme.border

    Column {
      id: formColumn
      anchors {
        left: parent.left
        right: parent.right
        margins: 12
      }
      spacing: 10

      Text {
        text: "Connect to " + (root.selectedProvider()
                               ? root.selectedProvider().name : "")
        color: Theme.text
        font.pixelSize: 13
        font.bold: true
      }

      Text {
        text: "API key"
        color: Theme.textMuted
        font.pixelSize: 11
      }

      TextField {
        id: apiKeyField
        width: parent.width
        height: 36
        placeholderText: "sk-..."
        placeholderTextColor: Theme.textFaint
        color: Theme.text
        font.pixelSize: 13
        leftPadding: 12
        rightPadding: 12
        echoMode: TextInput.Password
        passwordCharacter: "\u2022"
        background: Rectangle {
          radius: 8
          color: Theme.surface
          border.width: 1
          border.color: apiKeyField.activeFocus ? Theme.accent : Theme.border
        }
      }

      Text {
        text: "Base URL (optional — leave empty for the provider default)"
        color: Theme.textMuted
        font.pixelSize: 11
      }

      TextField {
        id: baseUrlField
        width: parent.width
        height: 36
        placeholderText: "https://api.example.com/v1"
        placeholderTextColor: Theme.textFaint
        color: Theme.text
        font.pixelSize: 13
        leftPadding: 12
        rightPadding: 12
        background: Rectangle {
          radius: 8
          color: Theme.surface
          border.width: 1
          border.color: baseUrlField.activeFocus ? Theme.accent : Theme.border
        }
      }

      Row {
        spacing: 10

        SettingsButton {
          text: "Connect"
          primary: true
          accessibleName: "Connect " + (root.selectedProvider()
                                        ? root.selectedProvider().name : "provider")
          enabled: apiKeyField.text.trim().length > 0
          onClicked: {
            if (providerManager) {
              providerManager.connectProvider(root.selectedProviderId,
                                              apiKeyField.text.trim(),
                                              baseUrlField.text.trim())
              root.statusMessage = "Connecting..."
            }
          }
        }
        SettingsButton {
          text: "Cancel"
          accessibleName: "Cancel connection"
          onClicked: {
            root.selectedProviderId = ""
            apiKeyField.text = ""
            baseUrlField.text = ""
            root.statusMessage = ""
          }
        }
      }
    }
  }

  // ─── MODELS ───
  Rectangle {
    visible: root.selectedProviderId.length > 0 && root.selectedModels.length > 0
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
        text: "Models — " + (root.selectedProvider()
                             ? root.selectedProvider().name : "")
        color: Theme.text
        font.pixelSize: 13
        font.bold: true
      }

      Text {
        text: "Enabled models appear in the prompt panel. All models are enabled by default."
        color: Theme.textMuted
        font.pixelSize: 11
        wrapMode: Text.Wrap
        width: parent.width
      }

      Repeater {
        model: root.selectedModels

        delegate: Item {
          required property var modelData
          width: parent.width
          implicitHeight: toggle.implicitHeight
          height: implicitHeight

          SettingToggle {
            id: toggle
            anchors {
              left: parent.left
              right: actionsRow.left
              rightMargin: 12
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
            id: actionsRow
            anchors {
              right: parent.right
              verticalCenter: parent.verticalCenter
            }
            spacing: 8

            Text {
              anchors.verticalCenter: parent.verticalCenter
              text: providerManager && providerManager.defaultModelKey === modelData.key
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
        spacing: 8
        visible: root.selectedProviderId === "custom" ||
                 root.selectedProviderId === "openrouter"

        TextField {
          id: manualModelField
          width: Math.min(300, parent.width - 90)
          height: 36
          placeholderText: "Add model id manually"
          placeholderTextColor: Theme.textFaint
          color: Theme.text
          font.pixelSize: 13
          leftPadding: 12
          rightPadding: 12
          background: Rectangle {
            radius: 8
            color: Theme.surface
            border.width: 1
            border.color: manualModelField.activeFocus ? Theme.accent : Theme.border
          }
        }
        SettingsButton {
          text: "Add"
          primary: true
          accessibleName: "Add model"
          enabled: manualModelField.text.trim().length > 0
          onClicked: {
            if (providerManager)
              providerManager.addModel(root.selectedProviderId,
                                       manualModelField.text.trim())
            manualModelField.text = ""
          }
        }
      }
    }
  }

  Connections {
    target: providerManager ? providerManager : null

    function onConnected(id) {
      if (id === root.selectedProviderId) {
        root.statusMessage = "Connected — models loaded."
        apiKeyField.text = ""
        baseUrlField.text = ""
      }
    }

    function onError(message) {
      root.statusMessage = message
    }
  }
}
