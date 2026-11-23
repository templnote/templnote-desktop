import QtQuick

// A settings row with a title, an optional description and a custom switch.
Item {
  id: root

  property string title: ""
  property string description: ""
  property bool checked: false

  signal toggled(bool value)

  width: parent ? parent.width : 0
  implicitHeight: Math.max(labels.implicitHeight, 24) + 20

  activeFocusOnTab: true

  Accessible.name: root.title
  Accessible.description: root.description
  Accessible.role: Accessible.CheckBox
  Accessible.checked: root.checked
  Accessible.focusable: true
  // A CheckBox exposes one action only: some ATs emit both press and toggle
  // for a single gesture, which would flip the value twice.
  Accessible.onToggleAction: root.toggleValue()

  function toggleValue() {
    // `checked` is bound to a preference in the containing page. Assigning to
    // it here would break that binding, so just report the requested value;
    // the page writes the preference and the binding updates this property.
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

  Column {
    id: labels

    anchors {
      left: parent.left
      right: toggle.left
      rightMargin: 20
      verticalCenter: parent.verticalCenter
    }

    spacing: 3

    Text {
      width: parent.width
      text: root.title
      color: Theme.text
      font.pixelSize: 14
      wrapMode: Text.Wrap
    }

    Text {
      visible: root.description.length > 0
      width: parent.width
      text: root.description
      color: Theme.textMuted
      font.pixelSize: 12
      wrapMode: Text.Wrap
    }
  }

  SettingSwitch {
    // The labels column anchors to this id; it was lost when the inline
    // switch was replaced by SettingSwitch, which hid every toggle's text.
    id: toggle

    anchors {
      right: parent.right
      verticalCenter: parent.verticalCenter
    }

    checked: root.checked
    // The row itself is the tab stop; avoid a second one for the knob.
    focusable: false
    accessibleName: root.title
    onToggled: function(value) { root.toggleValue() }
  }

  Rectangle {
    visible: root.activeFocus

    anchors {
      fill: parent
      margins: 2
    }

    radius: 6
    color: "transparent"
    border.width: 1
    border.color: Theme.accent
  }
}
