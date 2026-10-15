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
  `border-radius`; `transition: all` covers them. Structural/layout-affecting
  properties are not animated yet (that would relayout every frame).
- Both the `transition` shorthand and the `transition-property` / `-duration` /
  `-delay` / `-timing-function` longhands are parsed; time values accept `s` and
  `ms`; timing functions are `linear`, `ease`, `ease-in`, `ease-out` and
  `ease-in-out` (`ease` is a smoothstep approximation).
- Retargeting mid-flight starts a new transition from the current interpolated
  value, so reversing a hover animates smoothly from wherever it was.
- `computedStyle()` reports the displayed (interpolated) value, so transitions
  are directly observable in tests.

Still to do for animation: `@keyframes` animations and `cubic-bezier(...)`.

