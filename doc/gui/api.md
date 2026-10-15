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
win.getBoundingClientRect(selector)    // { x, y, width, height } (border box)
win.documentTree()                     // serialized DOM (debugging)
win.layoutTree()                       // serialized layout boxes (debugging)
win.paintCount()                       // number of shapes in the display list
win.paintList()                        // serialized display list (debugging)
win.measureText(text, fontSize?, family?)  // shaped advance width in pixels
win.fontMetrics(fontSize?, family?)    // { ascent, descent, lineHeight, ready }
win.hitTest(x, y)                      // deepest element descriptor, or ""
win.sendEvent(type, options?)          // synthesise input (testing)
win.advance(ms)                        // step the CSS transition clock (testing)
win.stats()                            // engine/runtime counters (memory debugging)
```

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
