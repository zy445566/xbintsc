# Layout

> Part of the [GUI extension](../gui.md); the engine overview, locked
> decisions and roadmap live there.

## Layout engine (M3)

`runtime/ext_gui/layout.{h,cpp}` turns the styled DOM into a `LayoutBox` tree
with absolute (viewport-relative) geometry:

- **Block flow** — block-level children stack vertically; `display: none`
  generates no box; `width: auto` fills the containing block, `height: auto`
  wraps the content. The box model (margin/padding/border) is resolved,
  including percentages against the containing block width. Adjoining vertical
  margins between siblings collapse to `max(positive) + min(negative)`; a
  parent's bottom margin does not collapse with its last child's (see the
  limitations below).
- **Inline flow** — consecutive inline-level children form an anonymous inline
  formatting context with greedy, word-based line breaking, `text-align` and
  `line-height`. Inline elements get the union of their descendants' geometry;
  `display: inline-block` is laid out atomically with a shrink-to-fit width.
  Each line shrinks to the band left free by any floats (`float: left|right`)
  that overlap it, so later lines wrap around them.
  Each text fragment remembers the run it covers, so paint can shape it. Text is
  measured with the HarfBuzz/FreeType stack (see [Text stack](./rendering.md#text-stack-m4b)).
- **Flexbox** — `row`/`column` (and the `-reverse` variants) with `gap`,
  `flex-basis`/`flex-grow`/`flex-shrink`, `justify-content` and `align-items`
  (including `stretch` when the cross size is definite). `flex-wrap: wrap`
  breaks items into several lines, `align-content`
  (`stretch`/`flex-start`/`flex-end`/`center`/`space-between`/`space-around`)
  distributes the lines on the cross axis and `wrap-reverse` flips that axis.
  Item margins are part of the item's outer size on both axes, so a negative
  `margin-top` pulls an item up instead of being ignored. Whitespace-only text
  between items (source indentation) does not become a flex item.
- **Positioning** — `position: relative` offsets a box (and its subtree) by
  `top`/`right`/`bottom`/`left` without changing the space it reserved;
  `position: absolute` (and `fixed`, which resolves against the viewport) takes
  the box out of flow and places it against the padding box of the nearest
  positioned ancestor — the viewport when there is none. An `auto` inset keeps
  the static position on that axis, an `auto` width is shrink-to-fit unless both
  `left` and `right` are set (then it fills the space between them), and
  percentages resolve against the containing block. Stacking is approximated by
  sorting each parent's children by `z-index` (negative → `auto` → positive)
  before painting; this is a single level, not a full stacking context, so a
  child cannot escape its parent's level.
  Note the consequence of that shrink-to-fit rule: an absolutely positioned box
  with `width: auto` and no explicit size gets **no** available width for its own
  inline content (its preferred width comes from its text or its non-absolute
  children only), so inline runs inside it are laid out at zero available width
  and each one wraps onto its own line. Give such a box an explicit `width`, or
  place its children with `left`/`width` (as
  `examples/gui/pelican-bike` does for its sprite runs), instead of relying on
  line flow inside it.
- **Floats and clearance** — `float: left|right` takes a box out of flow,
  shrink-to-fits its width, places it against the current line and lets later
  inline lines wrap around it; `clear: left|right|both` pushes a following block
  below the relevant floats, and a block that contains only floats grows to
  enclose them.
- **`overflow` clipping** — a box with `overflow: hidden` (or `scroll`/`auto`)
  clips its subtree to its padding box, intersected with any ancestor clip; the
  renderer applies the result with a GPU scissor rectangle. There is no
  scrolling yet, so `scroll`/`auto` clip like `hidden`.

Not yet implemented: parent/child and empty-block margin collapsing, anonymous
flex items from non-whitespace text, full `z-index` stacking contexts, and
`float` affecting block children (only inline content wraps; a block must
`clear`).

**Document** (`runtime/ext_gui/document.{h,cpp}`): owns the DOM tree, gathers
`<style>` text into one stylesheet, computes styles and layout for a viewport and
offers `querySelector`/`querySelectorAll`/`styleOf`/`boxOf`.

