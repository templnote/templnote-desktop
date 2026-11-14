import QtQuick

Column {
  id: root

  width: parent ? parent.width : 0
  spacing: 28

  Column {
    width: parent.width
    spacing: 12

    Text {
      text: "Theme"
      color: Theme.text
      font.pixelSize: 15
      font.bold: true
    }

    Text {
      width: parent.width
      text: "Choose whether Templnote follows your system appearance or always stays light or dark."
      color: Theme.textMuted
      font.pixelSize: 12
      wrapMode: Text.Wrap
    }

    Row {
      spacing: 8

      Repeater {
        model: [
          { key: "system", label: "System" },
          { key: "light", label: "Light" },
          { key: "dark", label: "Dark" }
        ]

        delegate: Rectangle {
          id: modeButton

          required property var modelData

          readonly property bool selected: Theme.mode === modelData.key

          width: 104
          height: 38
          radius: 8
          color: selected ? Theme.tint(Theme.accent, 0.16) : Theme.surfaceAlt
          border.width: selected ? 1 : 0
          border.color: Theme.accent

          Text {
            anchors.centerIn: parent
            text: modeButton.modelData.label
            color: modeButton.selected ? Theme.accent : Theme.textMuted
            font.pixelSize: 13
          }

          MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: Theme.setMode(modeButton.modelData.key)
          }
        }
      }
    }
  }

  Column {
    width: parent.width
    spacing: 12

    Text {
      text: "Colour scheme"
      color: Theme.text
      font.pixelSize: 15
      font.bold: true
    }

    Text {
      width: parent.width
      text: "Colour schemes provide a light and a dark palette. The selected theme mode decides which palette is used."
      color: Theme.textMuted
      font.pixelSize: 12
      wrapMode: Text.Wrap
    }

    Grid {
      width: parent.width
      columns: 2
      columnSpacing: 12
      rowSpacing: 12

      Repeater {
        model: Theme.schemeKeys

        delegate: Rectangle {
          id: schemeCard

          required property string modelData

          readonly property var schemeInfo: Theme.schemes[modelData]
          readonly property var preview: schemeInfo[Theme.resolvedMode]
          readonly property bool selected: Theme.scheme === modelData

          width: (root.width - 12) / 2
          height: 108
          radius: 8
          color: Theme.surfaceAlt
          border.width: selected ? 2 : 1
          border.color: selected ? Theme.accent : Theme.border

          Column {
            anchors {
              fill: parent
              margins: 14
            }

            spacing: 12

            Row {
              width: parent.width
              spacing: 8

              Text {
                width: parent.width - 22
                text: schemeCard.schemeInfo.name[Theme.resolvedMode]
                color: Theme.text
                font.pixelSize: 14
                font.bold: true
                elide: Text.ElideRight
              }

              Text {
                visible: schemeCard.selected
                text: "\u2713"
                color: Theme.accent
                font.pixelSize: 14
                font.bold: true
              }
            }

            Row {
              spacing: 7

              Repeater {
                model: [
                  schemeCard.preview.background,
                  schemeCard.preview.surface,
                  schemeCard.preview.text,
                  schemeCard.preview.accent
                ]

                delegate: Rectangle {
                  required property color modelData

                  width: 20
                  height: 20
                  radius: 10
                  color: modelData
                  border.width: 1
                  border.color: Theme.tint(Theme.text, 0.16)
                }
              }
            }

            Text {
              width: parent.width
              text: Theme.resolvedMode === "dark" ? "Dark palette" : "Light palette"
              color: Theme.textFaint
              font.pixelSize: 11
            }
          }

          MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: Theme.setScheme(schemeCard.modelData)
          }
        }
      }
    }
  }

  Rectangle {
    width: parent.width
    height: 1
    color: Theme.border
  }

  Column {
    width: parent.width
    spacing: 6

    Text {
      text: "Preview"
      color: Theme.text
      font.pixelSize: 15
      font.bold: true
    }

    Rectangle {
      width: parent.width
      height: 92
      radius: 8
      color: Theme.background
      border.width: 1
      border.color: Theme.border

      Column {
        anchors {
          fill: parent
          margins: 16
        }

        spacing: 7

        Text {
          text: Theme.schemeName
          color: Theme.text
          font.pixelSize: 17
          font.bold: true
        }

        Text {
          text: "The quick brown fox jumps over the lazy dog."
          color: Theme.textMuted
          font.pixelSize: 13
        }

        Row {
          spacing: 8

          Rectangle {
            width: 92
            height: 26
            radius: 8
            color: Theme.accent

            Text {
              anchors.centerIn: parent
              text: "Accent"
              color: Theme.accentText
              font.pixelSize: 11
              font.bold: true
            }
          }

          Rectangle {
            width: 92
            height: 26
            radius: 8
            color: Theme.surfaceAlt
            border.width: 1
            border.color: Theme.border

            Text {
              anchors.centerIn: parent
              text: "Surface"
              color: Theme.textMuted
              font.pixelSize: 11
            }
          }
        }
      }
    }
  }
}
