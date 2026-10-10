# 鹈鹕骑自行车 — Pelican Bike

A small, playable 2D game written as an **HTML/CSS document whose game loop is
TypeScript** — and the same file runs in two hosts:

- **In a browser**: open `index.html` directly (double-click it, or drag it into
  a tab). No build step, no server, no dependencies.
- **As a native binary**: xbintsc compiles the inline `<script lang="ts">` body
  ahead of time into native code and links it against the `gui` extension, so
  the game becomes a standalone executable with no JavaScript engine inside.

```
index.html          the whole game: markup, stylesheet, the inline script, and
                    the small `dom` shim that adapts it to the host
main.ts             the native driver: opens the window, loads the document,
                    prints a geometry report (used by the e2e test)
xbintsc.config.json entry + `gui` extension, so no flags are needed
```

The engine's window implements the Web APIs the game uses:
`window.addEventListener`, `window.requestAnimationFrame`, `window.close()` and
`window.closed`. `index.html` therefore has no host detection at all — a few
lines of `dom` plumbing name those APIs once and the rest of the game is plain
Web code:

```ts
const dom = {
  on: (t, fn) => { window.addEventListener(t, fn); },
  tick: (fn) => window.requestAnimationFrame(fn),
  open: () => !window.closed,
  closeWindow: () => { window.close(); }
};
```

Everything — `document.getElementById`, `getBoundingClientRect`,
`style.setProperty`, `textContent`, `innerHTML`, `requestAnimationFrame` — is the
same in both. The script also avoids `(window as any)` (the engine's parser
rejects `as`, and there is no need for it: `window["__pelican"] = hooks` works in
a browser and compiles natively). Keyboard *input* is the one place where the two
hosts genuinely disagree — over the *spelling* of a key, not the API — and the
game reconciles the two; see "Layout notes" below.

In the browser the game publishes its hooks on `window.__pelican`, so the console
can drive it:

```js
__pelican.info()      // host, player box, HUD state
__pelican.setAuto(false); __pelican.jump(); __pelican.step(0.016)
```

## Play in a browser

Just open `index.html`. Click the page once so it has keyboard focus, then:

| Key | Action |
| --- | --- |
| `Space` / `↑` | jump |
| `→` | pedal harder (sprint) |
| `←` | brake |
| `P` | toggle autoplay (on by default — any key takes over) |
| `R` | restart after a crash |
| `Esc` | attempt to close (browsers only allow this for a script-opened tab) |

The scene is a fixed 1000×620 box, exactly as in the native window.

## Build and run natively

From the repository root, in an **x64 Native Tools Command Prompt for VS 2022**
(clang needs the MSVC/SDK environment on Windows):

```bat
set xbintsc_CLANG=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang.exe
npx tsx src/cli/main.ts run examples/gui/pelican-bike/main.ts
```

The example ships `xbintsc.config.json`, so `--ext gui` is implied. To build a
standalone binary and play it:

```bat
npx tsx src/cli/main.ts build examples/gui/pelican-bike/main.ts -o build/pelican-bike.exe
build\pelican-bike.exe
```

A release build of xbintsc does the same without the `npx tsx` prefix:

```bash
xbintsc build examples/gui/pelican-bike/main.ts --out build
```

The game runs until you close the window. Set `XT_GUI_AUTOCLOSE_MS=<ms>` to make
the engine close it after a delay instead — that is the smoke-test mode the e2e
harness uses. In that mode the program prints a geometry report every 300 frames,
so a run proves the document laid out (the same output is asserted by
`tests/e2e/gui-example.test.ts`):

```
window=1000x660 (gui extension, SDL_GPU)
gpu=vulkan
frames=300
title=鹈鹕骑自行车
scene=x=0 y=40 1000x620
sky=x=0 y=40 1000x210
road=x=0 y=372 1000x84
pelican=x=420 y=404 52x46
rider=x=420 y=404 52x46
shadow=x=396 y=447 150x3
hud=0004 4 28 ▮▮▯▯▯▯▯ 4
ground=450
player-box=420,404 52x46
last-hit=-
handle-identity=stable
paint-shapes=72
hit-test=div#scene.scene
```

`gpu=` is `win.driver()`: SDL_GPU picks the backend the device supports (Vulkan,
Direct3D 12 or Metal), and the engine ships the matching shader format for each.
`frames=` is a running frame counter — the engine has no vsync or frame cap yet,
so it presents as fast as the GPU allows, and the simulation is delta-timed, so
the game plays identically at any frame rate.

## Controls

| Key | Action |
| --- | --- |
| `Space` / `↑` | jump |
| `→` | pedal harder (sprint) |
| `←` | brake |
| `P` | toggle autoplay (the pelican rides itself by default) |
| `R` | restart after a crash |
| `Esc` | quit: closes the native window; a browser tab may ignore `window.close()` |

Every key works in both hosts: the native engine reports `Space`/`Up`/`Right`/
`Left`, a browser reports ` `/`ArrowUp`/`ArrowRight`/`ArrowLeft`, and the game
matches either spelling (see "Layout notes"). A jump only clears an obstacle if
it starts early enough for the bike to be *above* it when it arrives, so jump as
the obstacle comes into range, not when it is almost on you.

## How it is put together

- **One positioned scene.** `.scene` is `position: absolute` inside the viewport;
  the sky gradient, clouds, hills, trees, road, the bike and the obstacles are all
  absolutely positioned inside it, so the game moves things with `left`/`bottom`
  insets instead of fighting the flow. Painting follows document order (there is
  no `z-index` yet), which is why the layers appear back-to-front in the markup.
- **CSS sprites, drawn programmatically.** The pelican and the bike are painted
  onto a 52×46 pixel grid by a small raster (`plot`/`line`/`ring`/`disc`/`blit`)
  and emitted as merged horizontal runs of 1px boxes: real wheels with spokes and
  tyres, a frame of diagonal tubes, a handlebar with a bell, and a pelican with a
  head, wing, pouch and pedalling legs. No `<canvas>`, no images, no SVG (the
  engine has none — see `doc/gui.md`). Each run is placed with its own
  `left`/`width` rather than by line flow: an absolutely positioned box with
  `width: auto` gives its inline content no available width in the engine, so a
  row of runs would otherwise wrap one run per line. See "Layout notes" below.
- **Two layers, so a frame stays cheap.** The static half (frame, bird, saddle)
  is built once; only the animated half (wheels, cranks, legs) is rebuilt each
  frame. Rebuilding the whole 350-box sprite per frame made the engine's relayout
  dominate and dropped the game to single-digit fps; the split brings it back to
  100+ fps.
- **The loop drives the DOM.** `requestAnimationFrame` steps the simulation; each
  frame sets a handful of `left`/`bottom`/`margin-left` values and the host
  restyles, relayouts and repaints once, batching all mutations.
- **Collision is measured, not guessed.** Every frame the bike's *contact patch*
  — the sprite's lower `HIT_H` pixels, from its centre down to the ground row —
  is measured against each obstacle's box, both in viewport coordinates, and the
  road line is derived from the rider's box at startup, so tuning the CSS keeps
  the physics consistent. The bird rides above that patch, which is what lets a
  jump clear an obstacle instead of clipping it with the pouch. An obstacle's
  rows are absolutely positioned, so they give their container no size; the
  container is therefore given the sprite's own `width`/`height` when it spawns,
  which is what makes its measured box — and so the collision — real, in the
  engine and in a browser alike (`getBoundingClientRect()` reports the border
  box, not the ink).
- **The jump is a real arc.** `JUMP_AIRTIME` (1.2s) and `GRAVITY` (1500px/s²)
  set the launch speed, so the bike rises ~270px — well clear of the tallest
  obstacle — and `state.y` drives the pelican's `bottom` inset, so the bird
  visibly leaves the road. The autopilot jumps once per obstacle, `JUMP_AIRTIME`
  early, which is the window measured to clear every obstacle type.
- **HUD.** Distance, score, a speed bar and the best run are DOM text nodes
  updated from the same loop.

The layout features this example relies on — `position: relative`/`absolute`
with `top`/`right`/`bottom`/`left`, shrink-to-fit and fill-available widths,
margins inside flex layout, and collapsing the source whitespace between block
boxes — are implemented in `runtime/ext_gui/layout_*.cpp`; the first two and the
flex margins are covered by `tests/e2e/gui-layout.test.ts`.

### Layout notes

Two of the engine's rules shape this document, and both are ordinary CSS once
you look closely — but they are easy to trip over when the game must be correct
in the engine *and* in a browser:

- **A box's size comes from its in-flow content.** The sprite rows are
  `position: absolute`, so an obstacle's container has no size of its own unless
  it is given one; `getBoundingClientRect()` reports that border box (0×0), not
  the ink of its overflowing rows. Hence `spawnThing` sets `width`/`height` from
  the sprite's own dimensions before the collision loop ever measures it.
- **An `auto`-width absolute box has no available width for line flow.** Its
  preferred width comes from its text and its non-absolute children only, and
  its inline content is then laid out at zero available width, so each run wraps
  onto its own line. That is why a sprite run carries an explicit `left` and
  `width` instead of relying on `display` order inside the row.
- **`opacity` is only honoured at its extremes**, so the game-over overlay is
  invisible while it has no `show` class and fully opaque once it has one — the
  intended fade-in is not visible in the engine (see `doc/gui.md`, "Implemented
  paint").
- **The two hosts spell keys differently.** The engine names a key the way
  `SDL_GetKeyName` does (`"Space"`, `"Up"`, `"Right"`); a browser reports
  `KeyboardEvent.key`, which is `" "` for the spacebar and `"ArrowUp"` /
  `"ArrowRight"` for the arrows. Comparing against one spelling silently works in
  one host and does nothing in the other, so `canJumpKey`/`isRightKey`/`isLeftKey`
  accept both, falling back to `KeyboardEvent.code` (`"Space"`, `"ArrowUp"`),
  which is the one spelling both agree on. The browser branch is covered by
  `tests/e2e/gui-example-browser.test.ts`, which delivers real key events rather
  than calling `jump()` directly.

## Tests

- `tests/e2e/gui-example.test.ts` — compiles and runs the native program (the
  gui archive is required; on Windows opt in with `xbintsc_GUI_TESTS=1`). The
  engine's auto-close path has an outstanding intermittent crash, documented in
  `doc/gui.md`, so this test accepts that outcome — the Windows exit code, or
  on Linux/macOS a crash signal reported as `status: null` — and asserts the
  report only when it arrives.
- `tests/e2e/gui-example-browser.test.ts` — extracts the inline script, runs it
  against a stub browser host, and drives frames through `window.__pelican`, so
  the browser branch cannot silently rot (no GPU, no build, deterministic).

## Known issues

- The engine's `XT_GUI_AUTOCLOSE_MS` shutdown crashes intermittently (native
  access violation — exit code `0xC0000005` on Windows, `SIGSEGV` on Linux,
  ~2 runs in 5); playing normally is unaffected. The engine now roots the
  values it caches in its own containers, which was a leading cause; details are
  in `doc/gui.md`, "Known issues".
- The game-over overlay is revealed with `opacity`, not `display`, because a
  `display: none -> flex` restyle has been seen to crash the same way.
