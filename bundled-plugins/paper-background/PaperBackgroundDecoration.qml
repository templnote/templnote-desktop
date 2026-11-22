import QtQuick
import Templnote

// The one and only renderer for the "paper-background" slot. It composites the
// substrate (theme solid, gradient or custom wallpaper), the fibre grain and
// the rounded-corner mask in a single shader pass. Doing it in one pass keeps
// the texture visible over the substrate and avoids 8-bit gradient banding
// from stacking a translucent QML layer on top.
Item {
  id: root

  property var pluginContext: null
  readonly property string pluginId: pluginContext && pluginContext.pluginId
                                     ? pluginContext.pluginId : "paper-background"

  // pluginSetting() is a plain invokable with no NOTIFY signal. Reading the
  // manager's revision properties here gives every setting binding below a
  // real dependency, so edits in the Settings panel re-render the substrate
  // immediately. Reading them through the Loader's pluginContext would not
  // work: a Loader only forwards properties to the item it creates, not later
  // changes to those properties.
  readonly property int revision: typeof pluginManager !== "undefined" && pluginManager
                                  ? pluginManager.stateRevision + pluginManager.settingsRevision
                                  : 0

  function setting(key, fallback) {
    const currentRevision = root.revision
    if (typeof pluginManager !== "undefined" && pluginManager) {
      const id = pluginContext && pluginContext.pluginId
                 ? pluginContext.pluginId : "paper-background"
      const value = pluginManager.pluginSetting(id, key)
      if (value !== undefined && value !== null)
        return value
    }
    return fallback
  }

  readonly property real grainIntensity: {
    const rev = root.revision
    return Number(setting("paperGrain", 0.035))
  }
  readonly property real radius: pluginContext && pluginContext.radius !== undefined
                                 ? pluginContext.radius : 0
  // Derive the substrate from the same setting the editor uses for the sheet
  // extent, so a theme switch or a full-workspace toggle is reflected without
  // needing the Loader to forward a stale baseColor.
  readonly property color baseColor: {
    const rev = root.revision
    return Boolean(setting("paperOverWorkspace", true))
           ? Theme.background : Theme.surface
  }
  readonly property color grainColor: Theme.isDark
                                      ? Theme.tint(Theme.text, 0.08)
                                      : Theme.tint(Theme.text, 0.05)

  readonly property string substrateMode: {
    const rev = root.revision
    return setting("paperBgMode", "Solid").toString()
  }
  readonly property string wallpaperPath: {
    const rev = root.revision
    return setting("wallpaperPath", "").toString()
  }
  readonly property real paperDim: {
    const rev = root.revision
    return Number(setting("paperDim", 0.40))
  }

  readonly property color gradientColor1: {
    const rev = root.revision
    return setting("gradientColor1", "#fdf6e3")
  }
  readonly property color gradientColor2: {
    const rev = root.revision
    return setting("gradientColor2", "#e0d5be")
  }
  readonly property real gradientOpacity: {
    const rev = root.revision
    return Number(setting("gradientOpacity", 0.35))
  }
  readonly property real gradientAngle: {
    const rev = root.revision
    return Number(setting("gradientAngle", 135))
  }

  // 0 = solid theme colour, 1 = gradient, 2 = wallpaper image.
  readonly property int bgMode: {
    if (root.substrateMode === "Wallpaper" && root.wallpaperPath.length > 0)
      return 2
    if (root.substrateMode === "Gradient")
      return 1
    return 0
  }

  // Blend the gradient stops toward the theme background by the gradient's
  // own opacity; the shader then applies the shared text-contrast dimming.
  function substrateColor(color) {
    const amount = Math.max(0, Math.min(1, root.gradientOpacity))
    return Qt.rgba(
      root.baseColor.r + (color.r - root.baseColor.r) * amount,
      root.baseColor.g + (color.g - root.baseColor.g) * amount,
      root.baseColor.b + (color.b - root.baseColor.b) * amount,
      1.0)
  }

  anchors.fill: parent

  // Off-screen source for the optional wallpaper sampler. Kept alive so
  // switching between modes never needs a new scene-graph texture.
  Image {
    id: wallpaperImg
    anchors.fill: parent
    visible: false
    source: root.wallpaperPath
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
    live: true
  }

  ShaderEffect {
    anchors.fill: parent

    property color paperColor: root.baseColor
    property color grainColor: root.grainColor
    property color gradientColor1: root.substrateColor(root.gradientColor1)
    property color gradientColor2: root.substrateColor(root.gradientColor2)
    property vector2d resolution: Qt.vector2d(Math.max(1, width), Math.max(1, height))
    property real grainIntensity: root.grainIntensity
    property real vignetteIntensity: 0.03
    property real shadowIntensity: 0.05
    property real cornerRadius: root.radius
    property int bgMode: root.bgMode
    property real wallpaperDim: root.bgMode === 0 ? 0 : Math.max(0, Math.min(0.95, root.paperDim))
    property real gradientAngle: root.gradientAngle
    property var wallpaperSource: wallpaperSourceItem

    fragmentShader: "qrc:/qt/qml/Templnote/qml/shaders/paper.frag.qsb"
  }
}
