import QtQuick
import QtQuick.Controls
import Templnote

Column {
  id: root

  property var pluginContext: null
  readonly property string pluginId: pluginContext && pluginContext.pluginId ? pluginContext.pluginId : "prompt-panel"

  width: parent ? parent.width : 0
  spacing: 20

  property string defaultModel: {
    if (pluginContext && typeof pluginContext.getSetting === "function") {
      const m = pluginContext.getSetting("defaultModel")
      if (m !== undefined && m !== null) return m.toString()
    }
    return "Claude 3.5 Sonnet"
  }

  Text {
    text: "AI Model Preferences"
    color: Theme.text
    font.pixelSize: 14
    font.bold: true
  }

  Text {
    width: parent.width
    text: "Choose the default model provider for the AI prompt panel."
    color: Theme.textMuted
    font.pixelSize: 12
    wrapMode: Text.Wrap
  }

  Row {
    spacing: 12

    Repeater {
      model: ["Claude 3.5 Sonnet", "GPT-4o", "Gemini 1.5 Pro", "Ollama (Local)"]

      delegate: Button {
        text: modelData
        highlighted: root.defaultModel === modelData
        onClicked: {
          root.defaultModel = modelData
          if (pluginContext && typeof pluginContext.setSetting === "function") {
            pluginContext.setSetting("defaultModel", modelData)
          }
        }
      }
    }
  }
}
