# Animation: frames and transitions

> Part of the [GUI extension](../gui.md); the engine overview, locked
> decisions and roadmap live there.

## Animation frames (M10)

`requestAnimationFrame` runs a callback once on the next frame; the callback
receives the frame timestamp (ms) and may mutate the DOM, which is restyled and
repainted in the same frame. Re-queue from inside the callback to animate:

```ts
const win = createWindow({ title: "anim", width: 320, height: 240 });
let n = 0;
const tick = (t: number) => {
  win.document.getElementById("label").textContent = String(n++);
  if (n < 120) win.requestAnimationFrame(tick); // id returned; cancelAnimationFrame(id) drops it
};
win.on("ready", () => win.requestAnimationFrame(tick));
win.loadHTML("<div id='label'>0</div>");
run();
```

## CSS transitions (M6b)

`XtDocument` runs CSS transitions between the *target* computed styles (the
cascade for the current `:hover`/`:focus` state) and the *displayed* styles used
for layout and paint. When a state change alters a transitioned property, a
running transition is recorded and re-applied every frame until it finishes;
the engine advances the clock with real frame deltas (`XtDocument::advance`),
and `win.advance(ms)` lets tests step it deterministically.

- Supported properties: `background-color`, `color`, `border-color`, `opacity`,
  `border-radius`, `transform`, `text-shadow`, `box-shadow`, every box offset
  and size length (`width`/`height`/`top`/`left`/`margin-*`/`padding-*`/…), the
  gap/flex-basis lengths and `filter` (matching filter lists interpolate their
  blur lengths, amounts and drop-shadow colours). `transition: all` covers the
  whole list. Lengths
  interpolate when their units line up (a mixed-unit change is CSS's discrete
  fallback); `opacity`, colours and `transform` interpolate normally.
- Both the `transition` shorthand and the `transition-property` / `-duration` /
  `-delay` / `-timing-function` longhands are parsed; time values accept `s` and
  `ms`; `calc()` is accepted inside the shorthand and longhands.
- Timing functions are `linear`, `ease`, `ease-in`, `ease-out`, `ease-in-out`,
  `cubic-bezier(x1, y1, x2, y2)`, `steps(n, start|end)` and their `jump-*`
  spellings. The engine solves the cubic Bézier numerically; keyword curves use
  the CSS control points.
- Retargeting mid-flight starts a new transition from the current interpolated
  value, so reversing a hover animates smoothly from wherever it was.
- `computedStyle()` reports the displayed (interpolated) value, so transitions
  are directly observable in tests.

`transform` lists interpolate **element-wise** (`animation_values.cpp`,
`xt_transform_interpolate`), which is what makes the common pair
`translateY(-4px) rotate(-0.35deg)` <-> `none` glide instead of jumping: the
missing side is filled with identity ops of the same kind. Two lists whose shapes
or units do not line up (`rotate(10deg)` vs `rotate(10rad)`, or a different number
of functions) are not interpolated at all — the transition snaps to the target,
which is CSS's discrete fallback rather than a wrong-looking sweep.

## CSS animations (`@keyframes`)

`@keyframes` rules are parsed into `StyleSheet::keyframes` (offsets from
`from`/`to`/percent selectors, sorted). `animation` and every `animation-*`
longhand are supported: name, duration, delay, timing function, iteration count
(including `infinite`), direction (`normal`/`reverse`/`alternate`/
`alternate-reverse`), fill mode (`none`/`forwards`/`backwards`/`both`) and
`paused`/`running`.

`XtDocument` starts one active animation per element/name when the computed
style first declares it, restarts it when the shorthand changes, and leaves it
alone across unrelated restyles. Each frame it resolves the local time to an
iteration progress, applies the timing function **per keyframe interval**, and
interpolates every animatable property. A property that a middle keyframe does
not mention keeps interpolating between the nearest keyframes that do declare
it (so a transform declared at 0% and 100% runs straight through a 50% step that
only changes `opacity`). Missing 0%/100% steps use the element's underlying
style. `forwards`/`backwards` hold the edge value outside the active time, and
`computedStyle()` reports the animated value.

Animations and transitions share `animation_values.cpp`, so both understand the
same property set, shadow lists and `transform` identity filling.

## CSS transforms (M15)

`transform` and `transform-origin` are supported as *paint-time* effects
(`translate`/`translate3d`/`translateX`/`translateY`,
`scale`/`scale3d`/`scaleX`/`scaleY`, `rotate`/`rotateZ`, `matrix`; the `3d`
spellings' `z` argument is parsed and dropped, since the engine is 2D;
`deg`/`rad`/`grad`/`turn`; `%`, `px`, `em`, `rem`, `vw`, `vh`). Layout
is not affected: geometry stays where flow put it, and only the painted result
moves, so a size, a scroll position or a sibling's position never depends on an
animation frame. See [Layout](./layout.md#transforms-m15) for the composition
rules and [Rendering](./rendering.md) for how it reaches the GPU.

Two ways to drive one:

```ts
// 1. Per frame — a pose from the frame clock. Nothing reflows: the string is
//    parsed into matrices and the display list picks them up in the same frame.
const tick = (t: number): void => {
  const s = t / 1000;
  art.style.setProperty(
    "transform",
    "translateY(" + (Math.sin(s * 2.4) * 5).toFixed(2) + "px) " +
      "rotate(" + (Math.sin(s * 1.15) * 1.1).toFixed(2) + "deg)",
  );
  win.requestAnimationFrame(tick);
};
```

```css
/* 2. From a state change — a transitioned transform, like any other property. */
#caption {
  transform-origin: 50% 100%;
  transition: transform 260ms ease-out;
}
#caption:hover { transform: translateY(-2px) scale(1.03); }
```

Both are exercised by `examples/gui/pelican-svg` (per-frame sway plus a
hover transition on the caption) and pinned by
`tests/e2e/gui-transform.test.ts`.

`calc()` can be used in any animated length (for example
`transition: width 300ms ease-out; width: calc(100% - 20px)`).

