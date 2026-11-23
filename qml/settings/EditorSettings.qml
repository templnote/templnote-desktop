import QtQuick

Column {
  id: root

  width: parent ? parent.width : 0
  spacing: 22

  Text {
    text: "Typography"
    color: Theme.text
    font.pixelSize: 15
    font.bold: true
  }

  SettingChoice {
    title: "Font family"
    description: "The font used for document content."
    options: ["System", "Mono", "Serif"]
    value: Preferences.fontFamily
    onSelected: function(value) {
      Preferences.fontFamily = value
    }
  }

  SettingChoice {
    title: "Line width"
    description: "How wide the writing column becomes on large windows."
    options: ["Narrow", "Wide"]
    value: Preferences.lineWidth === "Narrow" ? "Narrow" : "Wide"
    onSelected: function(value) {
      Preferences.lineWidth = value
    }
  }
}
