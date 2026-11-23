import QtQuick

// Home surface shown when no document is open. It stays intentionally empty
// of document content, but plugins can decorate it through the "home-screen"
// slot (widgets such as weather, clock, daily verse, etc.).
Rectangle {
  id: root

  color: "black"

  // Plugin decorations are loaded on top of the plain background. Reading the
  // revision properties makes this binding react to enable/disable and to
  // settings changes without a restart.
  readonly property int pluginRevision: {
    if (typeof pluginManager === "undefined" || !pluginManager)
      return 0
    return pluginManager.stateRevision + pluginManager.settingsRevision
  }

  Repeater {
    model: {
      const revision = root.pluginRevision
      const decorations =
          typeof pluginManager !== "undefined" && pluginManager
          ? (pluginManager.decorationsForSlot("home-screen") || []) : []
      const result = []
      for (let i = 0; i < decorations.length; ++i) {
        const dec = decorations[i]
        result.push({
          pluginId: dec.pluginId,
          component: dec.component,
          zOrder: dec.zOrder,
          revision: revision
        })
      }
      return result
    }

    delegate: Loader {
      required property var modelData
      z: modelData.zOrder || 0
      anchors.fill: parent
      source: modelData.component
      onStatusChanged: {
        if (status === Loader.Error)
          console.warn("HomeScreen: failed to load plugin component",
                       modelData.pluginId, modelData.component)
      }
    }
  }
}
