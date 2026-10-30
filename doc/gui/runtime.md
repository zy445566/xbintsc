# Runtime integration and memory

> Part of the [GUI extension](../gui.md); the engine overview, locked
> decisions and roadmap live there.

## Event loop integration

The runtime change (already landed):

- `int xt_loop_poll(int timeout_ms)` — one reactor iteration; `0` polls without
  blocking, `< 0` blocks.
- `void xt_loop_set_main(xt_main_loop_fn fn)` — a host may take over the main
  loop. `xt_run_event_loop()` delegates to it; the generated `main` is unchanged.
- `xt_loop_set_main(NULL)` restores the default `select(2)` loop.

The GUI engine can either register its own loop through `xt_loop_set_main`, or
expose an explicit `run()`; **M2 uses the explicit `run()`** so the window is a
plain native call the TypeScript program controls. Each tick it:

1. pumps SDL window/input events for every window,
2. reconciles the decoder/canvas tables with the DOM **only when the document
   changed** (or on the first frame),
3. builds/uploads the display list when it changed and renders every open
   window (clear pass + shape geometry + text geometry),
4. calls `xt_loop_poll(0)` and `xt_drain_microtasks()` so sockets/timers and
   `await` continuations keep making progress,
5. repeats until all windows close or `quit()` is called.

A frame **presents only when something happened**: a DOM mutation, a scroll, a
queued animation frame, a running transition, a new video/canvas frame, an
expose/resize event, or the first frame. A static document therefore performs no
swapchain acquire and no draw while the loop keeps servicing events, timers and
promises; `XT_GUI_ALWAYS_PRESENT=1` restores the old always-present behaviour
when debugging a missed invalidation. An expose/shown/restore event is the one
present the *window system* asks for rather than the document, so it is counted
as `exposes` in `win.stats()`; a present a starved frame had to postpone (no
swapchain image was available) is counted as `deferred`, and
`presents - exposes - deferred` is the work the page caused. `XT_GUI_PERF=1`
prints what each frame did (see
[Performance](./rendering.md#performance-m20)).

The `xt_loop_set_main` hook remains available for hosts that want to own the
loop themselves.

### The frame pump (pages that never call `run()`)

A page written for the browser does not call `run()`; it calls
`requestAnimationFrame` and expects the host to paint. `browser_loop.cpp` covers
that case with a single 16 ms `xt_set_timeout` tick:

- it starts at `loadHTML`, not at the first `requestAnimationFrame` — `ready` is
  emitted by the first *presented* frame, so waiting for the page's callback
  would deadlock;
- each tick dispatches pending input, renders every open window (which drains
  queued animation-frame callbacks before layout, so a page can stop itself),
- it re-arms **only while callbacks remain**, so an idle page lets the event
  loop drain and the process exit — the browser-tab lifecycle;
- it schedules the next tick for *frame start + 16 ms*, not 16 ms after the
  frame finished: the frame's own duration is not added to the interval, so a
  10 ms frame runs at a 16 ms period instead of 26 ms, and a page slower than
  16 ms runs as fast as it can instead of falling a frame's worth of time
  further behind on every frame;
- it stands aside while `xt_gui_run_active()` is set (the program called
  `run()`), so the same window is not rendered twice per tick;
- `XT_GUI_AUTOCLOSE_MS=n` makes the pump quit after `n` milliseconds, which is
  how the tests run a page that never closes itself.

This keeps network I/O, timers and `await` working inside a GUI program.

## Native ↔ TS bridge

- **TS → engine**: direct `xt_gui_*` calls / window methods.
- **Engine → TS**: `xt_call_with_this(fn, thisValue, argc, argv)` with function
  values captured from TS (e.g. event handlers registered with `win.on(...)`).
- **Future page→native RPC**: not needed while there is no page JS; native TS is
  the controller. If a declarative layer is added later, it will use the same
  `xt_call_*` entry points.

## Memory & GC

The engine owns most of a GUI program's memory outside the runtime heap: the DOM
tree, the detached-node pool, the element-handle tables, the layout box tree and
the display list are C++, so the mark-sweep collector cannot see them. `stats()`
(see [Diagnostics](./api.md#diagnostics)) reports the parts that can grow:

| Counter | Expected in a steady state |
| --- | --- |
| `treeNodes` | constant — the authored document |
| `detachedRoots`, `detachedNodes` | 0, or constant for nodes the script removed itself and still holds |
| `handles`, `handleSlots` | constant — one per element the script still refers to |
| `listeners` | constant — one entry per element with listeners |
| `heapBytes`, `heapAllocations` | sawtooth around the collector's threshold |

Everything else is per-frame scratch (boxes, display list, vertex staging) that is
freed before the next frame. A counter that climbs with the frame count is the
leak signature to look for first; `tests/e2e/gui-retention.test.ts` asserts that
the usual mutation patterns leave all of them unchanged.

Two engine details matter when reading those numbers:

- **What releases nodes.** `innerHTML`/`inner`/`textContent`/`replaceChild`
  release the subtrees they drop; `removeChild`/`remove` keep them (see [Node
  lifetime](./dom.md#node-lifetime)). A game that rebuilds a sprite with
  `innerHTML` therefore runs in constant memory.
- **What can still accumulate.** A node created with `createElement` and never
  attached stays in the pool, because the collector has no weak references and
  the engine cannot tell that the script dropped its handle. The same applies to
  a node removed and then forgotten by the script. Both are bounded by the number
  of *live* handles, but a program that creates elements per frame without
  attaching them grows the pool with the frame count.
- **Vector images are cached by pixel size.** A bitmap `<img>` decodes once and
  is cached by path, but an SVG is rasterised at the drawn device size, so the
  decoded-pixel and GPU-texture caches hold one entry per distinct size. That is
  constant for a static layout (the usual case) and small for a handful of
  sizes, but a program that reflows one SVG to a new integer size every frame
  accumulates entries; prefer animating a bitmap, or keep the vector to a fixed
  size.

The collector behind `heapBytes` is a non-moving mark-sweep (`runtime/xt_alloc.c`).
Its trigger follows the live set: after every collection the threshold becomes
twice the surviving heap (floor 1 MiB), so a small live set means small, frequent
collections and a bounded resident set. `XT_GC_THRESHOLD=<bytes>` pins the
threshold instead, and `XT_GC_TRACE=1` prints one line per collection (heap and
live-allocation counts before and after, the next trigger, and the live set by
object kind) to stderr — that is how the DOM leak was traced to the node pool.

