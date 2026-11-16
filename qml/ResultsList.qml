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
  property string selectedDocumentPath: ""
  property string emptyText: "Type to search all documents"

  signal resultClicked(int index)
  signal documentHeaderClicked(string path)
  signal documentHeaderDoubleClicked(string path)

  HighlightService {
    id: highlighter
  }

  function positionCurrent() {
    if (root.selectedIndex >= 0)
      resultView.positionViewAtIndex(root.selectedIndex, ListView.Contain)
  }

  function positionDocument(path) {
    if (!root.resultsModel || root.resultsModel.count === 0 || !path)
      return
    for (let i = 0; i < root.resultsModel.count; ++i) {
      const res = root.resultsModel.resultAt(i)
      if (res && res.documentPath === path) {
        resultView.positionViewAtIndex(i, ListView.Beginning)
        break
      }
    }
  }

  Rectangle {
    anchors.fill: parent
    radius: 8
    color: Theme.background
  }

  ListView {
    id: resultView

    anchors {
      fill: parent
      margins: 6
    }
    clip: true
    model: root.resultsModel
    currentIndex: root.selectedIndex
    spacing: 2

    section.property: "documentPath"
    section.criteria: ViewSection.FullString

    section.delegate: Rectangle {
      id: sectionHeader

      required property string section

      readonly property bool isSelected: root.selectedDocumentPath === section

      width: resultView.width
      height: 48
      radius: 6
      color: isSelected
             ? Theme.surfaceHover
             : (headerMouse.containsMouse ? Theme.surfaceAlt : "transparent")

      Behavior on color {
        ColorAnimation {
          duration: 150
          easing.type: Easing.OutQuad
        }
      }

      Rectangle {
        visible: sectionHeader.isSelected

        anchors {
          left: parent.left
          verticalCenter: parent.verticalCenter
        }

        width: 3
        height: 18
        radius: 2
        color: Theme.accent
      }

      Column {
        anchors {
          left: parent.left
          right: countLabel.left
          verticalCenter: parent.verticalCenter
          leftMargin: sectionHeader.isSelected ? 14 : 8
          rightMargin: 8
        }

        spacing: 2

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
          text: root.resultsModel && root.resultsModel.relativeDocumentPath
                ? root.resultsModel.relativeDocumentPath(section)
                : section
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

      MouseArea {
        id: headerMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.documentHeaderClicked(sectionHeader.section)
        onDoubleClicked: root.documentHeaderDoubleClicked(sectionHeader.section)
      }
    }

    delegate: Rectangle {
      id: resultRow

      required property int index
      required property string snippet
      required property var snippetRanges

      readonly property bool selected: root.selectedIndex === index

      width: resultView.width
      height: snippetText.implicitHeight + 18
      radius: 6
      color: selected
             ? Theme.surfaceHover
             : (rowMouse.containsMouse ? Theme.surfaceAlt : "transparent")

      Behavior on color {
        ColorAnimation {
          duration: 150
          easing.type: Easing.OutQuad
        }
      }

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
