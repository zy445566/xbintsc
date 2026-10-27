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
too: `addEventListener` / `removeEventListener` take the standard options bag
(`capture`, `once`, `passive`, `signal`), and `closed` mirrors `isOpen()`, so a
document written against the browser API runs unchanged.

Element listeners accept the same options: `{ once }` drops the listener after
its first call, `{ passive: true }` makes `preventDefault()` a no-op, and
`{ signal }` (an `AbortSignal` from `new AbortController()`) removes it on
abort, including a signal that was already aborted when the listener was added.
`element.dispatchEvent(event)` dispatches the *same* event object (identity is
observable), fills in `target` / `currentTarget` / `eventPhase` and records the
path for `event.composedPath()`. `new Event(...)`, `new CustomEvent(...)`,
`new MouseEvent(...)`, `new KeyboardEvent(...)`, `new PointerEvent(...)`,
`new WheelEvent(...)`, `new FocusEvent(...)`, `new InputEvent(...)` and
`new SubmitEvent(...)` keep every field of their `init` object (`detail`,
`composed`, the mouse/keyboard members, …).
Events emitted:
`ready` (after the first presented frame), `load`, `close`, `resize` and the
input events `mousemove`, `mousedown`, `mouseup`, `click`, `wheel`, `keydown`,
`keyup`.

On Windows a GUI program is linked as a **Windows-subsystem** executable, so
launching it does not flash a console window. That also means stdout/stderr go
nowhere when it is started from Explorer; set `app.console: true` in
`xbintsc.config.json` to keep the console subsystem (useful while debugging).

A host without a usable GPU cannot run the GUI at all. When SDL cannot open a
video driver or create a Metal/Vulkan/D3D12 device, the engine prints the reason
and exits with status **77** — the autotools "skip" convention, so CI and scripts
can tell "this machine has no GUI" from "the program failed". A window that
cannot be opened after that (an SDL window or GPU-claim error) makes
`createWindow` throw a JavaScript `Error`; it no longer returns an `undefined`
handle that fails a line later as `Cannot read properties of undefined`.

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
| `AudioContext`, `webkitAudioContext` | a Web Audio graph with a real mixer and decoder (see below) |
| `Event`, `CustomEvent`, `ProgressEvent`, `MouseEvent`, `KeyboardEvent`, `PointerEvent`, `WheelEvent`, `FocusEvent`, `InputEvent`, `SubmitEvent` | event constructors; the `init` argument is kept as given (`detail`, `composed`, mouse/keyboard fields, …) |
| `AbortController`, `AbortSignal` | a real controller/signal pair for `{ signal }` listener removal, with `abort()`, `aborted`, `reason`, `onabort` and `throwIfAborted()` |
| `HTMLCanvasElement`, `HTMLImageElement`, `WebGLRenderingContext`, `WebGL2RenderingContext`, `WebGLBuffer`/`WebGLTexture`/`WebGLShader`/`WebGLProgram`/`WebGLFramebuffer`/`WebGLRenderbuffer`/`WebGLUniformLocation`/`WebGLActiveInfo`/`WebGLVertexArrayObject`/`WebGLSampler`/`WebGLQuery`/`WebGLSync`/`WebGLTransformFeedback` | real constructor values. `canvas.getContext("webgl")` and `getContext("webgl2")` return GPU contexts (see [WebGL](./webgl.md)). |
| `ImageBitmap`, `createImageBitmap(source)` | immutable bitmaps backed by decoded pixels (`<img>`, `<canvas>`, `<video>` frames and `ImageData` shapes); the promise resolves immediately and `texImage2D` accepts the result |
| `ImageData`, `OffscreenCanvas`, `XRWebGL*`, `Request`, `Headers`, `Response`, `ReadableStream`, `DOMParser` | `typeof` probes answer `"undefined"` (no XR/fetch/streaming backend), which lets a page take its "unsupported" branch instead of calling a no-op stub. Three.js pages use its real renderer on this backend (see [Known issues](./known-issues.md)). |
| `requestAnimationFrame` / `cancelAnimationFrame` | queued on the window's frame list; see [Runtime integration](./runtime.md) |
| `alert` / `confirm` / `prompt`, `matchMedia`, `getComputedStyle`, `escape` | host implementations (Annex B `escape` encodes per UTF-16 code unit, matching `decodeURIComponent(escape(bytes))`) |

`getElementById` / `querySelector` are answered even before `loadHTML` runs: the
handle is *pending* and binds to the real node the first time it is used, so a
module that caches `document.querySelector("#id")` at import time keeps working.
Once the page is parsed a miss answers `undefined`, like the browser.

### Web Audio

`new AudioContext()` builds a real graph and mixes it: `runtime/ext_gui/audio_mixer.cpp`
renders 48 kHz stereo float in 128-frame quanta, multiple inputs are summed at
every node, and the first `start()`/`resume()` opens the default SDL playback
device (best effort — a headless host keeps rendering silently). The graph is
rendered on the SDL audio thread from native state; the page never has to pump
it, and no runtime value is touched off the main thread.

Implemented nodes: `createGain`, `createOscillator` (sine, square, sawtooth,
triangle), `createBufferSource` (buffer, loop, loopStart/loopEnd,
playbackRate/detune), `createBiquadFilter` (lowpass, highpass, bandpass,
lowshelf, highshelf, peaking, notch, allpass), `createDynamicsCompressor`,
`createStereoPanner`, `createConstantSource` and `createDelay(maxDelayTime)`.
`connect` returns the destination so chains chain (`src.connect(gain).connect(ctx.destination)`),
`disconnect()` / `disconnect(target)` update the native graph, and `start(when, offset, duration)` /
`stop(when)` are honoured on the graph's clock. Nodes outside this list
(`createAnalyser`, `createConvolver`, `createWaveShaper`, `createChannelSplitter`,
`createPeriodicWave`, …) are not provided, so their methods are simply absent.

Every `AudioParam` has a live `value` plus `setValueAtTime` /
`linearRampToValueAtTime` / `exponentialRampToValueAtTime` / `setTargetAtTime` /
`cancelScheduledValues` / `cancelAndHoldAtTime`. The automation timeline is
evaluated per sample, so a ramp keeps moving while the page is idle.
`currentTime` advances with the rendered quanta.

`createBuffer(channels, length, sampleRate)` returns an AudioBuffer whose
`getChannelData(i)` is a real `Float32Array`, so samples can be generated
procedurally. `decodeAudioData(bytes)` returns a promise for a decoded
AudioBuffer: WAV (PCM 8/16/24/32-bit, IEEE float 32/64, A-law and µ-law) is
decoded by the engine itself; MP3, AAC/M4A, Vorbis, Opus and FLAC need the
optional `media` extension (`--ext gui,media`), whose FFmpeg decoder is used as
the fallback. `bytes` may be an ArrayBuffer, a typed array, a Node `Buffer`, or
the raw byte string `fs.readFileSync` returns (this runtime has no dedicated
binary type, and that string's storage is exactly the file bytes). The result
is resampled to the context's 48 kHz, so `buffer.sampleRate` matches
`context.sampleRate`; the promise rejects with an `EncodingError` when nothing
can decode the bytes.

`resume()` / `suspend()` / `close()` control the device and the `state`
property; `sampleRate` is 48000, like the media decoder.

For tests and diagnostics an **offline** context renders synchronously:

```ts
const ctx = new AudioContext({ __offline: true });
// … build the graph and start sources …
const samples = ctx.__render(480);   // interleaved stereo Float32Array
```

This private hook never opens a device and is not part of the Web Audio API; it
is what `tests/e2e/gui-audio.test.ts` uses to pin the mixer math.

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