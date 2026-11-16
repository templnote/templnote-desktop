import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "SearchService"

// Right pane of global search: the complete selected block with all matched
// terms highlighted.
Item {
  id: root

  // QVariantMap supplied by SearchResultsModel.resultAt().
  property var result: null

  readonly property bool hasResult: result !== null
                                    && result !== undefined
                                    && result.blockText !== undefined

  HighlightService {
    id: highlighter
  }

  Rectangle {
    anchors.fill: parent
    radius: 8
    color: Theme.background
  }

  ColumnLayout {
    anchors.fill: parent
    anchors.margins: 16
    spacing: 8
    visible: root.hasResult

    Text {
      Layout.fillWidth: true
      text: root.hasResult ? root.result.documentTitle : ""
      color: Theme.text
      font.pixelSize: 16
      font.bold: true
      elide: Text.ElideRight
    }

    Text {
      Layout.fillWidth: true
      text: root.hasResult
            ? (root.result.documentRelativePath !== undefined && root.result.documentRelativePath.length > 0
               ? root.result.documentRelativePath
               : root.result.documentPath)
            : ""
      color: Theme.textFaint
      font.pixelSize: 11
      elide: Text.ElideMiddle
    }

    Text {
      Layout.fillWidth: true
      text: root.hasResult
            ? root.result.matchCount + " "
              + (root.result.matchCount === 1 ? "match" : "matches")
              + " in this block"
            : ""
      color: Theme.textMuted
      font.pixelSize: 11
    }

    Item {
      Layout.preferredHeight: 6
    }

    Flickable {
      id: previewFlickable

      Layout.fillWidth: true
      Layout.fillHeight: true

      clip: true
      contentWidth: width
      contentHeight: previewText.implicitHeight

      ScrollBar.vertical: ScrollBar {}

      Text {
        id: previewText

        width: previewFlickable.width
        textFormat: Text.RichText
        text: root.hasResult
              ? highlighter.highlight(
                  root.result.blockText,
                  root.result.ranges,
                  highlighter.blendColor(Theme.accent,
                                         Theme.background, 0.34))
              : ""
        color: Theme.text
        font.pixelSize: 15
        lineHeight: 1.35
        wrapMode: Text.Wrap
      }
    }
  }

  Text {
    anchors.centerIn: parent
    width: parent.width - 64
    visible: !root.hasResult
    text: root.result === null || root.result === undefined
          ? "Select a result to preview it"
          : ""
    color: Theme.textFaint
    font.pixelSize: 13
    horizontalAlignment: Text.AlignHCenter
    wrapMode: Text.Wrap
  }
}
