pragma Singleton

import QtQuick
import QtCore

// Persisted preferences that are independent of plugins and themes.
//
// Values are stored with QtCore's Settings so they survive restarts.
QtObject {
  id: root

  property Settings settings: Settings {
    id: store

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

  property alias restoreLastSession: store.restoreLastSession
  property alias lastDocumentPath: store.lastDocumentPath

  property alias expandExplorerOnLaunch: store.expandExplorerOnLaunch
  property alias columnCount: store.columnCount
  property alias settingsActiveSection: store.settingsActiveSection

  property alias fontFamily: store.fontFamily
  property alias lineWidth: store.lineWidth

  property alias showBlockDebugBorders: store.showBlockDebugBorders
  property alias showBlockTypeBadge: store.showBlockTypeBadge
  property alias showDragDropBounds: store.showDragDropBounds
  property alias showFpsOverlay: store.showFpsOverlay
  property alias fpsOverlayPosition: store.fpsOverlayPosition
}
