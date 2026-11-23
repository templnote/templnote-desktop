import QtQuick
import QtQuick.Controls

// Shared settings button style, matching the dialog buttons in Main.qml:
//   - secondary: transparent with a themed border
//   - primary:   accent fill with accentText label
//   - danger:    destructive actions, using the theme's danger colour
Button {
  id: root

  property bool primary: false
  property bool danger: false
  property string accessibleName: text

  height: 32
  leftPadding: 16
  rightPadding: 16
  activeFocusOnTab: true

  Accessible.role: Accessible.Button
  Accessible.name: root.accessibleName

  background: Rectangle {
    radius: 8
    color: {
      if (!root.enabled)
        return Theme.surfaceAlt
      if (root.danger) {
        return root.down ? Qt.darker(Theme.danger, 1.2)
                         : (root.hovered ? Qt.darker(Theme.danger, 1.1)
                                         : Theme.danger)
      }
      if (root.primary) {
        return root.down ? Qt.darker(Theme.accent, 1.15)
                         : (root.hovered ? Qt.lighter(Theme.accent, 1.1)
                                         : Theme.accent)
      }
      return root.down ? Theme.surfaceHover
                       : (root.hovered ? Theme.surfaceAlt : "transparent")
    }
    border.width: (root.primary || root.danger) ? 0 : 1
    border.color: root.activeFocus ? Theme.accent : Theme.border
  }

  contentItem: Text {
    text: root.text
    color: (root.primary || root.danger) ? Theme.accentText : Theme.text
    font.pixelSize: 13
    font.bold: root.primary || root.danger
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
  }
}
