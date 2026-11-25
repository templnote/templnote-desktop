pragma Singleton

import QtQuick
import QtCore

// Persisted preferences that are independent of plugins and themes.
//
// Values are stored with QtCore's Settings so they survive restarts.
QtObject {
  id: root

  property Settings settings: Settings {
    id: settings

    category: "preferences"

    // General
    property bool restoreLastSession: true
    property string lastDocumentPath: ""

    // Interface
    property bool expandExplorerOnLaunch: true
    property int columnCount: 2
    property string settingsActiveSection: "appearance"

    // Editor
    property string fontFamily: "System"
    property string lineWidth: "Wide"

    // Developer
    property bool showBlockDebugBorders: false
    property bool showBlockTypeBadge: false
    property bool showDragDropBounds: false
    property bool showFpsOverlay: false
    property string fpsOverlayPosition: "Top-Right"
  }

  property alias restoreLastSession: settings.restoreLastSession
  property alias lastDocumentPath: settings.lastDocumentPath

  property alias expandExplorerOnLaunch: settings.expandExplorerOnLaunch
  property alias columnCount: settings.columnCount
  property alias settingsActiveSection: settings.settingsActiveSection

  property alias fontFamily: settings.fontFamily
  property alias lineWidth: settings.lineWidth

  property alias showBlockDebugBorders: settings.showBlockDebugBorders
  property alias showBlockTypeBadge: settings.showBlockTypeBadge
  property alias showDragDropBounds: settings.showDragDropBounds
  property alias showFpsOverlay: settings.showFpsOverlay
  property alias fpsOverlayPosition: settings.fpsOverlayPosition
}
