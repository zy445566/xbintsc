# GUI API

> Part of the [GUI extension](../gui.md); the engine overview, locked
> decisions and roadmap live there.

## TS-facing API

```ts
import { createWindow, run, quit } from "gui";

const win = createWindow({ title: "Demo", width: 900, height: 600 });
win.setBackground("#14161c");
win.loadHTML(INDEX_HTML);              // parses HTML/CSS and computes styles
win.on("ready", () => console.log("first frame presented"));
win.on("close", () => console.log("window closed"));

run();                                 // drives the main loop until all windows close
```

Methods implemented on a window handle: `setTitle` / `setSize` / `loadHTML` /
`getHTML` / `setBackground` / `close` / `isOpen` / `on` / `off`, plus `driver()`,
which reports the SDL_GPU backend in use (`"vulkan"`, `"direct3d12"`, `"metal"`,
… — SDL picks the best one the device supports). The Web spellings are available
too: `addEventListener` / `removeEventListener` are aliases of `on` / `off` (their
`options` argument is accepted and ignored) and the `closed` getter mirrors
`isOpen()`, so a document written against the browser API runs unchanged.
Events emitted:
`ready` (after the first presented frame), `load`, `close`, `resize` and the
input events `mousemove`, `mousedown`, `mouseup`, `click`, `wheel`, `keydown`,
`keyup`.

On Windows a GUI program is linked as a **Windows-subsystem** executable, so
launching it does not flash a console window. That also means stdout/stderr go
nowhere when it is started from Explorer; set `app.console: true` in
`xbintsc.config.json` to keep the console subsystem (useful while debugging).

## Browser globals

A document ported from the web rarely stops at `createWindow`. With `--ext gui`
the runtime also serves the small browser surface such ports touch, without the
program declaring any of it:

```ts
const ctx = new AudioContext();            // also window.AudioContext
ctx.createGain().connect(ctx.destination);
document.querySelector("#start").addEventListener("click", () => {});
console.log(navigator.userAgent, window.innerWidth, location.href);
requestAnimationFrame(frame);
```

| Global | Value |
| --- | --- |
| `window`, `self` | the most recently opened window handle (one is opened implicitly if the program has not created one yet), so `window.innerWidth`, `window.addEventListener`, `window.setTimeout`, `window.localStorage`, `window.location`, `window.devicePixelRatio` and `window.matchMedia` all work |
| `document` | that window's document handle |
| `navigator` | `userAgent` (a desktop Chrome string ending in `xbintsc`), `platform`, `vendor`, `language(s)`, `hardwareConcurrency`, `maxTouchPoints`, `onLine`, `vibrate()` |
| `localStorage` | an in-memory `getItem` / `setItem` / `removeItem` / `clear` / `key` / `length` store |
| `AudioContext`, `webkitAudioContext` | a Web Audio graph builder (see below) |
| `Event`, `CustomEvent`, `ProgressEvent`, … | event constructors |
| `HTMLCanvasElement`, `HTMLImageElement`, `ImageBitmap`, `ImageData`, `OffscreenCanvas`, `WebGL*`, `XRWebGL*`, `Request`, `Headers`, `Response`, `ReadableStream`, `DOMParser` | `typeof` probes answer `"undefined"` (the engine has no general WebGL/XR/fetch/streaming backend), which lets a page take its "unsupported" branch instead of calling a no-op stub. Three.js is the exception: the compiler's `.js` asset loader swaps its `WebGLRenderer` for a Canvas2D scene renderer (see [Known issues](./known-issues.md)). |
| `requestAnimationFrame` / `cancelAnimationFrame` | queued on the window's frame list; see [Runtime integration](./runtime.md) |
| `alert` / `confirm` / `prompt`, `matchMedia`, `getComputedStyle`, `escape` | host implementations (Annex B `escape` encodes per UTF-16 code unit, matching `decodeURIComponent(escape(bytes))`) |

`getElementById` / `querySelector` are answered even before `loadHTML` runs: the
handle is *pending* and binds to the real node the first time it is used, so a
module that caches `document.querySelector("#id")` at import time keeps working.
Once the page is parsed a miss answers `undefined`, like the browser.

### Web Audio

`new AudioContext()` builds a graph but **does not mix or render audio**: there
is no backend behind it. What is implemented is the surface a page needs to run
and to read back its own state — `createGain` / `createOscillator` /
`createBufferSource` / `createBiquadFilter` / `createDynamicsCompressor` /
`createBuffer` (real `Float32Array` channels) / `decodeAudioData` (resolves with
an empty buffer) / `resume` / `suspend` / `close`, `AudioParam` `value` plus
`setValueAtTime` / `linearRampToValueAtTime` / `exponentialRampToValueAtTime` /
`setTargetAtTime` / `cancelScheduledValues` / `cancelAndHoldAtTime`, and node
`connect` (returns the destination, so chains chain) / `disconnect` / `start` /
`stop`. `sampleRate` is 48000, `state` `"running"`, and `currentTime` advances
only through the event loop.

Input handlers receive a single payload object (lifecycle handlers receive none):

```ts
win.on("click", (e) => console.log(e.x, e.y, e.button, e.target));
win.on("wheel", (e) => console.log(e.deltaX, e.deltaY));
win.on("keydown", (e) => console.log(e.key, e.code, e.ctrl, e.shift, e.alt, e.meta));
```

| Field | Events | Meaning |
| --- | --- | --- |
| `x`, `y` / `clientX`, `clientY` | pointer, wheel | viewport-relative logical pixels |
| `button` | pointer | `0` left, `1` middle, `2` right (`-1` for move) |
| `clicks` | pointer | click count reported by the OS |
| `deltaX`, `deltaY` | wheel | scroll amount (`deltaY` positive = down) |
| `key`, `code` | keyboard | key name and physical scancode name |
| `repeat`, `ctrl`, `shift`, `alt`, `meta` | keyboard | modifiers |
| `target` | pointer, wheel | deepest element under the point as a CSS-like descriptor (`div#main.card`), or absent |

## Diagnostics

So the HTML/CSS/paint pipeline is testable without a GPU, a window handle
exposes read-only hooks:

```ts
win.computedStyle(selector, property)  // e.g. ("#main", "width") -> "60%"
win.queryCount(selector)               // number of matching elements
win.getBoundingClientRect(selector)    // { x, y, width, height } (border box, transformed)
win.documentTree()                     // serialized DOM (debugging)
win.layoutTree()                       // serialized layout boxes (debugging)
win.paintCount()                       // number of shapes in the display list
win.paintList()                        // serialized display list (debugging)
win.inlineSvg(selector)                // serialized inline <svg>: { markup, width, height, shapes }
win.measureText(text, fontSize?, family?)  // shaped advance width in pixels
win.fontMetrics(fontSize?, family?)    // { ascent, descent, lineHeight, ready }
win.hitTest(x, y)                      // deepest element descriptor, or ""
win.sendEvent(type, options?)          // synthesise input (testing)
win.advance(ms)                        // step the CSS transition clock (testing)
win.stats()                            // engine/runtime counters (memory debugging)
```

`win.inlineSvg(selector)` returns the standalone SVG document the engine builds
from the first `<svg>` element matching `selector` (`markup`), together with the
intrinsic size and shape count NanoSVG parses out of it; it is `undefined` when
nothing matches or the element is not an `<svg>`. It makes the serializer's
canonical casing (`viewBox`, `linearGradient`, `gradientUnits`, …) testable
without a GPU — the HTML parser delivers those names lowercased.

`win.stats()` returns `{ heapBytes, heapAllocations, treeNodes, detachedRoots,`
`detachedNodes, handles, handleSlots, listeners }`: the live runtime heap, the
size of the DOM tree, the detached-node pool, and the element-handle tables. In a
steady state every one of them is constant — a climbing `detachedNodes` or
`handles` means the program removes nodes faster than it releases them (see
[Node lifetime](./dom.md#node-lifetime)).

They are used by `tests/e2e/gui-*.test.ts` to assert parsing, selector matching,
specificity, inheritance, `!important` and layout geometry. They will stay useful
afterwards for debugging.

## Multiple windows

Multiple windows fall out of the object model: `createWindow` returns a native
object handle; each handle owns its own `SDL_Window`/GPU surface and its own DOM
tree. `run()` drives one shared main loop that ticks every window and exits when
the last one closes.
