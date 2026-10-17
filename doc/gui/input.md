# Input and events (M5)

> Part of the [GUI extension](../gui.md); the engine overview, locked
> decisions and roadmap live there. The public event API is in
> [api.md](./api.md).

`LayoutTree::hitTest` returns the deepest box containing a point (probing later
siblings first so the topmost element wins), and `xt_dom_describe` turns the
element into the `div#id.class` descriptor carried by event payloads. A
transformed box maps the point through its inverse matrix first, so a rotated,
scaled or translated element is hit exactly where it is drawn — and `:hover`
follows the artwork, not the box flow left behind.

- SDL pointer/wheel/key events are routed to the owning window, hit tested, and
delivered to the handlers registered with `on`. `mousedown` also moves focus.
- `:hover` matches the hovered element **and its ancestors** (so hovering a child
  lights up its parents); `:focus` matches the focused element. When either
  changes, `XtDocument::setHover`/`setFocus` recompute styles and layout and mark
  the window's geometry dirty, so the change is painted on the next frame.
- `win.hitTest(x, y)` and `win.sendEvent(type, options)` expose hit testing and
  synthetic input so the whole path is testable headlessly (the e2e suite drives
  clicks, wheels and keys without a real mouse).

Still to do for full input: text selection, drag, IME and clipboard.

