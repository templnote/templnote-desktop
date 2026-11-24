pragma Singleton

import QtQuick
import QtCore

// Central theme controller.
//
// A colour scheme provides both a light and a dark palette. The app-wide mode
// ("system", "light" or "dark") decides which one is used.
// Colour schemes are dynamically contributed by active theme plugins via
// pluginManager.activeThemes. A minimal fallback scheme is provided when no
// theme plugins are enabled.
QtObject {
  id: root

  // ─── Minimal fallback (no theme plugins) ───
  readonly property var fallbackScheme: ({
    name: {
      dark: "Default Dark",
      light: "Default Light"
    },
    dark: {
      background: "#1e1e1e",
      surface: "#252526",
      surfaceAlt: "#2d2d2d",
      surfaceHover: "#353535",
      border: "#3e3e3e",
      text: "#cccccc",
      textMuted: "#999999",
      textFaint: "#666666",
      accent: "#569cd6",
      accentText: "#ffffff",
      danger: "#f44747"
    },
    light: {
      background: "#ffffff",
      surface: "#f3f3f3",
      surfaceAlt: "#e8e8e8",
      surfaceHover: "#e0e0e0",
      border: "#d4d4d4",
      text: "#1e1e1e",
      textMuted: "#616161",
      textFaint: "#767676",
      accent: "#0066b8",
      accentText: "#ffffff",
      danger: "#cd3131"
    }
  })

  property Settings settings: Settings {
    id: settings

    category: "appearance"

    property string mode: "system"
    property string scheme: "tokyo-night"
  }

  property alias mode: settings.mode
  property alias scheme: settings.scheme

  readonly property bool systemDark: Application.styleHints.colorScheme === Qt.ColorScheme.Dark
  readonly property bool isDark: mode === "dark" || (mode === "system" && systemDark)
  readonly property string resolvedMode: isDark ? "dark" : "light"

  readonly property var schemes: {
    const map = {}
    if (typeof pluginManager !== "undefined" && pluginManager && pluginManager.activeThemes) {
      const themes = pluginManager.activeThemes
      for (let i = 0; i < themes.length; ++i) {
        const t = themes[i]
        if (t && t.schemeKey)
          map[t.schemeKey] = t
      }
    }
    return map
  }

  readonly property var schemeKeys: {
    if (typeof pluginManager !== "undefined" && pluginManager && pluginManager.activeThemes) {
      const themes = pluginManager.activeThemes
      const keys = []
      for (let i = 0; i < themes.length; ++i) {
        if (themes[i] && themes[i].schemeKey)
          keys.push(themes[i].schemeKey)
      }
      return keys
    }
    return []
  }

  readonly property var activeScheme: {
    if (scheme.length > 0 && schemes[scheme] !== undefined)
      return schemes[scheme]
    if (schemeKeys.length > 0 && schemes[schemeKeys[0]] !== undefined)
      return schemes[schemeKeys[0]]
    return fallbackScheme
  }

  readonly property string schemeName: activeScheme && activeScheme.name && activeScheme.name[resolvedMode]
                                       ? activeScheme.name[resolvedMode]
                                       : (resolvedMode === "dark" ? "Default Dark" : "Default Light")
  readonly property var palette: activeScheme && activeScheme[resolvedMode]
                                 ? activeScheme[resolvedMode]
                                 : fallbackScheme[resolvedMode]

  readonly property color background: palette.background
  readonly property color surface: palette.surface
  readonly property color surfaceAlt: palette.surfaceAlt
  readonly property color surfaceHover: palette.surfaceHover
  readonly property color border: palette.border
  readonly property color text: palette.text
  readonly property color textMuted: palette.textMuted
  readonly property color textFaint: palette.textFaint
  readonly property color accent: palette.accent
  readonly property color accentText: palette.accentText
  readonly property color danger: palette.danger

  // ─── Shared control metrics ───
  // Used by the AI panel and available to any other QML surface so buttons,
  // cards, fields and labels keep the same rhythm across themes and sizes.
  readonly property int spaceXs: 4
  readonly property int spaceSm: 8
  readonly property int spaceMd: 12
  readonly property int spaceLg: 16
  readonly property int radiusSm: 6
  readonly property int radiusMd: 8
  readonly property int radiusLg: 10
  readonly property int controlHeight: 28
  readonly property int buttonHeight: 32
  readonly property int rowHeight: 44
  readonly property int fontSize: 12
  readonly property int fontSizeSmall: 10
  readonly property int fontSizeTitle: 16

  function tint(color, alpha) {
    return Qt.rgba(color.r, color.g, color.b, alpha)
  }

  function schemeFor(key) {
    return schemes[key] !== undefined ? schemes[key] : activeScheme
  }

  function setMode(value) {
    if (value === "system" || value === "light" || value === "dark")
      mode = value
  }

  function setScheme(value) {
    if (schemes[value] !== undefined)
      scheme = value
  }
}
