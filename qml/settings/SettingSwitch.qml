import QtQuick

// Compact themed switch, using the same visuals as SettingToggle's knob (the
// component used throughout Developer / General / Interface settings).
// Usable standalone where a full SettingToggle row would be too large.
Item {
  id: root

  property bool checked: false
  property string accessibleName: ""
  property bool focusable: true

  signal toggled(bool value)

  width: 42
  height: 24
  activeFocusOnTab: root.focusable

  Accessible.role: Accessible.Switch
  Accessible.name: root.accessibleName
  Accessible.checked: root.checked
  Accessible.focusable: root.focusable

  function toggleValue() {
    root.toggled(!root.checked)
  }

  Keys.onReturnPressed: function(event) {
    event.accepted = true
    root.toggleValue()
  }
  Keys.onEnterPressed: function(event) {
    event.accepted = true
    root.toggleValue()
  }
  Keys.onSpacePressed: function(event) {
    event.accepted = true
    root.toggleValue()
  }

  Rectangle {
    anchors.fill: parent
    radius: 12
    color: root.checked ? Theme.accent : Theme.surfaceHover
    border.width: root.checked ? 0 : 1
    border.color: Theme.border
  }

  Rectangle {
    width: 18
    height: 18
    radius: 9
    y: 3
    x: root.checked ? parent.width - width - 3 : 3
    color: root.checked ? Theme.accentText : Theme.textMuted

    Behavior on x {
      NumberAnimation {
        duration: 140
        easing.type: Easing.OutCubic
      }
    }
  }

  Rectangle {
    visible: root.activeFocus
    anchors.fill: parent
    anchors.margins: -3
    radius: 15
    color: "transparent"
    border.width: 1
    border.color: Theme.accent
  }

  MouseArea {
    anchors.fill: parent
    cursorShape: Qt.PointingHandCursor
    onClicked: root.toggleValue()
  }
}
