import QtQuick

Column {
  id: root

  width: parent ? parent.width : 0
  spacing: 22

  property string fontFamily: "System"
  property string lineWidth: "Medium"

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
    value: root.fontFamily
    onSelected: function(value) {
      root.fontFamily = value
    }
  }

  SettingChoice {
    title: "Line width"
    description: "How wide the writing column becomes on large windows."
    options: ["Narrow", "Medium", "Wide"]
    value: root.lineWidth
    onSelected: function(value) {
      root.lineWidth = value
    }
  }

  Rectangle {
    width: parent.width
    height: 1
    color: Theme.border
  }

  Text {
    text: "Writing"
    color: Theme.text
    font.pixelSize: 15
    font.bold: true
  }

  SettingToggle {
    title: "Markdown input shortcuts"
    description: "Convert Markdown prefixes such as #, - and 1. into blocks while typing."
    checked: true
  }

  SettingToggle {
    title: "Spell check"
    description: "Highlight misspelled words while writing."
    checked: false
  }

  SettingToggle {
    title: "Smart quotes"
    description: "Replace straight quotes with typographic quotes as you type."
    checked: false
  }
}
