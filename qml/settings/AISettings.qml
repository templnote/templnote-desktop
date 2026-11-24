import QtQuick
import QtQuick.Controls

// AI Providers settings: connect cloud providers with a key. Local providers
// are added automatically after a probe and need no key. Models live in their
// own Settings section (AIModelsSettings.qml).
Column {
  id: root
  objectName: "aiSettingsRoot"

  width: parent ? parent.width : 0
  spacing: 18

  property string statusMessage: ""
  // Reading the registry revision inside the JS bindings below makes them
  // re-evaluate when connection state or model toggles change.
  readonly property int registryRevision: providerManager ? providerManager.revision : 0

  property var providerList: {
    const rev = root.registryRevision
    return providerManager ? providerManager.providers : []
  }

  // Kept on the root so it is in scope for every control inside the dialog,
  // including controls defined in their own components (SettingsButton).
  function connectAction() {
    if (!providerManager || connectPopup.providerId.length === 0)
      return
    root.statusMessage = "Connecting..."
    connectPopup.close()
    providerManager.connectProvider(connectPopup.providerId,
                                    connectPopup.keyField.text.trim(),
                                    connectPopup.urlField.text.trim())
    connectPopup.keyField.text = ""
    connectPopup.urlField.text = ""
  }

  function providerDescription(id) {
    switch (id) {
    case "google": return "Gemini models for fast, structured responses"
    case "openai": return "GPT models for fast, capable general AI tasks"
    case "anthropic": return "Direct access to Claude models via API key"
    case "openrouter": return "Access many supported models from one provider"
    case "groq": return "Fast open-source inference with an OpenAI-style API"
    case "together": return "Hosted open models through an OpenAI-style API"
    case "custom": return "Any OpenAI-compatible endpoint"
    default: return "Connect with an API key"
    }
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
          "Choose which of their models are available in the separate Models section."
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
      color: root.statusMessage.indexOf("Cannot") === 0 ||
             root.statusMessage.indexOf("An API key") === 0
             ? Theme.danger : Theme.textMuted
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
        color: Theme.surfaceAlt
        border.width: 1
        border.color: Theme.border

        Item {
          anchors {
            left: parent.left
            right: parent.right
            leftMargin: 12
            rightMargin: 12
            verticalCenter: parent.verticalCenter
          }
          height: 36

          Column {
            anchors {
              left: parent.left
              right: connectButton.visible ? connectButton.left : parent.right
              rightMargin: connectButton.visible ? 12 : 0
              verticalCenter: parent.verticalCenter
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
                       : root.providerDescription(modelData.id))
              color: modelData.connected ? Theme.accent : Theme.textMuted
              font.pixelSize: 11
              elide: Text.ElideRight
            }
          }

          SettingsButton {
            id: connectButton
            anchors {
              right: parent.right
              verticalCenter: parent.verticalCenter
            }
            text: modelData.connected ? "Disconnect" : "Connect"
            accessibleName: text + " " + modelData.name
            visible: modelData.kind !== "local"
            onClicked: {
              root.statusMessage = ""
              if (modelData.connected) {
                if (providerManager)
                  providerManager.disconnectProvider(modelData.id)
              } else {
                connectPopup.providerId = modelData.id
                connectPopup.providerName = modelData.name
                connectPopup.errorMessage = ""
                connectPopup.keyField.text = ""
                connectPopup.urlField.text = ""
                connectPopup.open()
              }
            }
          }
        }
      }
    }
  }

  // ─── CONNECT DIALOG ───
  Popup {
    id: connectPopup
    objectName: "connectPopup"
    anchors.centerIn: Overlay.overlay
    width: Math.min(480, root.width - 32)
    modal: true
    focus: true
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property string providerId: ""
    property string providerName: ""
    property string errorMessage: ""
    property alias keyField: apiKeyField
    property alias urlField: baseUrlField

    Overlay.modal: Rectangle {
      color: Qt.rgba(0, 0, 0, 0.55)
    }

    background: Rectangle {
      radius: 12
      color: Theme.surface
      border.width: 1
      border.color: Theme.border
    }

    contentItem: Column {
      id: dialogColumn
      width: connectPopup.width - 40
      spacing: 12

      Row {
        width: parent.width
        spacing: 10

        Text {
          anchors.verticalCenter: parent.verticalCenter
          text: "✦"
          color: Theme.accent
          font.pixelSize: 18
        }

        Text {
          id: dialogTitle
          anchors.verticalCenter: parent.verticalCenter
          text: "Connect " + connectPopup.providerName
          color: Theme.text
          font.pixelSize: 15
          font.bold: true
        }

        Item {
          width: Math.max(0, parent.width - dialogTitle.implicitWidth - 70)
          height: 1
        }

        Text {
          anchors.verticalCenter: parent.verticalCenter
          text: "×"
          color: Theme.textMuted
          font.pixelSize: 20
          MouseArea {
            anchors.fill: parent
            onClicked: connectPopup.close()
          }
        }
      }

      Text {
        width: parent.width
        text: "Enter your " + connectPopup.providerName +
              " API key to connect your account and use its models in Templnote."
        color: Theme.textMuted
        font.pixelSize: 12
        wrapMode: Text.Wrap
      }

      Text {
        text: connectPopup.providerName + " API key"
        color: Theme.text
        font.pixelSize: 11
        font.bold: true
      }

      TextField {
        id: apiKeyField
        width: parent.width
        height: 38
        placeholderText: "API key"
        placeholderTextColor: Theme.textFaint
        color: Theme.text
        font.pixelSize: 13
        leftPadding: 12
        rightPadding: 12
        echoMode: TextInput.Password
        passwordCharacter: "\u2022"
        background: Rectangle {
          radius: 8
          color: Theme.surfaceAlt
          border.width: 1
          border.color: apiKeyField.activeFocus ? Theme.accent : Theme.border
        }
        Keys.onReturnPressed: {
          if (apiKeyField.text.trim().length > 0)
            root.connectAction()
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
        height: 38
        placeholderText: "https://api.example.com/v1"
        placeholderTextColor: Theme.textFaint
        color: Theme.text
        font.pixelSize: 13
        leftPadding: 12
        rightPadding: 12
        background: Rectangle {
          radius: 8
          color: Theme.surfaceAlt
          border.width: 1
          border.color: baseUrlField.activeFocus ? Theme.accent : Theme.border
        }
      }

      Text {
        visible: connectPopup.errorMessage.length > 0
        width: parent.width
        text: connectPopup.errorMessage
        color: Theme.danger
        font.pixelSize: 11
        wrapMode: Text.Wrap
      }

      Row {
        spacing: 10

        SettingsButton {
          objectName: "connectContinueButton"
          text: "Continue"
          primary: true
          accessibleName: "Connect " + connectPopup.providerName
          enabled: apiKeyField.text.trim().length > 0
          onClicked: root.connectAction()
        }

        SettingsButton {
          text: "Cancel"
          accessibleName: "Cancel connection"
          onClicked: connectPopup.close()
        }
      }

    }
  }

  Connections {
    target: providerManager ? providerManager : null

    function onConnected(id) {
      root.statusMessage = "Connected — models loaded."
      connectPopup.errorMessage = ""
    }

    function onError(message) {
      if (connectPopup.opened)
        connectPopup.errorMessage = message
      else
        root.statusMessage = message
    }
  }
}
