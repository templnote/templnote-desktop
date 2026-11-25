import QtQuick
import QtQuick.Controls

Item {
  id: root

  width: parent ? parent.width : 0
  implicitHeight: contentColumn.implicitHeight

  property string selectedPluginId: ""
  property string searchText: ""

  // Non-modal, inline feedback for automatic dependency changes. The C++
  // signals used to be emitted into the void; surface them as a plain text
  // row so the user can see what enable/disable cascaded to.
  property string dependencyNotice: ""

  // A notice describes the most recent automatic dependency change. Clear it
  // as soon as the user does something else (or a newer notice supersedes it)
  // so the row can never keep displaying stale, misleading information. It is
  // also reused for the truthful "cannot enable" notice, which is derived
  // directly from missingDependencies() instead of a misleading signal.
  function clearDependencyNotice() {
    root.dependencyNotice = ""
  }

  // Uninstall is a non-modal, two-step inline flow in the detail view:
  // "Uninstall" -> "Confirm uninstall" / "Cancel". `uninstallError` keeps a
  // failure visible instead of silently dropping the click.
  property bool confirmingUninstall: false
  property string uninstallError: ""

  // Q_INVOKABLE methods are not reactive; reading stateRevision gives these
  // bindings an explicit dependency so enable/disable/uninstall refresh them.
  property string detailMissingDepsText: {
    if (!pluginManager || root.selectedPluginId.length === 0) return ""
    const revision = pluginManager.stateRevision
    const deps = pluginManager.missingDependencies(root.selectedPluginId)
    return deps.length > 0 ? deps.join(", ") : ""
  }
  property bool detailCanUninstall: {
    if (!pluginManager || root.selectedPluginId.length === 0) return false
    const revision = pluginManager.stateRevision
    return pluginManager.canUninstall(root.selectedPluginId)
  }
  function resetUninstallUi() {
    root.confirmingUninstall = false
    root.uninstallError = ""
  }

  function requestUninstall() {
    root.clearDependencyNotice()
    root.uninstallError = ""
    root.confirmingUninstall = true
    Qt.callLater(function() {
      if (confirmUninstallBtn.visible) confirmUninstallBtn.forceActiveFocus()
    })
  }

  function cancelUninstall() {
    root.confirmingUninstall = false
    root.uninstallError = ""
    Qt.callLater(function() {
      if (uninstallBtn.visible) uninstallBtn.forceActiveFocus()
    })
  }

  function confirmUninstall() {
    if (!pluginManager || root.selectedPluginId.length === 0) return
    const id = root.selectedPluginId
    if (!pluginManager.uninstallPlugin(id)) {
      root.confirmingUninstall = false
      root.uninstallError = "Could not uninstall " + id +
                            ". It may be in use or already removed."
      return
    }
    // uninstallPlugin() may emit dependentsWillBreak(), whose handler writes
    // dependencyNotice. Clearing selectedPluginId below fires
    // onSelectedPluginIdChanged -> clearDependencyNotice(), which would swallow
    // that notice. Preserve it across this specific reset so the user can still
    // see which dependents were cascaded to after returning to the list.
    const noticeAfterUninstall = root.dependencyNotice

    root.resetUninstallUi()
    root.selectedPluginId = "" // clears selection and returns to the list

    if (noticeAfterUninstall.length > 0)
      root.dependencyNotice = noticeAfterUninstall
  }

  onSelectedPluginIdChanged: {
    root.clearDependencyNotice()
    root.resetUninstallUi()
  }

  Connections {
    target: typeof pluginManager !== "undefined" ? pluginManager : null
    enabled: typeof pluginManager !== "undefined" && pluginManager !== null

    function onDependenciesRequired(id, deps) {
      root.dependencyNotice = "Auto-enabled dependencies for " + id + ": " + deps.join(", ")
    }

    function onDependentsWillBreak(id, dependents) {
      root.dependencyNotice = "Disabling " + id + " also disabled: " + dependents.join(", ")
    }
  }

  Column {
    id: contentColumn
    width: parent.width
    spacing: 20

    Rectangle {
      visible: root.dependencyNotice.length > 0
      width: parent.width
      height: noticeText.implicitHeight + 16
      radius: 6
      color: Theme.tint(Theme.accent, 0.1)
      border.width: 1
      border.color: Theme.tint(Theme.accent, 0.3)

      Accessible.role: Accessible.Notification
      Accessible.name: root.dependencyNotice

      Text {
        id: noticeText
        anchors {
          fill: parent
          margins: 8
        }
        text: root.dependencyNotice
        color: Theme.text
        font.pixelSize: 12
        wrapMode: Text.Wrap
        verticalAlignment: Text.AlignVCenter

        Accessible.role: Accessible.StaticText
        Accessible.name: root.dependencyNotice
      }
    }

    // ─── PER-PLUGIN SETTINGS VIEW ───
    Column {
      id: pluginDetailView
      visible: root.selectedPluginId.length > 0
      width: parent.width
      spacing: 16

      // Back navigation & Plugin Header
      Row {
        width: parent.width
        spacing: 12

        Rectangle {
          id: backBtn
          width: 32
          height: 32
          radius: 8
          color: backMouse.containsMouse ? Theme.surfaceHover : Theme.surfaceAlt
          border.width: backBtn.activeFocus ? 2 : 1
          border.color: backBtn.activeFocus ? Theme.accent : Theme.border

          activeFocusOnTab: true

          Accessible.role: Accessible.Button
          Accessible.name: "Back to plugin list"

          function goBack() {
            root.clearDependencyNotice()
            root.selectedPluginId = ""
          }

          Keys.onReturnPressed: function(event) {
            event.accepted = true
            backBtn.goBack()
          }
          Keys.onEnterPressed: function(event) {
            event.accepted = true
            backBtn.goBack()
          }
          Keys.onSpacePressed: function(event) {
            event.accepted = true
            backBtn.goBack()
          }

          Text {
            anchors.centerIn: parent
            text: "←"
            color: Theme.text
            font.pixelSize: 16
            font.bold: true
          }

          MouseArea {
            id: backMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: backBtn.goBack()
          }
        }

        Column {
          anchors.verticalCenter: parent.verticalCenter
          width: parent.width - 120
          spacing: 2

          Text {
            text: {
              if (root.selectedPluginId.length === 0 || !pluginManager) return ""
              const info = pluginManager.pluginInfo(root.selectedPluginId)
              return info.name ? info.name : root.selectedPluginId
            }
            color: Theme.text
            font.pixelSize: 16
            font.bold: true
          }

          Text {
            text: {
              if (root.selectedPluginId.length === 0 || !pluginManager) return ""
              const info = pluginManager.pluginInfo(root.selectedPluginId)
              return info.description ? info.description : ""
            }
            color: Theme.textMuted
            font.pixelSize: 12
          }
        }

        SettingSwitch {
          id: detailEnableSwitch
          anchors.verticalCenter: parent.verticalCenter
          accessibleName: "Enable " + (root.selectedPluginId || "plugin")
          checked: {
            if (!pluginManager || root.selectedPluginId.length === 0)
              return false
            // isEnabled() has no NOTIFY; the revision read keeps this Switch
            // in sync when dependencies are auto-enabled/disabled.
            const revision = pluginManager.stateRevision
            return pluginManager.isEnabled(root.selectedPluginId)
          }
          onToggled: function(value) {
            if (!pluginManager || root.selectedPluginId.length === 0) return
            root.clearDependencyNotice()
            // A toggle writes `checked` directly and removes the binding above.
            // Apply the user's intent, then install a fresh reactive binding so
            // later enable/disable changes still refresh this switch.
            const wantsEnabled = value
            let ok = true
            if (wantsEnabled)
              ok = pluginManager.enablePlugin(root.selectedPluginId)
            else
              ok = pluginManager.disablePlugin(root.selectedPluginId)
            // enablePlugin() returns false on an unsatisfiable dependency
            // chain without emitting dependenciesRequired. Build the notice
            // from missingDependencies() directly so it never lies.
            if (!ok && wantsEnabled) {
              const deps = pluginManager.missingDependencies(root.selectedPluginId)
              root.dependencyNotice = "Cannot enable — missing: " +
                                      (deps.length > 0 ? deps.join(", ") : "unknown")
            }
            checked = Qt.binding(function() {
              if (!pluginManager || root.selectedPluginId.length === 0)
                return false
              const revision = pluginManager.stateRevision
              return pluginManager.isEnabled(root.selectedPluginId)
            })
          }
        }
      }

      Rectangle {
        width: parent.width
        height: 1
        color: Theme.border
      }

      // Missing dependencies come from the live plugin graph, not from a
      // signal that may never be emitted. Shown only when there is a gap.
      Text {
        width: parent.width
        visible: root.detailMissingDepsText.length > 0
        text: "Missing dependencies: " + root.detailMissingDepsText
        color: Theme.danger
        font.pixelSize: 12
        wrapMode: Text.Wrap

        Accessible.role: Accessible.StaticText
        Accessible.name: text
      }

      // Non-modal, inline two-step uninstall confirmation. Only shown for
      // plugin copies the manager says can be uninstalled.
      Row {
        anchors.right: parent.right
        spacing: 10
        visible: root.detailCanUninstall

        Button {
          id: uninstallBtn
          visible: !root.confirmingUninstall
          text: "Uninstall"
          height: 32
          activeFocusOnTab: true

          Accessible.role: Accessible.Button
          Accessible.name: "Uninstall " + (root.selectedPluginId || "plugin")

          background: Rectangle {
            radius: 8
            color: uninstallBtn.down ? Qt.darker(Theme.danger, 1.2)
                  : (uninstallBtn.hovered ? Qt.darker(Theme.danger, 1.1) : Theme.danger)
          }
          contentItem: Text {
            text: uninstallBtn.text
            color: Theme.accentText
            font.pixelSize: 13
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
          }
          onClicked: root.requestUninstall()
        }

        Button {
          id: cancelUninstallBtn
          visible: root.confirmingUninstall
          text: "Cancel"
          height: 32
          activeFocusOnTab: true

          Accessible.role: Accessible.Button
          Accessible.name: "Cancel uninstall"

          background: Rectangle {
            radius: 8
            color: cancelUninstallBtn.down ? Theme.surfaceHover
                  : (cancelUninstallBtn.hovered ? Theme.surfaceAlt : "transparent")
            border.width: 1
            border.color: Theme.border
          }
          contentItem: Text {
            text: cancelUninstallBtn.text
            color: Theme.text
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
          }
          onClicked: root.cancelUninstall()
        }

        Button {
          id: confirmUninstallBtn
          visible: root.confirmingUninstall
          text: "Confirm uninstall"
          height: 32
          activeFocusOnTab: true

          Accessible.role: Accessible.Button
          Accessible.name: "Confirm uninstall of " + (root.selectedPluginId || "plugin")

          background: Rectangle {
            radius: 8
            color: confirmUninstallBtn.down ? Qt.darker(Theme.danger, 1.2)
                  : (confirmUninstallBtn.hovered ? Qt.darker(Theme.danger, 1.1) : Theme.danger)
          }
          contentItem: Text {
            text: confirmUninstallBtn.text
            color: Theme.accentText
            font.pixelSize: 13
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
          }
          onClicked: root.confirmUninstall()
        }
      }

      Text {
        width: parent.width
        visible: root.uninstallError.length > 0
        text: root.uninstallError
        color: Theme.danger
        font.pixelSize: 12
        wrapMode: Text.Wrap

        Accessible.role: Accessible.Notification
        Accessible.name: text
      }

      // Settings QML Component Loader
      Loader {
        id: settingsLoader
        width: parent.width
        source: root.selectedPluginId.length > 0 && pluginManager
                ? pluginManager.settingsComponent(root.selectedPluginId) : ""

        property var pluginContext: ({
          pluginId: root.selectedPluginId,
          getSetting: function(key) {
            return pluginManager ? pluginManager.pluginSetting(root.selectedPluginId, key) : undefined
          },
          setSetting: function(key, val) {
            if (pluginManager) pluginManager.setPluginSetting(root.selectedPluginId, key, val)
          },
          theme: Theme
        })
      }

      // Declarative settings from the manifest (no hand-written Settings QML).
      Loader {
        id: declarativeSettingsLoader
        width: parent.width
        visible: !settingsLoader.source || settingsLoader.source.length === 0
        source: visible && pluginManager
                ? Qt.resolvedUrl("DeclarativePluginSettings.qml") : ""

        property string pluginId: root.selectedPluginId
        property var fields: {
          if (!pluginManager || root.selectedPluginId.length === 0) return []
          const settings = pluginManager.declarativeSettings(root.selectedPluginId)
          return settings && settings.fields ? settings.fields : []
        }
        property var defaults: {
          if (!pluginManager || root.selectedPluginId.length === 0) return ({})
          const d = pluginManager.pluginSettings(root.selectedPluginId)
          return d && typeof d === "object" ? d : ({})
        }
      }
    }

    // ─── PLUGINS LIST VIEW ───
    Column {
      id: pluginListView
      visible: root.selectedPluginId.length === 0
      width: parent.width
      spacing: 20

      // Search Bar
      Rectangle {
        width: parent.width
        height: 38
        radius: 8
        color: Theme.surfaceAlt
        border.width: 1
        border.color: searchInput.activeFocus ? Theme.accent : Theme.border

        TextInput {
          id: searchInput
          anchors {
            fill: parent
            leftMargin: 12
            rightMargin: 12
          }
          verticalAlignment: TextInput.AlignVCenter
          color: Theme.text
          selectionColor: Theme.accent
          selectedTextColor: Theme.accentText
          font.pixelSize: 13
          clip: true
          activeFocusOnTab: true
          Accessible.role: Accessible.EditableText
          Accessible.name: "Search plugins"
          onTextChanged: {
            root.clearDependencyNotice()
            root.searchText = text.trim().toLowerCase()
          }

          Text {
            anchors.fill: parent
            verticalAlignment: Text.AlignVCenter
            visible: !searchInput.text.length
            text: "Search plugins..."
            color: Theme.textFaint
            font.pixelSize: 13
          }
        }
      }

      Text {
        width: parent.width
        text: "Colour themes are managed under Appearance → Colour scheme."
        color: Theme.textFaint
        font.pixelSize: 11
        wrapMode: Text.Wrap
      }

      // Plugins Section
      Column {
        width: parent.width
        spacing: 12

        Repeater {
          model: pluginManager ? pluginManager.pluginsModel : null

          delegate: Rectangle {
            id: pluginCard
            required property string id
            required property string name
            required property string description
            required property string version
            required property string category
            required property bool bundled
            required property bool hasSettings

            // The model's "enabled" role would shadow QQuickItem::enabled on
            // this Rectangle. Read the state through the manager instead, like
            // the theme cards in AppearanceSettings do. Reading stateRevision
            // keeps the value reactive even though isEnabled() has no NOTIFY.
            readonly property bool pluginEnabled: {
              if (typeof pluginManager === "undefined" || !pluginManager)
                return false
              const revision = pluginManager.stateRevision
              return pluginManager.isEnabled(pluginCard.id)
            }

            Accessible.role: Accessible.ListItem
            Accessible.name: pluginCard.name + ", version " + pluginCard.version +
                             (pluginCard.bundled ? ", bundled" : "") +
                             (pluginCard.pluginEnabled ? ", enabled" : ", disabled")

            // Theme plugins live exclusively in Appearance → Colour scheme,
            // where they are presented as selectable colour schemes. Hiding
            // them here avoids a second, confusing management surface.
            readonly property bool isTheme: category === "theme"

            // Filter match
            readonly property bool matchesSearch: {
              if (root.searchText.length === 0) return true
              return name.toLowerCase().indexOf(root.searchText) !== -1 ||
                     description.toLowerCase().indexOf(root.searchText) !== -1 ||
                     category.toLowerCase().indexOf(root.searchText) !== -1
            }

            visible: matchesSearch && !pluginCard.isTheme
            width: parent.width
            height: visible ? 68 : 0
            radius: 8
            color: Theme.surfaceAlt
            border.width: 1
            border.color: pluginCard.pluginEnabled ? Theme.tint(Theme.accent, 0.4) : Theme.border

            Row {
              anchors {
                fill: parent
                margins: 12
              }
              spacing: 12

              Column {
                anchors.verticalCenter: parent.verticalCenter
                // Reserve room for the always-visible Manage button, the
                // optional gear button and the enable Switch.
                width: parent.width - 170
                spacing: 3

                Row {
                  spacing: 8
                  Text {
                    text: pluginCard.name
                    color: Theme.text
                    font.pixelSize: 14
                    font.bold: true
                  }

                  Rectangle {
                    visible: pluginCard.bundled
                    width: 58
                    height: 18
                    radius: 4
                    color: Theme.tint(Theme.accent, 0.15)
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                      anchors.centerIn: parent
                      text: "Bundled"
                      color: Theme.accent
                      font.pixelSize: 10
                      font.bold: true
                    }
                  }

                  Text {
                    text: "v" + pluginCard.version
                    color: Theme.textFaint
                    font.pixelSize: 11
                    anchors.verticalCenter: parent.verticalCenter
                  }
                }

                Text {
                  width: parent.width
                  text: pluginCard.description
                  color: Theme.textMuted
                  font.pixelSize: 11
                  elide: Text.ElideRight
                }
              }

              // Settings button ⚙️
              Rectangle {
                id: gearButton

                visible: pluginCard.hasSettings
                width: 32
                height: 32
                radius: 6
                color: gearMouse.containsMouse ? Theme.surfaceHover : "transparent"
                anchors.verticalCenter: parent.verticalCenter
                border.width: gearButton.activeFocus ? 1 : 0
                border.color: Theme.accent

                activeFocusOnTab: true

                Accessible.role: Accessible.Button
                Accessible.name: "Settings for " + pluginCard.name

                function openSettings() {
                  root.clearDependencyNotice()
                  root.selectedPluginId = pluginCard.id
                }

                Keys.onReturnPressed: function(event) {
                  event.accepted = true
                  gearButton.openSettings()
                }
                Keys.onEnterPressed: function(event) {
                  event.accepted = true
                  gearButton.openSettings()
                }
                Keys.onSpacePressed: function(event) {
                  event.accepted = true
                  gearButton.openSettings()
                }

                Text {
                  anchors.centerIn: parent
                  text: "⚙"
                  color: Theme.textMuted
                  font.pixelSize: 16
                }

                MouseArea {
                  id: gearMouse
                  anchors.fill: parent
                  hoverEnabled: true
                  cursorShape: Qt.PointingHandCursor
                  onClicked: gearButton.openSettings()
                }
              }

              // Always-visible detail/manage affordance. The gear above is
              // settings-only and hidden for plugins without a settings
              // component, so without this button those plugins had no way to
              // reach the detail view (and therefore Uninstall).
              Rectangle {
                id: manageButton

                width: 32
                height: 32
                radius: 6
                color: manageMouse.containsMouse ? Theme.surfaceHover : "transparent"
                anchors.verticalCenter: parent.verticalCenter
                border.width: manageButton.activeFocus ? 1 : 0
                border.color: Theme.accent

                activeFocusOnTab: true

                Accessible.role: Accessible.Button
                Accessible.name: "Manage " + pluginCard.name
                Accessible.description: "Open plugin details and uninstall options"

                function openDetail() {
                  root.clearDependencyNotice()
                  root.selectedPluginId = pluginCard.id
                }

                Keys.onReturnPressed: function(event) {
                  event.accepted = true
                  manageButton.openDetail()
                }
                Keys.onEnterPressed: function(event) {
                  event.accepted = true
                  manageButton.openDetail()
                }
                Keys.onSpacePressed: function(event) {
                  event.accepted = true
                  manageButton.openDetail()
                }

                Text {
                  anchors.centerIn: parent
                  text: "ⓘ"
                  color: Theme.textMuted
                  font.pixelSize: 16
                }

                MouseArea {
                  id: manageMouse
                  anchors.fill: parent
                  hoverEnabled: true
                  cursorShape: Qt.PointingHandCursor
                  onClicked: manageButton.openDetail()
                }
              }

              // Themed toggle, matching the switches in Developer settings.
              SettingSwitch {
                id: enableSwitch
                anchors.verticalCenter: parent.verticalCenter
                accessibleName: "Enable " + pluginCard.name
                // stateRevision is read inside pluginEnabled, so the switch
                // refreshes after enable/disable and dependency cascades.
                checked: pluginCard.pluginEnabled
                onToggled: function(value) {
                  if (!pluginManager) return
                  root.clearDependencyNotice()
                  // The toggle removed the model binding; restore it after the
                  // C++ call so later model updates stay reactive.
                  const wantsEnabled = value
                  let ok = true
                  if (wantsEnabled)
                    ok = pluginManager.enablePlugin(pluginCard.id)
                  else
                    ok = pluginManager.disablePlugin(pluginCard.id)
                  if (!ok && wantsEnabled) {
                    const deps = pluginManager.missingDependencies(pluginCard.id)
                    root.dependencyNotice = "Cannot enable — missing: " +
                                            (deps.length > 0 ? deps.join(", ") : "unknown")
                  }
                  checked = Qt.binding(function() { return pluginCard.pluginEnabled })
                }
              }
            }
          }
        }
      }
    }
  }
}
