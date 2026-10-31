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
  measured with the stb_truetype stack (see [Text stack](./rendering.md#text-stack-m4b)).
- **Flexbox** — `row`/`column` (and the `-reverse` variants) with `gap`,
  `flex-basis`/`flex-grow`/`flex-shrink`, `justify-content` and `align-items`
  (including `stretch` when the cross size is definite). `flex-wrap: wrap`
  breaks items into several lines, `align-content`
  (`stretch`/`flex-start`/`flex-end`/`center`/`space-between`/`space-around`)
  distributes the lines on the cross axis and `wrap-reverse` flips that axis.
  Item margins are part of the item's outer size on both axes, so a negative
  `margin-top` pulls an item up instead of being ignored. Whitespace-only text
  between items (source indentation) does not become a flex item.
- **Grid** — `display: grid` with `grid-template-columns` / `grid-template-rows`
  (fixed lengths, `auto`, `<n>fr`, `minmax(...)`, `repeat(...)`), `gap` /
  `row-gap` / `column-gap`, row-major auto-placement, simple `grid-column` /
  `grid-row` line placement, and `align/justify-items` plus
  `align/justify-self` (including `place-items` / `place-self`). Implicit rows
  and columns grow from the items; a single-cell grid with `place-items: center` centres a card in the whole overlay. Item measurement is a two-pass
  layout (intrinsic size, then final placement), so text fragments are cleared
  between passes.
- **Inline-level containers, auto margins and aspect ratio** — `display:
  inline-flex`/`inline-grid` lay their children out with the flex/grid algorithm
  but participate in the parent line as one atomic inline box (`vertical-align`
  shifts it by its length value). A block-level box with `margin-left: auto` /
  `margin-right: auto` absorbs the leftover inline space (both auto → centred);
  a flex item's auto main-axis margins absorb the line's free space before
  `justify-content`, which is what pushes a footer or button hint to the end.
  `aspect-ratio` derives a `height: auto` box from its used width (the
  width-from-height direction is not implemented). `border-radius` accepts a
  percentage and resolves against the smaller border-box side.
- **Vertical writing mode** — `writing-mode: vertical-rl|vertical-lr` lays text
  out one character per column step: CJK/kana/Hangul glyphs stay upright and
  other scripts are rotated 90° at paint time. A definite height starts a new
  column; a flex container's row/column axes swap with the writing mode. This is
  a text-focused implementation: nested blocks and mixed horizontal content in a
  vertical container still fall back to horizontal layout.
- **CSS value functions and custom properties** — `calc()` arithmetic (including
  nested `calc()` and `calc()` inside the other functions), `clamp(min, value,
  max)`, `min(...)` and `max(...)` keep their expression unresolved in the
  computed style and are evaluated at layout time against the element's font
  size and the viewport (`vw`/`vh`); `env(safe-area-inset-*)` resolves to zero in
  a desktop window. `var(--name[, fallback])` is substituted from inherited custom
  properties during the cascade. `inset` is the four-value
  `top/right/bottom/left` shorthand, `box-sizing: border-box` subtracts padding
  and borders from an explicit width/height, `visibility: hidden` skips painting
  and hit testing, and `pointer-events: none` makes a box transparent to input
  (its descendants can opt back in with `auto`).
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

## Transforms (M15)

`transform` and `transform-origin` are a **paint-time** effect: the box model
above is untouched (nothing reflows, no geometry is recomputed), and a second
pass after `xt_layout_run` gives every `LayoutBox` a `Matrix2D world` — the
transform of the whole ancestor chain — plus its `inverse`:

- **Only element boxes contribute.** A text box shares its parent element's
  `style` pointer, so letting both contribute would apply the element's list
  twice; the pass composes an element's own list, its ancestors', and nothing
  else.
- **`transform-origin` is resolved here**, not in the cascade: the computed style
  keeps the two (possibly `%`) `Length`s (default `50% 50%`) and layout resolves
  them against the *border box*, applying `T(origin) · M · T(-origin)`. That is
  why `computedStyle(el, "transform-origin")` still reports `50% 92%`. The
  keywords are positional as in CSS: `top` alone is `center top`, and the
  keyword pair may be given in either order (`bottom right` = `right bottom`).
- **Lengths resolve against the box**: `%` in `translate()` is a share of the
  border box's width/height, `em`/`rem` against the root font size (16px), and
  `vw`/`vh` against the viewport.
- **Composition order** follows CSS: the list applies right to left, so
  `rotate(90deg) translate(10px)` moves the box *down* the screen, not right.

Everything downstream follows the matrices:

| Consumer | Behaviour |
| --- | --- |
| Paint | each op carries the matrix (see [Rendering](./rendering.md#paint-and-display-list-m4am4b)); a clipped subtree clips to the bounding box of its padding box through the matrix |
| Hit testing | `hitTestBox` maps the point through each box's inverse before the containment test, so a rotated or scaled element is clickable where it is *drawn* |
| `getBoundingClientRect` | the axis-aligned bounding box of the **transformed** border box (`xt_layout_transformed_bounds`) |
| Layout geometry | unchanged — `layoutTree()` and `offsetWidth`/`offsetHeight` keep reporting the untransformed box |

Unsupported transform functions (`skew`, `perspective`, …) invalidate the whole
declaration, so a box is either correctly transformed or not transformed at all
rather than partially applied.

**Document** (`runtime/ext_gui/document.{h,cpp}`): owns the DOM tree, gathers
`<style>` text into one stylesheet, computes styles and layout for a viewport and
offers `querySelector`/`querySelectorAll`/`styleOf`/`boxOf`.

