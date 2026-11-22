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

  // Appends every item in a visual subtree that can hold keyboard focus, in
  // declaration (tab) order. Invisible or disabled branches are skipped so a
  // focus ring never lands on a control the user cannot see or use.
  function collectFocusables(item, out) {
    if (!item || !item.visible || !item.enabled)
      return

    // Custom controls opt in with activeFocusOnTab. TextInput/TextEdit expose
    // cursorPosition, and Qt Quick Controls are focusable by default.
    if (item.activeFocusOnTab || item.activeFocus || item.cursorPosition !== undefined)
      out.push(item)

    const children = item.children || []
    for (let i = 0; i < children.length; ++i) {
      const child = children[i]
      if (child && typeof child.visible === "boolean")
        collectFocusables(child, out)
    }
  }

  function step(index, direction) {
    let next = index + direction
    if (next < 0)
      next = targets.length - 1
    else if (next >= targets.length)
      next = 0
    return next
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

    let next = current < 0
            ? (direction > 0 ? 0 : targets.length - 1)
            : step(current, direction)

    for (let i = 0; i < targets.length; ++i) {
      const target = targets[next]
      if (target && target.visible && target.enabled) {
        target.forceActiveFocus()
        return
      }
      next = step(next, direction)
    }
  }
}
