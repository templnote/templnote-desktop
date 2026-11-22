import QtQuick
import QtQuick.Controls
import QtQuick.Window
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
  // Keyboard activation/navigation from a focused row delegate. SearchWindow
  // maps these to accept() and move() respectively; the row itself cannot
  // reach those functions because they live on the host's root. Activation is
  // intentionally index-less: Up/Down already changed the current selection,
  // so accept() must act on that rather than the now-stale focused index.
  signal resultActivated()
  signal navigationRequested(int direction)

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

  // ListView creates both section and row delegates as children of its
  // contentItem. Expose them in visual order so the host can build a focus
  // ring that includes document headers and individual result rows.
  function focusTargets() {
    const items = []
    const contentItem = resultView.contentItem

    if (!contentItem)
      return items

    const children = contentItem.children
    for (let i = 0; i < children.length; ++i) {
      const child = children[i]
      if (child && child.visible &&
          (child.isSectionHeader || child.isResultRow))
        items.push(child)
    }

    items.sort(function(left, right) {
      return left.y - right.y || left.x - right.x
    })
    return items
  }

  // Tab cycles through every visible header/row delegate, including ones
  // ListView has parked outside the viewport. Map the focused delegate into
  // the view's content coordinates and scroll just enough to reveal it.
  function ensureFocusVisible() {
    const item = Window.window ? Window.window.activeFocusItem : null
    const contentItem = resultView.contentItem
    if (!item || !item.visible || !contentItem ||
        !contentItem.isAncestorOf(item))
      return

    const rect = contentItem.mapFromItem(item, 0, 0, item.width, item.height)
    const margin = 6
    const maxContentY = Math.max(0, resultView.contentHeight - resultView.height)
    let y = resultView.contentY

    if (rect.y - margin < y)
      y = rect.y - margin
    else if (rect.y + rect.height + margin > y + resultView.height)
      y = rect.y + rect.height + margin - resultView.height

    resultView.contentY = Math.max(0, Math.min(y, maxContentY))
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

      // Marker used by focusTargets() to distinguish headers from row delegates.
      property bool isSectionHeader: true
      readonly property bool isSelected: root.selectedDocumentPath === section

      activeFocusOnTab: true

      Accessible.name: root.resultsModel
                       ? root.resultsModel.groupTitle(section) + ", "
                         + root.resultsModel.groupMatchCount(section)
                           + " matches"
                       : section
      Accessible.description: root.resultsModel && root.resultsModel.relativeDocumentPath
                              ? root.resultsModel.relativeDocumentPath(section)
                              : section
      Accessible.role: Accessible.Button
      Accessible.selected: sectionHeader.isSelected
      Accessible.checked: sectionHeader.isSelected
      Accessible.focusable: true
      Accessible.onPressAction: root.documentHeaderClicked(sectionHeader.section)

      Keys.onReturnPressed: function(event) {
        event.accepted = true
        root.documentHeaderClicked(sectionHeader.section)
      }
      Keys.onSpacePressed: function(event) {
        event.accepted = true
        root.documentHeaderClicked(sectionHeader.section)
      }

      width: resultView.width
      height: 48
      radius: 6
      color: isSelected
             ? Theme.surfaceHover
             : (headerMouse.containsMouse || sectionHeader.activeFocus
                ? Theme.surfaceAlt : "transparent")
      border.width: sectionHeader.activeFocus ? 1 : 0
      border.color: Theme.accent

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

      // Marker used by focusTargets() to distinguish rows from section headers.
      property bool isResultRow: true
      readonly property bool selected: root.selectedIndex === index

      activeFocusOnTab: true

      Accessible.name: resultRow.snippet
      Accessible.role: Accessible.ListItem
      Accessible.selected: resultRow.selected
      Accessible.focusable: true
      Accessible.onPressAction: root.resultClicked(resultRow.index)

      // A clicked row keeps active focus, so it must own the navigation and
      // activation keys as well. Otherwise Up/Down/Enter are swallowed here
      // and never reach the search field's handlers.
      Keys.onUpPressed: function(event) {
        event.accepted = true
        root.navigationRequested(-1)
      }
      Keys.onDownPressed: function(event) {
        event.accepted = true
        root.navigationRequested(1)
      }
      Keys.onReturnPressed: function(event) {
        event.accepted = true
        root.resultActivated()
      }
      Keys.onEnterPressed: function(event) {
        event.accepted = true
        root.resultActivated()
      }
      Keys.onSpacePressed: function(event) {
        event.accepted = true
        root.resultActivated()
      }

      width: resultView.width
      height: snippetText.implicitHeight + 18
      radius: 6
      color: selected
             ? Theme.surfaceHover
             : (rowMouse.containsMouse || resultRow.activeFocus
                ? Theme.surfaceAlt : "transparent")
      border.width: resultRow.activeFocus ? 1 : 0
      border.color: Theme.accent

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
        onClicked: {
          // A clicked row becomes the focus target so Tab and the focus border
          // continue from the row the user actually selected.
          resultRow.forceActiveFocus()
          root.resultClicked(resultRow.index)
        }
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
