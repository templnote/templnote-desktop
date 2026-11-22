import QtQuick
import QtQuick.Window

// Invisible helper that keeps keyboard focus inside an overlay or dialog.
//
// The host surface installs its own Keys.onTabPressed/onBacktabPressed handlers
// and calls cycle() from them. Key events bubble up from the focused child to
// the surface, so accepting Tab there prevents Qt from moving focus to a
// control behind the overlay.
Item {
  id: guard

  // Focusable items in tab order, innermost controls first.
  property var targets: []

  function setTargets(items) {
    targets = items || []
  }

  function cycle(direction) {
    if (!targets || targets.length === 0)
      return

    const active = Window.window ? Window.window.activeFocusItem : null
    let current = -1

    for (let i = 0; i < targets.length; ++i) {
      const target = targets[i]
      if (!target || !active)
        continue

      if (target === active ||
          (typeof target.isAncestorOf === "function" && target.isAncestorOf(active))) {
        current = i
        break
      }
    }

    let next
    if (current < 0) {
      next = direction > 0 ? 0 : targets.length - 1
    } else {
      next = current + direction
      if (next < 0)
        next = targets.length - 1
      else if (next >= targets.length)
        next = 0
    }

    targets[next].forceActiveFocus()
  }
}
