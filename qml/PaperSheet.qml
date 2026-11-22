import QtQuick

Item {
  id: root

  // Surface colors from theme
  property color baseColor: Theme.surface
  property color grainColor: Theme.isDark ? Theme.tint(Theme.text, 0.08) : Theme.tint(Theme.text, 0.05)
  property real radius: 10
  property real elevation: 1.0
  property bool showShadow: true

  // Ambient Drop Shadow for paper elevation off the desk
  Rectangle {
    visible: root.showShadow
    anchors.fill: parent
    anchors.margins: -4 * root.elevation
    anchors.topMargin: 2 * root.elevation
    radius: root.radius + 4
    color: Qt.rgba(0, 0, 0, Theme.isDark ? 0.35 : 0.07)
    z: -2
    opacity: 0.7
  }

  Rectangle {
    visible: root.showShadow
    anchors.fill: parent
    anchors.margins: -1 * root.elevation
    anchors.topMargin: 4 * root.elevation
    radius: root.radius + 2
    color: Qt.rgba(0, 0, 0, Theme.isDark ? 0.25 : 0.04)
    z: -1
    opacity: 0.5
  }

  // Base background fill (fallback & clipping backing)
  Item {
    id: backing
    anchors.fill: parent
    clip: root.radius > 0

    // Fallback solid color (visible if no background decoration is loaded)
    Rectangle {
      anchors.fill: parent
      radius: root.radius
      color: root.baseColor
    }

    // Dynamic decoration layers from enabled plugins in slot "paper-background"
    Repeater {
      model: {
        if (typeof pluginManager === "undefined" || !pluginManager)
          return []

        // decorationsForSlot() is a plain invokable with no NOTIFY signal, so
        // reading the revision properties is what makes this binding react to
        // enable/disable and to per-plugin setting changes at runtime.
        const revision =
            pluginManager.stateRevision + pluginManager.settingsRevision
        const decorations =
            pluginManager.decorationsForSlot("paper-background") || []
        const result = []

        for (let i = 0; i < decorations.length; ++i) {
          const dec = decorations[i]
          result.push({
            pluginId: dec.pluginId,
            slot: dec.slot,
            component: dec.component,
            zOrder: dec.zOrder,
            revision: revision
          })
        }

        return result
      }

      delegate: Loader {
        required property var modelData
        anchors.fill: parent
        source: modelData.component
        z: modelData.zOrder

        // Identity/static geometry only. Setting values are read by the
        // decoration directly from pluginManager with a revision dependency:
        // a Loader does not forward later changes to properties on itself.
        property var pluginContext: ({
          pluginId: modelData.pluginId,
          getSetting: function(key) { return pluginManager.pluginSetting(modelData.pluginId, key) },
          setSetting: function(key, val) { pluginManager.setPluginSetting(modelData.pluginId, key, val) },
          theme: Theme,
          radius: root.radius,
          baseColor: root.baseColor
        })
      }
    }

    // Very subtle crisp sheet border (resembles cardstock trim)
    Rectangle {
      visible: root.radius > 0
      anchors.fill: parent
      radius: root.radius
      color: "transparent"
      border.width: 1
      border.color: Theme.isDark ? Theme.tint(Theme.border, 0.45) : Theme.tint(Theme.border, 0.65)
    }
  }
}
