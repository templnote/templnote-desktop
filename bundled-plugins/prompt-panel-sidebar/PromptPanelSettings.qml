import QtQuick
import QtQuick.Controls
import Templnote

// The available models come from the core provider registry, not from a
// hardcoded list here. This page just shows the current selection and points
// at the AI Providers settings section where models are toggled.
Column {
  id: root

  property var pluginContext: null
  readonly property string pluginId: pluginContext && pluginContext.pluginId
                                     ? pluginContext.pluginId : "prompt-panel-sidebar"
  readonly property var registry: typeof providerManager !== "undefined"
                                  ? providerManager : null
  readonly property int revision: registry ? registry.revision : 0

  property string currentLabel: {
    const rev = root.revision
    if (!registry)
      return "No provider registry"
    const key = registry.defaultModelKey
    if (key.length === 0)
      return registry.anyConnected ? "No model selected" : "Not connected"
    const info = registry.modelInfo(key)
    return info && info.label ? info.label : key
  }

  width: parent ? parent.width : 0
  spacing: 20

  Text {
    text: "AI Model Preferences"
    color: Theme.text
    font.pixelSize: 14
    font.bold: true
  }

  Text {
    width: parent.width
    text: "The prompt panel shows only models you have enabled for connected providers."
    color: Theme.textMuted
    font.pixelSize: 12
    wrapMode: Text.Wrap
  }

  Rectangle {
    width: parent.width
    height: 58
    radius: 8
    color: Theme.surfaceAlt
    border.width: 1
    border.color: Theme.border

    Column {
      anchors {
        left: parent.left
        right: parent.right
        margins: 12
      }
      spacing: 4

      Text {
        text: "Selected model"
        color: Theme.textMuted
        font.pixelSize: 11
      }
      Text {
        text: root.currentLabel
        color: Theme.text
        font.pixelSize: 14
        font.bold: true
      }
    }
  }

  Text {
    width: parent.width
    text: "Open Settings → AI Providers to connect a provider, enable models and choose the default."
    color: Theme.textMuted
    font.pixelSize: 12
    wrapMode: Text.Wrap
  }
}
