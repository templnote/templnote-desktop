pragma Singleton

import QtQuick
import QtCore

// Persisted preferences that are independent of the colour theme.
//
// Values are stored with QtCore's Settings so they survive restarts. The ones
// that have a wired behaviour (session restore, explorer layout) are read
// directly by the UI; the remaining placeholders still remember their state
// for future work instead of resetting on every launch.
QtObject {
  id: root

  property Settings settings: Settings {
    id: settings

    category: "preferences"

    // General
    property bool restoreLastSession: true
    property string lastDocumentPath: ""
    property bool checkForUpdates: true

    // Interface
    property bool expandExplorerOnLaunch: true
    property bool paperOverWorkspace: true
    property string paperPreset: "Journal" // "Office", "Journal", "Craft"
    property real paperGrain: 0.035
    property string paperBgMode: "Solid" // "Solid", "Gradient", "Wallpaper"
    property string paperWallpaperPath: ""
    property real paperWallpaperDim: 0.40
    property string paperGradientPreset: "Warm Parchment"
    property string paperGradientColor1: "#fdf6e3"
    property string paperGradientColor2: "#eee8d5"
    property real paperGradientAngle: 135.0

    // Editor
    property string fontFamily: "System"
    property string lineWidth: "Medium"
    property bool spellCheck: false
    property bool smartQuotes: false

    // Developer
    property bool showBlockDebugBorders: false
    property bool showBlockTypeBadge: false
    property bool showDragDropBounds: false
    property bool showFpsOverlay: false
    property string fpsOverlayPosition: "Top-Right"
  }

  property alias restoreLastSession: settings.restoreLastSession
  property alias lastDocumentPath: settings.lastDocumentPath
  property alias checkForUpdates: settings.checkForUpdates

  property alias expandExplorerOnLaunch: settings.expandExplorerOnLaunch
  property alias paperOverWorkspace: settings.paperOverWorkspace
  property alias paperPreset: settings.paperPreset
  property alias paperGrain: settings.paperGrain
  property alias paperBgMode: settings.paperBgMode
  property alias paperWallpaperPath: settings.paperWallpaperPath
  property alias paperWallpaperDim: settings.paperWallpaperDim
  property alias paperGradientPreset: settings.paperGradientPreset
  property alias paperGradientColor1: settings.paperGradientColor1
  property alias paperGradientColor2: settings.paperGradientColor2
  property alias paperGradientAngle: settings.paperGradientAngle

  property alias fontFamily: settings.fontFamily
  property alias lineWidth: settings.lineWidth
  property alias spellCheck: settings.spellCheck
  property alias smartQuotes: settings.smartQuotes

  property alias showBlockDebugBorders: settings.showBlockDebugBorders
  property alias showBlockTypeBadge: settings.showBlockTypeBadge
  property alias showDragDropBounds: settings.showDragDropBounds
  property alias showFpsOverlay: settings.showFpsOverlay
  property alias fpsOverlayPosition: settings.fpsOverlayPosition

  function applyPaperPreset(presetName) {
    paperPreset = presetName
    if (presetName === "Office") {
      paperGrain = 0.015
    } else if (presetName === "Journal") {
      paperGrain = 0.035
    } else if (presetName === "Craft") {
      paperGrain = 0.065
    }
  }

  readonly property var gradientPresets: ({
    "Warm Parchment": { c1: "#fdf6e3", c2: "#e0d5be", angle: 135 },
    "Sunset Glow":    { c1: "#ffecd2", c2: "#fcb69f", angle: 120 },
    "Northern Sky":   { c1: "#d4fc79", c2: "#96e6a1", angle: 135 },
    "Cyber Dusk":     { c1: "#2b1055", c2: "#7597de", angle: 160 },
    "Tokyo Neon":     { c1: "#1f1d36", c2: "#3f3351", angle: 135 },
    "Ethereal Mist":  { c1: "#e0c3fc", c2: "#8ec5fc", angle: 135 }
  })

  function applyGradientPreset(name) {
    if (gradientPresets[name]) {
      paperGradientPreset = name
      paperGradientColor1 = gradientPresets[name].c1
      paperGradientColor2 = gradientPresets[name].c2
      paperGradientAngle = gradientPresets[name].angle
    }
  }
}
