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

- Supported properties: `background-color`, `color`, `border-color`,
  `border-radius` and `transform`; `transition: all` covers them. A
  layout-affecting property (width, position, …) is still not animated — that
  would reflow every frame — but `transform` is paint-time, so animating it costs
  one matrix per animated box per frame ([Layout](./layout.md#transforms-m15)).
- Both the `transition` shorthand and the `transition-property` / `-duration` /
  `-delay` / `-timing-function` longhands are parsed; time values accept `s` and
  `ms`; timing functions are `linear`, `ease`, `ease-in`, `ease-out` and
  `ease-in-out` (`ease` is a smoothstep approximation).
- Retargeting mid-flight starts a new transition from the current interpolated
  value, so reversing a hover animates smoothly from wherever it was.
- `computedStyle()` reports the displayed (interpolated) value, so transitions
  are directly observable in tests.

`transform` lists interpolate **element-wise** (`document.cpp`,
`xt_transform_interpolate`), which is what makes the common pair
`translateY(-4px) rotate(-0.35deg)` <-> `none` glide instead of jumping: the
missing side is filled with identity ops of the same kind. Two lists whose shapes
or units do not line up (`rotate(10deg)` vs `rotate(10rad)`, or a different number
of functions) are not interpolated at all — the transition snaps to the target,
which is CSS's discrete fallback rather than a wrong-looking sweep.

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

Still to do for animation: `@keyframes` animations and `cubic-bezier(...)`.

