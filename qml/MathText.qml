pragma Singleton

import QtQml

// Lightweight LaTeX-to-rich-text converter for the subset used in notes.
// It intentionally avoids a browser dependency: complex documents can still
// keep the raw LaTeX in the block, while the common notation renders inline.
QtObject {
  function toHtml(input) {
    var s = String(input === undefined || input === null ? "" : input)

    // Display delimiters are not part of the expression.
    s = s.replace(/\$\$/g, "")

    // Escape before injecting markup so user text cannot inject HTML.
    s = s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")

    // Named symbols (longest first so \times is not eaten by \t).
    var symbols = {
      "alpha": "α", "beta": "β", "gamma": "γ", "delta": "δ", "epsilon": "ε",
      "zeta": "ζ", "eta": "η", "theta": "θ", "iota": "ι", "kappa": "κ",
      "lambda": "λ", "mu": "μ", "nu": "ν", "xi": "ξ", "pi": "π",
      "rho": "ρ", "sigma": "σ", "tau": "τ", "upsilon": "υ", "phi": "φ",
      "chi": "χ", "psi": "ψ", "omega": "ω",
      "Gamma": "Γ", "Delta": "Δ", "Theta": "Θ", "Lambda": "Λ", "Xi": "Ξ",
      "Pi": "Π", "Sigma": "Σ", "Phi": "Φ", "Psi": "Ψ", "Omega": "Ω",
      "times": "×", "cdot": "·", "div": "÷", "pm": "±", "mp": "∓",
      "le": "≤", "leq": "≤", "ge": "≥", "geq": "≥", "ne": "≠", "neq": "≠",
      "approx": "≈", "equiv": "≡", "propto": "∝", "infty": "∞",
      "sum": "∑", "prod": "∏", "int": "∫", "partial": "∂", "nabla": "∇",
      "rightarrow": "→", "to": "→", "leftarrow": "←", "Rightarrow": "⇒",
      "in": "∈", "notin": "∉", "subset": "⊂", "subseteq": "⊆",
      "cup": "∪", "cap": "∩", "emptyset": "∅", "forall": "∀", "exists": "∃",
      "angle": "∠", "perp": "⊥", "parallel": "∥", "degree": "°"
    }
    var names = Object.keys(symbols).sort(function(a, b) { return b.length - a.length })
    for (var i = 0; i < names.length; ++i) {
      var re = new RegExp("\\\\" + names[i] + "(?![a-zA-Z])", "g")
      s = s.replace(re, symbols[names[i]])
    }

    // Text spans keep their contents literal.
    s = s.replace(/\\text\{([^{}]*)\}/g, "$1")

    // Fractions, radicals and grouped scripts.
    for (var pass = 0; pass < 3; ++pass) {
      s = s.replace(/\\frac\{([^{}]*)\}\{([^{}]*)\}/g, "<span>($1)/($2)</span>")
      s = s.replace(/\\dfrac\{([^{}]*)\}\{([^{}]*)\}/g, "<span>($1)/($2)</span>")
      s = s.replace(/\\sqrt\{([^{}]*)\}/g, "√<span style=\"text-decoration:overline\">$1</span>")
      s = s.replace(/\^\{([^{}]*)\}/g, "<sup>$1</sup>")
      s = s.replace(/_\{([^{}]*)\}/g, "<sub>$1</sub>")
    }

    // Single-character scripts.
    s = s.replace(/\^([A-Za-z0-9+\-=<>])/g, "<sup>$1</sup>")
    s = s.replace(/_([A-Za-z0-9+\-=<>])/g, "<sub>$1</sub>")

    // Formatting commands and ignorable spacing directives.
    s = s.replace(/\\(?:left|right|displaystyle|textstyle|quad|,|;|!)/g, " ")
    s = s.replace(/\\mathbb\{([^{}]*)\}/g, "<b>$1</b>")
    s = s.replace(/\\mathrm\{([^{}]*)\}/g, "$1")
    s = s.replace(/\\mathbf\{([^{}]*)\}/g, "<b>$1</b>")
    s = s.replace(/\\operatorname\{([^{}]*)\}/g, "$1")
    s = s.replace(/\\(?:[a-zA-Z]+)/g, "")

    // Keep braces tidy in the rendered text.
    s = s.replace(/[{}]/g, "")

    return s.trim()
  }

  function isMath(source) {
    var s = String(source === undefined || source === null ? "" : source)
    return /\$[^$\n]+\$|\$\$[^$]+\$\$/.test(s)
  }
}
