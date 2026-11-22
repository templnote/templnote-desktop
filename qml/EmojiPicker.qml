import QtQuick
import QtQuick.Controls

// Floating emoji picker opened by typing ":". Emoji data comes from the
// vendored Emojibase dataset exposed as the EmojiDatabase context property.
Rectangle {
  id: root

  signal emojiSelected(string emoji, string shortcode)
  signal dismissed()

  property string query: ""
  property int selectedIndex: 0
  readonly property var selectedEmoji: emojiGrid.count > 0
      ? emojiGrid.model[Math.max(0, Math.min(selectedIndex, emojiGrid.count - 1))]
      : null

  width: 360
  height: 320
  radius: 10
  color: Theme.surface
  border.color: Theme.border
  border.width: 1
  visible: false
  z: 40
  opacity: 0
  scale: 0.94
  transformOrigin: Item.TopLeft

  Behavior on opacity {
    NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
  }
  Behavior on scale {
    NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
  }

  function setQuery(text) {
    if (root.query === (text || ""))
      return
    root.query = text || ""
    root.selectedIndex = 0
    filterEmojis()
    searchInput.text = root.query
  }

  function openFor(text) {
    const wasVisible = root.visible
    setQuery(text || "")
    root.visible = true
    root.opacity = 1
    root.scale = 1
    if (!wasVisible)
      searchInput.forceActiveFocus()
  }

  function close() {
    root.opacity = 0
    root.scale = 0.94
    root.visible = false
  }

  function filterEmojis() {
    if (root.query.length > 0)
      emojiGrid.model = EmojiDatabase.search(root.query, 240)
    else if (groupList.currentIndex >= 0)
      emojiGrid.model = EmojiDatabase.forGroup(groupList.currentIndex, 240)
    else
      emojiGrid.model = EmojiDatabase.search("", 240)
    root.selectedIndex = 0
  }

  function moveSelection(direction) {
    if (emojiGrid.count === 0)
      return
    const columns = Math.max(1, Math.floor(emojiGrid.width / 36))
    let next = root.selectedIndex + direction * (Math.abs(direction) > 1 ? columns : 1)
    next = Math.max(0, Math.min(emojiGrid.count - 1, next))
    root.selectedIndex = next
    emojiGrid.positionViewAtIndex(next, GridView.Visible)
  }

  function activateSelection() {
    const emoji = root.selectedEmoji
    if (!emoji)
      return
    root.emojiSelected(emoji.emoji, emoji.shortcode)
    root.close()
  }

  Column {
    anchors.fill: parent
    anchors.margins: 8
    spacing: 6

    Rectangle {
      width: parent.width
      height: 32
      radius: 6
      color: Theme.surfaceAlt
      border.color: searchInput.activeFocus ? Theme.accent : Theme.border
      border.width: 1

      TextField {
        id: searchInput
        anchors.fill: parent
        leftPadding: 10
        rightPadding: 10
        topPadding: 0
        bottomPadding: 0
        verticalAlignment: TextInput.AlignVCenter
        placeholderText: "Search emoji (type to filter)"
        placeholderTextColor: Theme.textFaint
        color: Theme.text
        font.pixelSize: 12
        background: null

        text: root.query
        onTextChanged: {
          root.query = text
          root.filterEmojis()
        }

        Keys.onUpPressed: function(event) { event.accepted = true; root.moveSelection(-1) }
        Keys.onDownPressed: function(event) { event.accepted = true; root.moveSelection(1) }
        Keys.onLeftPressed: function(event) { event.accepted = true; root.moveSelection(-1) }
        Keys.onRightPressed: function(event) { event.accepted = true; root.moveSelection(1) }
        Keys.onReturnPressed: function(event) { event.accepted = true; root.activateSelection() }
        Keys.onEscapePressed: function(event) { event.accepted = true; root.dismissed() }
      }
    }

    ListView {
      id: groupList
      width: parent.width
      height: 26
      orientation: ListView.Horizontal
      clip: true
      spacing: 4
      model: EmojiDatabase.groups()
      currentIndex: 0
      highlight: Rectangle {
        color: Theme.tint(Theme.accent, 0.18)
        radius: 4
      }
      delegate: Item {
        width: groupLabel.implicitWidth + 14
        height: 24

        Text {
          id: groupLabel
          anchors.centerIn: parent
          text: modelData.name
          color: groupList.currentIndex === index ? Theme.accent : Theme.textMuted
          font.pixelSize: 11
        }

        MouseArea {
          anchors.fill: parent
          onClicked: {
            groupList.currentIndex = index
            root.query = ""
            searchInput.text = ""
            root.filterEmojis()
          }
        }
      }
      onCurrentIndexChanged: {
        if (root.query.length === 0)
          root.filterEmojis()
      }
    }

    GridView {
      id: emojiGrid
      width: parent.width
      height: parent.height - 70
      clip: true
      cellWidth: 36
      cellHeight: 36
      model: []
      boundsBehavior: Flickable.StopAtBounds

      delegate: Item {
        width: 36
        height: 36

        Rectangle {
          anchors.fill: parent
          anchors.margins: 2
          radius: 6
          color: root.selectedIndex === index
                 ? Theme.tint(Theme.accent, 0.22)
                 : (emojiMouse.containsMouse ? Theme.surfaceHover : "transparent")
        }

        Text {
          anchors.centerIn: parent
          text: modelData.emoji
          font.pixelSize: 20
        }

        ToolTip.text: modelData.label + (modelData.shortcode
                     ? "  :" + modelData.shortcode + ":" : "")
        ToolTip.visible: emojiMouse.containsMouse
        ToolTip.delay: 400

        MouseArea {
          id: emojiMouse
          anchors.fill: parent
          hoverEnabled: true
          onClicked: {
            root.selectedIndex = index
            root.activateSelection()
          }
        }
      }
    }
  }
}
