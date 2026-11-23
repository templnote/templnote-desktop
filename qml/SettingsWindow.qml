import QtQuick
import QtQuick.Controls
import QtQuick.Window

// Centrally floating settings surface. It lives inside the main window so it
// can dim the editor and keep the app feeling like a single window.
Item {
  id: root

  anchors.fill: parent
  visible: false
  z: 1000
  focus: true

  // Read-only view of the persisted preference. Selecting a section must use
  // setActiveSection(); assigning here would destroy this binding.
  readonly property string activeSection: Preferences.settingsActiveSection

  // Safe internal value: always a known section key, even when the persisted
  // preference is empty or stale.
  property string currentSectionKey: {
    const key = root.activeSection
    for (let i = 0; i < root.sections.length; ++i) {
      if (root.sections[i].key === key)
        return key
    }
    return root.sections[0].key
  }

  function setActiveSection(section) {
    if (typeof section !== "string" || section.length === 0)
      return
    if (Preferences.settingsActiveSection !== section)
      Preferences.settingsActiveSection = section
  }

  // Focus to hand back to when the settings surface closes.
  property var previousFocus: null

  readonly property var sections: [
    {
      key: "general",
      title: "General",
      description: "Startup, updates and application information",
      file: "settings/GeneralSettings.qml"
    },
    {
      key: "appearance",
      title: "Appearance",
      description: "Theme mode and colour scheme",
      file: "settings/AppearanceSettings.qml"
    },
    {
      key: "interface",
      title: "Interface",
      description: "Layout and paper aesthetic",
      file: "settings/InterfaceSettings.qml"
    },
    {
      key: "editor",
      title: "Editor",
      description: "Writing, formatting and editing behaviour",
      file: "settings/EditorSettings.qml"
    },
    {
      key: "ai",
      title: "AI Providers",
      description: "Connect providers and choose which models are available",
      file: "settings/AISettings.qml"
    },
    {
      key: "plugins",
      title: "Plugins",
      description: "Extend Templnote with plugins",
      file: "settings/PluginsSettings.qml"
    },
    {
      key: "plugin-store",
      title: "Plugin Store",
      description: "Browse and install signed community plugins",
      file: "settings/PluginStoreSettings.qml"
    },
    {
      key: "developer",
      title: "Developer",
      description: "Inspection, diagnostics and debug visualizers",
      file: "settings/DeveloperSettings.qml"
    }
  ]

  readonly property var currentSection: {
    for (let i = 0; i < sections.length; ++i) {
      if (sections[i].key === currentSectionKey)
        return sections[i]
    }
    return sections[0]
  }

  ModalFocusGuard { id: settingsFocusGuard }

  // The tab ring is rebuilt from the close button, the section rows and every
  // focusable control inside the currently loaded page. It is refreshed on
  // each Tab as well, because pages such as Plugins swap their whole view when
  // a plugin detail is opened or the search filter changes.
  function refreshFocusTargets() {
    const targets = [closeButton]

    for (let i = 0; i < sectionRepeater.count; ++i) {
      const row = sectionRepeater.itemAt(i)
      if (row && row.visible && row.enabled)
        targets.push(row)
    }

    if (pageLoader.item)
      settingsFocusGuard.collectFocusables(pageLoader.item, targets)

    settingsFocusGuard.setTargets(targets)
  }

  // A focus cycle can land on a control in the loaded page that is scrolled
  // out of view. Map the focused item into the scroller's content coordinates
  // and scroll just enough to reveal it. Flickable has no ensureVisible()
  // method in Qt 6 (that is only a custom helper in Qt examples), so adjust
  // contentY directly and clamp it to the scrollable range.
  function ensureFocusVisible() {
    const item = Window.window ? Window.window.activeFocusItem : null
    const content = scroller ? scroller.contentItem : null
    if (!item || !content || !item.visible || !content.isAncestorOf(item))
      return
    const rect = content.mapFromItem(item, 0, 0, item.width, item.height)
    const margin = 12
    const maxContentY = Math.max(0, scroller.contentHeight - scroller.height)
    let y = scroller.contentY

    if (rect.y - margin < y)
      y = rect.y - margin
    else if (rect.y + rect.height + margin > y + scroller.height)
      y = rect.y + rect.height + margin - scroller.height

    scroller.contentY = Math.max(0, Math.min(y, maxContentY))
  }

  Connections {
    target: pageLoader

    function onItemChanged() {
      Qt.callLater(refreshFocusTargets)
    }
  }

  Keys.onTabPressed: function(event) {
    if (!root.visible)
      return
    event.accepted = true
    refreshFocusTargets()
    settingsFocusGuard.cycle(1)
    Qt.callLater(root.ensureFocusVisible)
  }
  Keys.onBacktabPressed: function(event) {
    if (!root.visible)
      return
    event.accepted = true
    refreshFocusTargets()
    settingsFocusGuard.cycle(-1)
    Qt.callLater(root.ensureFocusVisible)
  }

  function open(section) {
    // Opening again while already visible must not overwrite the original
    // focus target with a control inside the settings panel.
    if (!root.visible)
      root.previousFocus = Window.window ? Window.window.activeFocusItem : null
    if (section !== undefined)
      root.setActiveSection(section)
    visible = true

    Qt.callLater(function() {
      refreshFocusTargets()
      closeButton.forceActiveFocus()
    })
  }

  function close() {
    if (!root.visible)
      return

    const focusTarget = root.previousFocus
    visible = false
    root.previousFocus = null
    settingsFocusGuard.setTargets([])

    if (focusTarget)
      focusTarget.forceActiveFocus()
  }

  Shortcut {
    sequence: "Escape"
    enabled: root.visible
    onActivated: root.close()
  }

  Rectangle {
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0.52)

    MouseArea {
      anchors.fill: parent
      onClicked: root.close()
    }
  }

  Rectangle {
    id: panel

    anchors.centerIn: parent
    width: Math.min(root.width - 80, 900)
    height: Math.min(root.height - 80, 580)
    radius: 8
    color: Theme.surface
    border.width: 1
    border.color: Theme.border
    clip: true

    MouseArea {
      anchors.fill: parent
    }

    Rectangle {
      id: sidebar

      anchors {
        top: parent.top
        topMargin: 1
        left: parent.left
        leftMargin: 1
        bottom: parent.bottom
        bottomMargin: 1
      }

      width: 207
      color: Theme.surfaceAlt
      topLeftRadius: 8
      bottomLeftRadius: 8

      Column {
        anchors {
          top: parent.top
          left: parent.left
          right: parent.right
          topMargin: 22
          leftMargin: 14
          rightMargin: 14
        }

        spacing: 4

        Column {
          width: parent.width
          topPadding: 6
          bottomPadding: 18
          leftPadding: 10

          spacing: 2

          Text {
            text: "Templnote"
            color: Theme.text
            font.pixelSize: 16
            font.bold: true
          }

          Text {
            text: "Settings"
            color: Theme.textFaint
            font.pixelSize: 12
          }
        }

        Repeater {
          id: sectionRepeater
          model: root.sections

          delegate: Rectangle {
            id: sectionRow

            required property var modelData

            readonly property bool selected: root.currentSectionKey === modelData.key

            focus: sectionRow.selected
            activeFocusOnTab: true

            Accessible.name: sectionRow.modelData.title
            Accessible.description: sectionRow.modelData.description
            Accessible.role: Accessible.Button
            Accessible.selected: sectionRow.selected
            Accessible.checked: sectionRow.selected
            Accessible.focusable: true
            Accessible.onPressAction: root.setActiveSection(sectionRow.modelData.key)

            Keys.onReturnPressed: root.setActiveSection(sectionRow.modelData.key)
            Keys.onSpacePressed: root.setActiveSection(sectionRow.modelData.key)

            width: parent.width
            height: 38
            radius: 8
            color: selected
                   ? Theme.tint(Theme.accent, 0.16)
                   : sectionMouse.containsMouse ? Theme.surfaceHover : "transparent"

            Rectangle {
              visible: sectionRow.selected

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
              anchors {
                left: parent.left
                leftMargin: 14
                right: parent.right
                rightMargin: 10
                verticalCenter: parent.verticalCenter
              }

              text: sectionRow.modelData.title
              color: sectionRow.selected ? Theme.accent : Theme.textMuted
              font.pixelSize: 13
              elide: Text.ElideRight
            }

            MouseArea {
              id: sectionMouse

              anchors.fill: parent
              hoverEnabled: true
              cursorShape: Qt.PointingHandCursor
              onClicked: root.setActiveSection(sectionRow.modelData.key)
            }
          }
        }
      }

      Text {
        anchors {
          left: parent.left
          leftMargin: 24
          bottom: parent.bottom
          bottomMargin: 18
        }

        text: "v0.1.0"
        color: Theme.textFaint
        font.pixelSize: 11
      }
    }

    Item {
      id: content

      anchors {
        top: parent.top
        left: sidebar.right
        right: parent.right
        bottom: parent.bottom
      }

      Item {
        id: header

        anchors {
          top: parent.top
          left: parent.left
          right: parent.right
        }

        height: 74

        Column {
          anchors {
            left: parent.left
            leftMargin: 28
            right: closeButton.left
            rightMargin: 16
            verticalCenter: parent.verticalCenter
          }

          spacing: 3

          Text {
            width: parent.width
            text: root.currentSection.title
            color: Theme.text
            font.pixelSize: 20
            font.bold: true
            elide: Text.ElideRight
          }

          Text {
            width: parent.width
            text: root.currentSection.description
            color: Theme.textMuted
            font.pixelSize: 12
            elide: Text.ElideRight
          }
        }

        Rectangle {
          id: closeButton

          anchors {
            right: parent.right
            rightMargin: 20
            verticalCenter: parent.verticalCenter
          }

          focus: true
          activeFocusOnTab: true

          Accessible.name: "Close settings"
          Accessible.role: Accessible.Button
          Accessible.focusable: true
          Accessible.onPressAction: root.close()

          Keys.onReturnPressed: root.close()
          Keys.onSpacePressed: root.close()

          width: 30
          height: 30
          radius: 8
          color: closeMouse.containsMouse ? Theme.surfaceHover : "transparent"

          Text {
            anchors.centerIn: parent
            text: "\u2715"
            color: Theme.textMuted
            font.pixelSize: 13
          }

          MouseArea {
            id: closeMouse

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.close()
          }
        }

        Rectangle {
          anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
          }

          height: 1
          color: Theme.border
        }
      }

      Flickable {
        id: scroller

        anchors {
          top: header.bottom
          left: parent.left
          right: parent.right
          bottom: parent.bottom
          leftMargin: 28
          rightMargin: 28
          topMargin: 24
          bottomMargin: 24
        }

        contentWidth: width
        contentHeight: pageLoader.item ? pageLoader.item.implicitHeight : 0
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Loader {
          id: pageLoader

          width: scroller.width
          source: root.currentSection.file
        }
      }
    }
  }
}
