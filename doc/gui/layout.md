# Layout

> Part of the [GUI extension](../gui.md); the engine overview, locked
> decisions and roadmap live there.

## Layout engine (M3)

`runtime/ext_gui/layout.{h,cpp}` turns the styled DOM into a `LayoutBox` tree
with absolute (viewport-relative) geometry:

- **Block flow** — block-level children stack vertically (no margin collapsing
  yet); `display: none` generates no box; `width: auto` fills the containing
  block, `height: auto` wraps the content. The box model (margin/padding/border)
  is resolved, including percentages against the containing block width.
- **Inline flow** — consecutive inline-level children form an anonymous inline
  formatting context with greedy, word-based line breaking, `text-align` and
  `line-height`. Inline elements get the union of their descendants' geometry;
  `display: inline-block` is laid out atomically with a shrink-to-fit width.
  Each text fragment remembers the run it covers, so paint can shape it. Text is
  measured with the HarfBuzz/FreeType stack (see [Text stack](./rendering.md#text-stack-m4b)).
- **Flexbox** — single-line `row`/`column` (and the `-reverse` variants) with
  `gap`, `flex-basis`/`flex-grow`/`flex-shrink`, `justify-content` and
  `align-items` (including `stretch` when the cross size is definite). Item
  margins are part of the item's outer size on both axes, so a negative
  `margin-top` pulls an item up instead of being ignored.
- **Positioning** — `position: relative` offsets a box (and its subtree) by
  `top`/`right`/`bottom`/`left` without changing the space it reserved;
  `position: absolute` (and `fixed`, which resolves against the viewport) takes
  the box out of flow and places it against the padding box of the nearest
  positioned ancestor — the viewport when there is none. An `auto` inset keeps
  the static position on that axis, an `auto` width is shrink-to-fit unless both
  `left` and `right` are set (then it fills the space between them), and
  percentages resolve against the containing block. There is no `z-index`
  stacking yet: painting stays in document order.
  Note the consequence of that shrink-to-fit rule: an absolutely positioned box
  with `width: auto` and no explicit size gets **no** available width for its own
  inline content (its preferred width comes from its text or its non-absolute
  children only), so inline runs inside it are laid out at zero available width
  and each one wraps onto its own line. Give such a box an explicit `width`, or
  place its children with `left`/`width` (as
  `examples/gui/pelican-bike` does for its sprite runs), instead of relying on
  line flow inside it.

Not yet implemented: margin collapsing, multi-line flex wrapping, `z-index`
stacking, `overflow` clipping and floats.

**Document** (`runtime/ext_gui/document.{h,cpp}`): owns the DOM tree, gathers
`<style>` text into one stylesheet, computes styles and layout for a viewport and
offers `querySelector`/`querySelectorAll`/`styleOf`/`boxOf`.

