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

    anchors.fill: parent
    clip: true
    model: root.resultsModel
    currentIndex: root.selectedIndex
    spacing: 0

    section.property: "documentPath"
    section.criteria: ViewSection.FullString

    section.delegate: Rectangle {
      id: sectionHeader

      required property string section

      width: resultView.width
      height: 54
      color: Theme.surface

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
          leftMargin: 14
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
          rightMargin: 14
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

      width: resultView.width
      height: snippetText.implicitHeight + 20
      color: root.selectedIndex === index
             ? Theme.surfaceHover
             : (rowMouse.containsMouse ? Theme.surfaceAlt : "transparent")

      Rectangle {
        visible: root.selectedIndex === index

        anchors {
          left: parent.left
          top: parent.top
          bottom: parent.bottom
        }

        width: 3
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
        color: Theme.textMuted
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
