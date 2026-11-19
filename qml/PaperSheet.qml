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

  // Mode: 0 = Solid, 1 = Gradient, 2 = Wallpaper
  readonly property int bgMode: Preferences.paperBgMode === "Wallpaper" && Preferences.paperWallpaperPath.length > 0
                                  ? 2
                                  : (Preferences.paperBgMode === "Gradient" ? 1 : 0)

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

    // Wallpaper source image (cropped and scaled smoothly)
    Image {
      id: wallpaperImg
      anchors.fill: parent
      visible: false
      source: Preferences.paperWallpaperPath
      fillMode: Image.PreserveAspectCrop
      asynchronous: true
      cache: true
      mipmap: true
    }

    ShaderEffectSource {
      id: wallpaperSourceItem
      sourceItem: wallpaperImg
      hideSource: true
      visible: false
      // The wallpaper is a static image: updating the texture every frame
      // would re-render it needlessly. The image loads asynchronously, so
      // request one refresh once it is ready.
      live: false
    }

    Connections {
      target: wallpaperImg

      function onStatusChanged() {
        if (wallpaperImg.status === Image.Ready)
          wallpaperSourceItem.scheduleUpdate()
      }
    }

    // GPU-accelerated paper fiber shader with built-in sub-pixel SDF rounded corners
    ShaderEffect {
      id: paperShader
      anchors.fill: parent

      property color paperColor: root.baseColor
      property color grainColor: root.grainColor
      property color gradientColor1: Preferences.paperGradientColor1
      property color gradientColor2: Preferences.paperGradientColor2
      property vector2d resolution: Qt.vector2d(Math.max(1, width), Math.max(1, height))
      property vector2d mousePos: Qt.vector2d(root.mouseX, root.mouseY)
      property real grainIntensity: root.grainIntensity
      property real vignetteIntensity: root.vignetteIntensity
      property real shadowIntensity: 0.05
      property real cornerRadius: root.radius
      property int bgMode: root.bgMode
      property real wallpaperDim: Preferences.paperWallpaperDim
      property real gradientAngle: Preferences.paperGradientAngle

      // Texture sampler for custom wallpaper
      property var wallpaperSource: wallpaperSourceItem

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

