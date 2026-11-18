import QtQuick

Item {
  id: root

  // Surface colors from theme
  property color baseColor: Theme.isDark ? Theme.surface : Theme.surface
  property color grainColor: Theme.isDark ? Theme.tint(Theme.text, 0.08) : Theme.tint(Theme.text, 0.05)
  property real grainIntensity: Preferences.paperGrain
  property real vignetteIntensity: 0.03
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

    // GPU-accelerated paper fiber shader with built-in sub-pixel SDF rounded corners
    ShaderEffect {
      id: paperShader
      anchors.fill: parent

      property color paperColor: root.baseColor
      property color grainColor: root.grainColor
      property vector2d resolution: Qt.vector2d(Math.max(1, width), Math.max(1, height))
      property real grainIntensity: root.grainIntensity
      property real vignetteIntensity: root.vignetteIntensity
      property real shadowIntensity: 0.05
      property real cornerRadius: root.radius

      fragmentShader: "shaders/paper.frag.qsb"
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
