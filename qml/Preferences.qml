pragma Singleton

import QtQuick
import QtCore

// Persisted preferences that are independent of the colour theme.
//
// Values are stored with QtCore's Settings so they survive restarts. The ones
// that have a wired behaviour (session restore, explorer layout, motion) are
// read directly by the UI; the remaining placeholders still remember their
// state for future work instead of resetting on every launch.
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
    property bool showNavigationRail: true
    property bool expandExplorerOnLaunch: true
    property string density: "Comfortable"
    property bool reduceMotion: false
    property bool animateExplorer: true

    // Editor
    property string fontFamily: "System"
    property string lineWidth: "Medium"
    property bool markdownShortcuts: true
    property bool spellCheck: false
    property bool smartQuotes: false

    // Developer
    property bool showBlockDebugBorders: true
    property bool showBlockTypeBadge: false
    property bool showDragDropBounds: false
    property bool showFpsOverlay: false
    property string fpsOverlayPosition: "Top-Right"
  }

  property alias restoreLastSession: settings.restoreLastSession
  property alias lastDocumentPath: settings.lastDocumentPath
  property alias checkForUpdates: settings.checkForUpdates

  property alias showNavigationRail: settings.showNavigationRail
  property alias expandExplorerOnLaunch: settings.expandExplorerOnLaunch
  property alias density: settings.density
  property alias reduceMotion: settings.reduceMotion
  property alias animateExplorer: settings.animateExplorer

  property alias fontFamily: settings.fontFamily
  property alias lineWidth: settings.lineWidth
  property alias markdownShortcuts: settings.markdownShortcuts
  property alias spellCheck: settings.spellCheck
  property alias smartQuotes: settings.smartQuotes

  property alias showBlockDebugBorders: settings.showBlockDebugBorders
  property alias showBlockTypeBadge: settings.showBlockTypeBadge
  property alias showDragDropBounds: settings.showDragDropBounds
  property alias showFpsOverlay: settings.showFpsOverlay
  property alias fpsOverlayPosition: settings.fpsOverlayPosition
}
