pragma Singleton

import QtQuick
import QtCore

// Central theme controller.
//
// A colour scheme (Tokyo Night, Gruvbox, ...) provides both a light and a dark
// palette. The app-wide mode ("system", "light" or "dark") decides which one
// is used. The choice is persisted with QtCore's Settings type, so the theme
// survives restarts without any C++ changes.
QtObject {
  id: root

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

  readonly property var schemeKeys: [
    "tokyo-night",
    "gruvbox",
    "solarized-osaka",
    "github"
  ]

  readonly property var schemes: ({
    "tokyo-night": {
      name: {
        dark: "Tokyo Night",
        light: "Tokyo Night Day"
      },
      dark: {
        background: "#1a1b26",
        surface: "#16161e",
        surfaceAlt: "#24283b",
        surfaceHover: "#2a2f45",
        border: "#292e42",
        text: "#c0caf5",
        textMuted: "#9aa5ce",
        textFaint: "#737da8",
        accent: "#7aa2f7",
        accentText: "#1a1b26",
        danger: "#f7768e"
      },
      light: {
        background: "#e1e2e7",
        surface: "#d5d9e8",
        surfaceAlt: "#c9d0e5",
        surfaceHover: "#bfc8e2",
        border: "#b4bdd6",
        text: "#3760bf",
        textMuted: "#6172b0",
        textFaint: "#6b76a5",
        accent: "#2e7de9",
        accentText: "#e1e2e7",
        danger: "#f52a65"
      }
    },
    "gruvbox": {
      name: {
        dark: "Gruvbox Dark",
        light: "Gruvbox Light"
      },
      dark: {
        background: "#282828",
        surface: "#1d2021",
        surfaceAlt: "#32302f",
        surfaceHover: "#3c3836",
        border: "#504945",
        text: "#ebdbb2",
        textMuted: "#a89984",
        textFaint: "#8a7a68",
        accent: "#fabd2f",
        accentText: "#282828",
        danger: "#fb4934"
      },
      light: {
        background: "#fbf1c7",
        surface: "#f2e5bc",
        surfaceAlt: "#ebdbb2",
        surfaceHover: "#e0cfaa",
        border: "#d5c4a1",
        text: "#3c3836",
        textMuted: "#7c6f64",
        textFaint: "#8b7d6b",
        accent: "#b57614",
        accentText: "#fbf1c7",
        danger: "#9d0006"
      }
    },
    "solarized-osaka": {
      name: {
        dark: "Solarized Osaka",
        light: "Solarized Osaka Light"
      },
      dark: {
        background: "#002b36",
        surface: "#00212b",
        surfaceAlt: "#073642",
        surfaceHover: "#0a4350",
        border: "#586e75",
        text: "#93a1a1",
        textMuted: "#839496",
        textFaint: "#7a929a",
        accent: "#268bd2",
        accentText: "#fdf6e3",
        danger: "#dc322f"
      },
      light: {
        background: "#fdf6e3",
        surface: "#eee8d5",
        surfaceAlt: "#e6dfc8",
        surfaceHover: "#ded7c0",
        border: "#d3cbb7",
        text: "#586e75",
        textMuted: "#657b83",
        textFaint: "#708587",
        accent: "#268bd2",
        accentText: "#fdf6e3",
        danger: "#dc322f"
      }
    },
    "github": {
      name: {
        dark: "GitHub Dark",
        light: "GitHub Light"
      },
      dark: {
        background: "#0d1117",
        surface: "#161b22",
        surfaceAlt: "#21262d",
        surfaceHover: "#262c36",
        border: "#30363d",
        text: "#e6edf3",
        textMuted: "#8b949e",
        textFaint: "#828b95",
        accent: "#2f81f7",
        accentText: "#ffffff",
        danger: "#f85149"
      },
      light: {
        background: "#ffffff",
        surface: "#f6f8fa",
        surfaceAlt: "#eaeef2",
        surfaceHover: "#e6eaef",
        border: "#d0d7de",
        text: "#1f2328",
        textMuted: "#656d76",
        textFaint: "#6e7781",
        accent: "#0969da",
        accentText: "#ffffff",
        danger: "#cf222e"
      }
    }
  })

  readonly property var activeScheme: schemes[scheme] !== undefined ? schemes[scheme] : schemes["tokyo-night"]
  readonly property string schemeName: activeScheme.name[resolvedMode]
  readonly property var palette: activeScheme[resolvedMode]

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

  function tint(color, alpha) {
    return Qt.rgba(color.r, color.g, color.b, alpha)
  }

  function schemeFor(key) {
    return schemes[key] !== undefined ? schemes[key] : schemes["tokyo-night"]
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
