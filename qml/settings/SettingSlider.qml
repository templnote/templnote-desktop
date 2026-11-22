import QtQuick
import QtQuick.Controls

Item {
  id: root

  property string title: ""
  property string description: ""
  property real from: 0.0
  property real to: 1.0
  property real stepSize: 0.01
  property real value: 0.0
  property string valueText: value.toFixed(2)

  signal moved(real value)

  width: parent ? parent.width : 0
  implicitHeight: Math.max(labels.implicitHeight, controls.implicitHeight) + 16

  // No accessibility node on the root: the Slider Control below is the real
  // focusable, and already exposes role/value/minimum/maximum. Giving the
  // row a second Slider node made screen readers announce a phantom control.

  Column {
    id: labels

    anchors {
      left: parent.left
      right: controls.left
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

  Row {
    id: controls

    anchors {
      right: parent.right
      verticalCenter: parent.verticalCenter
    }

    spacing: 12

    Slider {
      id: slider
      width: 140
      from: root.from
      to: root.to
      stepSize: root.stepSize
      value: root.value
      activeFocusOnTab: true

      Accessible.name: root.title
      Accessible.description: root.description

      onMoved: {
        // value is bound to a preference in the page; report the new value and
        // let the page persist it rather than overwriting the binding.
        root.moved(slider.value)
      }

      background: Rectangle {
        x: slider.leftPadding
        y: slider.topPadding + slider.availableHeight / 2 - height / 2
        implicitWidth: 140
        implicitHeight: 4
        width: slider.availableWidth
        height: implicitHeight
        radius: 2
        color: Theme.border

        Rectangle {
          width: slider.visualPosition * parent.width
          height: parent.height
          color: Theme.accent
          radius: 2
        }
      }

      handle: Rectangle {
        x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
        y: slider.topPadding + slider.availableHeight / 2 - height / 2
        implicitWidth: 16
        implicitHeight: 16
        radius: 8
        color: slider.pressed ? Theme.accent : Theme.surface
        border.color: Theme.accent
        border.width: slider.activeFocus ? 3 : 2
      }

      Rectangle {
        anchors {
          fill: parent
          margins: -3
        }

        visible: slider.activeFocus
        radius: 8
        color: "transparent"
        border.width: 1
        border.color: Theme.accent
      }
    }

    Text {
      width: 38
      text: root.valueText
      color: Theme.textMuted
      font.pixelSize: 12
      font.family: "Monospace"
      horizontalAlignment: Text.AlignRight
      anchors.verticalCenter: parent.verticalCenter
    }
  }
}
