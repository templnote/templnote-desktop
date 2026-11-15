import QtQuick
import QtQuick.Controls
import "SearchService"

// Left pane of global search: results grouped by document, with one row per
// matching block. Navigation itself lives in SearchWindow; this component only
// displays the selected index and reports clicks.
Item {
  id: root

  property var resultsModel: null
  property int selectedIndex: -1
  property string emptyText: "Type to search all documents"

  signal resultClicked(int index)

  HighlightService {
    id: highlighter
  }

  function positionCurrent() {
    if (root.selectedIndex >= 0)
      resultView.positionViewAtIndex(root.selectedIndex, ListView.Contain)
  }

  Rectangle {
    anchors.fill: parent
    color: "transparent"
  }

  ListView {
    id: resultView

    anchors {
      fill: parent
      leftMargin: 8
      rightMargin: 8
    }
    clip: true
    model: root.resultsModel
    currentIndex: root.selectedIndex
    spacing: 4

    section.property: "documentPath"
    section.criteria: ViewSection.FullString

    section.delegate: Rectangle {
      id: sectionHeader

      required property string section

      width: resultView.width
      height: 54
      color: "transparent"

      Rectangle {
        anchors {
          left: parent.left
          right: parent.right
          top: parent.top
        }

        height: 1
        color: Theme.border
      }

      Column {
        anchors {
          left: parent.left
          right: countLabel.left
          verticalCenter: parent.verticalCenter
          leftMargin: 10
          rightMargin: 12
        }

        spacing: 3

        Text {
          width: parent.width
          text: root.resultsModel
                ? root.resultsModel.groupTitle(section)
                : section
          color: Theme.text
          font.pixelSize: 13
          font.bold: true
          elide: Text.ElideRight
        }

        Text {
          width: parent.width
          text: section
          color: Theme.textFaint
          font.pixelSize: 11
          elide: Text.ElideMiddle
        }
      }

      Text {
        id: countLabel

        anchors {
          right: parent.right
          rightMargin: 10
          verticalCenter: parent.verticalCenter
        }

        text: root.resultsModel
              ? root.resultsModel.groupMatchCount(section) + " matches"
              : ""
        color: Theme.textMuted
        font.pixelSize: 11
      }
    }

    delegate: Rectangle {
      id: resultRow

      required property int index
      required property string snippet
      required property var snippetRanges

      readonly property bool selected: root.selectedIndex === index

      width: resultView.width
      height: snippetText.implicitHeight + 20
      radius: 8
      color: selected
             ? Theme.tint(Theme.accent, 0.16)
             : (rowMouse.containsMouse ? Theme.surfaceHover : "transparent")

      Rectangle {
        visible: resultRow.selected

        anchors {
          left: parent.left
          verticalCenter: parent.verticalCenter
        }

        width: 3
        height: 18
        radius: 2
        color: Theme.accent
      }

      Text {
        id: snippetText

        anchors {
          left: parent.left
          right: parent.right
          verticalCenter: parent.verticalCenter
          leftMargin: 14
          rightMargin: 14
        }

        textFormat: Text.RichText
        text: highlighter.highlight(
                resultRow.snippet,
                resultRow.snippetRanges,
                highlighter.blendColor(Theme.accent, Theme.surface, 0.30))
        color: resultRow.selected ? Theme.text : Theme.textMuted
        font.pixelSize: 13
        wrapMode: Text.Wrap
        maximumLineCount: 3
        elide: Text.ElideRight
      }

      MouseArea {
        id: rowMouse

        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.resultClicked(resultRow.index)
      }
    }
  }

  Text {
    anchors.centerIn: parent
    width: parent.width - 40
    visible: !root.resultsModel || root.resultsModel.count === 0
    text: root.emptyText
    color: Theme.textFaint
    font.pixelSize: 13
    horizontalAlignment: Text.AlignHCenter
    wrapMode: Text.Wrap
  }
}
