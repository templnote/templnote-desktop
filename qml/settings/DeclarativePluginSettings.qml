import QtQuick
import QtQuick.Controls
import Templnote

// Generic settings renderer for plugins that declare `settings.fields` in
// their manifest instead of shipping a hand-written Settings QML component.
// This is what lets an AI agent create a plugin with settings without having
// to author a full QML page.
Column {
  id: root

  property string pluginId: ""
  property var fields: []
  property var defaults: ({})
  readonly property int revision: pluginManager
                                  ? pluginManager.settingsRevision : 0

  width: parent ? parent.width : 0
  spacing: 16

  function read(key, fallback) {
    const rev = root.revision
    if (pluginManager) {
      const value = pluginManager.pluginSetting(root.pluginId, key)
      if (value !== undefined && value !== null)
        return value
    }
    return fallback
  }

  function write(key, value) {
    if (pluginManager)
      pluginManager.setPluginSetting(root.pluginId, key, value)
  }

  Repeater {
    model: root.fields

    delegate: Item {
      required property var modelData
      readonly property var field: modelData
      readonly property string key: field.key || ""
      readonly property string type: field.type || "string"
      readonly property string label: field.label || key
      readonly property string description: field.description || ""
      readonly property var fallback: root.defaults[key]
      width: root.width
      height: contentColumn.implicitHeight

      Column {
        id: contentColumn
        width: parent.width
        spacing: 6

        Text {
          width: parent.width
          text: label
          color: Theme.text
          font.pixelSize: 14
          font.bold: true
        }

        Text {
          width: parent.width
          text: description
          color: Theme.textMuted
          font.pixelSize: 12
          wrapMode: Text.Wrap
          visible: text.length > 0
        }

        SettingToggle {
          visible: type === "boolean"
          title: label
          checked: Boolean(root.read(key, fallback !== undefined ? fallback : false))
          onToggled: function(value) { root.write(key, value) }
        }

        SettingSlider {
          visible: type === "number"
          title: label
          from: field.from !== undefined ? field.from : 0
          to: field.to !== undefined ? field.to : 100
          stepSize: field.step !== undefined ? field.step : 1
          value: Number(root.read(key, fallback !== undefined ? fallback : from))
          valueText: value.toFixed(field.decimals !== undefined ? field.decimals : 0)
          onMoved: function(val) { root.write(key, val) }
        }

        SettingChoice {
          visible: type === "choice"
          title: label
          options: field.options || []
          value: String(root.read(key, fallback !== undefined ? fallback : ""))
          onSelected: function(value) { root.write(key, value) }
        }

        Rectangle {
          visible: type === "string" || type === "text"
          width: parent.width
          height: 36
          radius: 6
          color: Theme.surfaceAlt
          border.width: 1
          border.color: Theme.border

          TextInput {
            id: textInput
            anchors.fill: parent
            anchors.margins: 9
            text: String(root.read(key, fallback !== undefined ? fallback : ""))
            color: Theme.text
            font.pixelSize: 13
            onEditingFinished: root.write(key, text)
          }
        }
      }
    }
  }
}
