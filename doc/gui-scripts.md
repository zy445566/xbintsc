# GUI extension — DOM handles, events and AOT `<script>`

Status: **design** — this document specifies how the `gui` extension grows from
read-only HTML/CSS rendering (M7) into a small, *interactive* DOM with
**compile-time (AOT) scripts**, without adding a JavaScript engine and without
letting GUI concerns leak into the core compiler.

It is the plan behind milestones **M8** (DOM object model + mutation + element
events) and **M9** (AOT `<script>` pipeline). The locked decisions in
`doc/gui.md` still hold; this document only refines what "no page JS" means.

## Motivation

Today the engine renders an HTML/CSS tree and exposes only *window-level*
events (`win.on("click", …)`). The handler receives `e.target` as a CSS
descriptor string (`div#main.card`), the DOM is read-only (diagnostics only),
and the only way to change the UI is `win.loadHTML(...)`, which rebuilds
everything.

That is enough for static demos, but the obvious next question — "can I write
`<script>` and react to clicks on a specific element?" — has no answer yet.
xbintsc is a **pure AOT compiler** and has **no runtime interpreter/JIT**, so
`eval`-style script execution is impossible. The only self-consistent way to
run scripts is to **compile them ahead of time**.

## Goals

1. **Element handles** — `document.querySelector(...)` returns an object with
   stable identity (`a === b` for the same element) and readable/writable DOM
   properties (`id`, `className`, `textContent`, `innerHTML`, `style`,
   `classList`, attributes, traversal, geometry).
2. **Mutation** — `appendChild` / `removeChild` / `insertBefore` /
   `replaceChild` / `textContent=` / `innerHTML=` / `classList.*` /
   `style.setProperty` update the tree; the engine restyles + relayouts lazily
   and repaints.
3. **Element events** — `el.addEventListener(type, fn, options?)` with capture
   and bubble phases, `removeEventListener`, `dispatchEvent`, `el.click()`,
   event objects with `target` / `currentTarget` / `preventDefault` /
   `stopPropagation`.
4. **AOT `<script>`** — a `<script>` body written in TypeScript is extracted at
   **compile time**, compiled by the *existing* xbintsc front-end as a normal
   module, and invoked by the engine after the document is parsed.
5. **Zero core-compiler coupling** — the lexer/parser/binder/codegen learn
   nothing about HTML or the GUI. Extensions contribute only modules/objects
   (and, new in M9, an **asset-loader hook**).
6. **Backward compatibility** — existing programs and tests keep working:
   `win.on(...)` handlers keep receiving a *string* `e.target`.

## Non-goals

- **A JS engine** (QuickJS/V8/…). Still explicitly out of scope. There is no
  runtime `eval`, no interpreter, no JIT.
- **Dynamic scripts at runtime.** Scripts are compile-time assets. HTML fetched
  over the network or produced at runtime does **not** execute its scripts.
- **A security sandbox / browser semantics.** There is no origin model; a
  script is ordinary native TS and can `import` anything the compiler allows.
  This is documented as "not a browser".
- **Full DOM/BOM.** We implement the practical subset a UI needs.

## Locked decisions (refined)

| # | Decision |
| --- | --- |
| 1 | **No page JS engine.** Behaviour is native TS. `<script>` bodies are *AOT-compiled* to native code by xbintsc and invoked through `xt_call_with_this` — there is still no interpreter. |
| 8 | **Scripts are compile-time assets.** The HTML asset loader transforms inline `<script>` bodies into compiled modules and replaces them with `data-xt-id` markers; the runtime only *invokes* already-compiled functions. |
| 9 | **The DOM is owned by the engine; handles are engine objects.** Handles use the same shared-prototype object model as window handles; hidden fields carry a window reference, a document generation and a node index. |
| 10 | **`window`/`document` are injected as parameters**, not globals, so no global-object machinery is added to the compiler. |

## Architecture

```
     <script>…</script>            compile time (bundler)
            │  gui .html asset loader
            ▼
   ┌──────────────────────┐        ┌───────────────────────────┐
   │ transform the HTML   │        │ emit  __xt_script_<hash>  │  a normal
   │ body → function      │        │       (window, document)  │  xbintsc
   │ <script data-xt-id>  │        │ + __registerScript(hash)  │  module
   └──────────────────────┘        └───────────────────────────┘
            │                                   │
            ▼                                   ▼
     runtime HTML string              native code linked into the binary
            │  win.loadHTML(html)
            ▼
   engine parses HTML, finds `data-xt-id` markers, looks each hash up in the
   script registry and calls `fn(windowHandle, documentHandle)`
```

### 1. `Extension.assetLoaders` (the only new core hook)

The core compiler currently has no way for an extension to influence *how a
file is loaded*. M9 adds one generic hook to `src/extensions/registry.ts`:

```ts
interface AssetLoadResult {
  /** Replaces the file contents as seen by the bundler. */
  moduleSource: string;
  /** Additional files this asset depends on (recompiled when they change). */
  dependencies?: string[];
}

interface Extension {
  // …existing fields…
  /** Optional: rewrite an imported asset into a TS module. Keyed by extension. */
  assetLoaders?(): Record<string, (path: string, source: string) => AssetLoadResult>;
}
```

The bundler (`src/driver/bundler/graph.ts`) consults the registry immediately
after `readFileSync`, *before* parsing. The core stays platform-agnostic: it
only knows "an extension may transform the bytes of `*.foo` into TS".

The `gui` extension registers an `.html` loader. A plain `import html from
"./index.html"` therefore yields a **string constant** by default, and (when
the file contains `<script>`) a **side-effecting module** that registers the
compiled script bodies and default-exports the rewritten HTML.

### 2. The `.html` transform

Given:

```html
<button id="b">0</button>
<script lang="ts">
  const b = document.getElementById("b");
  let n = 0;
  b.addEventListener("click", () => { b.textContent = String(++n); });
</script>
```

the loader emits a module shaped like:

```ts
// generated
const __html = "<button id=\"b\">0</button>\n<script data-xt-id=\"a1b2…\"></script>";
function __xt_script_a1b2(window: any, document: any): void {
  const b = document.getElementById("b");
  let n = 0;
  b.addEventListener("click", () => { b.textContent = String(++n); });
}
__registerScript(() => "a1b2…", __xt_script_a1b2);
export default __html;
```

- The id is `sha1(body)`; the body is replaced by an empty marker element so
  the engine can find it in document order.
- `window` and `document` are **function parameters**, so script globals resolve
  as ordinary locals — **no compiler changes**, no global object.
- `__registerScript(id, fn)` is a new runtime builtin that stores the closure in
  a global `unordered_map<string, xt_value>`.
- Deferred scripts (`<script defer>`, or all inline scripts, as decided in M9c)
  run after the full document is parsed and the first layout is computed, in
  document order. `DOMContentLoaded` then `load` are fired on `window`.

### 3. Runtime execution

`win.loadHTML(html)`:

1. parses the document as today,
2. scans for `data-xt-id` markers,
3. for each, calls `registry[id](winHandle, docHandle)`,
4. triggers the first layout, then fires `DOMContentLoaded` and `load`.

Because the calls happen after parsing, `document.getElementById(...)` inside a
script always finds its element. `innerHTML = …` never executes embedded
scripts (markers are inert).

## Element handle object model (M8)

Handles are plain runtime objects (`xt_object_new_with_proto`) sharing a
prototype per kind, exactly like window handles:

| Hidden field | Meaning |
| --- | --- |
| `__xt_gui_win` | owning window object (resolved by `xt_gui_window_from_this`) |
| `__xt_gui_gen` | document generation the handle was created for |
| `__xt_gui_node` | index into the window's node table (element/text handles) |
| `__xt_gui_doc` | `true` for the singleton `document` handle |

**Identity is stable**: the window keeps `node_order: vector<Node*>` plus
`node_index`/`node_handles: map<Node*, …>`, so querying the same element twice
returns the *same* object value (`a === b`).

**Detached nodes** stay alive in the document pool, so a handle held across a
`remove()` still resolves (tombstoned semantics); a generation mismatch makes a
stale handle a no-op instead of a dangling pointer.

### Accessors, not fields

Read/write DOM properties are defined with `xt_object_define_getter` /
`xt_object_define_setter`, which already support prototype inheritance with
`this` = receiver. So `el.textContent = "x"` and `el.id = "y"` work while the
values live in the C++ tree. `classList` and `style` are small objects that hold
a back-reference to the element handle (no closures/env needed; the arena never
frees, so cycles are harmless).

### Implemented surface (M8)

- `document`: `querySelector`, `querySelectorAll`, `getElementById`,
  `createElement`, `createTextNode`, `body`, `documentElement`,
  `addEventListener` / `removeEventListener` / `dispatchEvent`.
- node (element/text/document): `tagName` / `nodeName` / `nodeType`, `id`,
  `className`, `textContent` / `innerText`, `innerHTML`, `parentNode` /
  `parentElement`, `children` / `childNodes` / `childElementCount`,
  `firstElementChild` / `lastElementChild`, `nextElementSibling` /
  `previousElementSibling`, `isConnected`, `classList`, `style`.
- attributes: `getAttribute` / `setAttribute` / `hasAttribute` /
  `removeAttribute`.
- queries: `querySelector` / `querySelectorAll` / `matches` (subtree-scoped).
- mutation: `appendChild` / `insertBefore` / `removeChild` / `replaceChild` /
  `remove` / `cloneNode(deep?)`.
- geometry/focus: `getBoundingClientRect`, `focus`, `blur`, `click`.
- events: `addEventListener(type, fn, optionsOrCapture?)`,
  `removeEventListener`, `dispatchEvent`.

### Lazy restyle / relayout

Every mutation calls `XtDocument::invalidate()` and sets `win->struct_dirty`.
Before any synchronous read (`computedStyle`, `getBoundingClientRect`,
`queryCount`, `layoutTree`, `paintList`, `hitTest`, …) and before each frame,
`xt_gui_flush_dom()` restyles + relayouts **once** if anything is pending. This
keeps mutations cheap and batched while making reads immediately consistent.

## Events and bubbling

Element listeners are stored on the window (`node_listeners`), keyed by node
(not on `Node` itself, so the tree stays layout-focused).

Dispatch:

1. build the propagation path `target → … → root`,
2. create an Event object (`type`, `target` = node handle, `currentTarget`,
   `bubbles`, `defaultPrevented`, internal `__stop` / `__stopImmediate`),
3. **capture** phase: root → target (listeners registered with `capture: true`),
4. **bubble** phase: target → root,
5. legacy window-level `win.on(type, fn)` handlers run **last**.

`once` listeners are removed before being called; `stopPropagation` ends the
current phase chain and `stopImmediatePropagation` also skips later listeners on
the same node. `el.click()` synthesises `click` at that node (DOM only);
real pointer/wheel/key input dispatches **both** the DOM event and the legacy
window payload.

### Backward compatibility

The legacy window payload keeps its **string** `e.target`
(`tests/e2e/gui.test.ts` asserts `e.target === "div#inner"`). Only the *element*
Event's `target` is a handle, whose descriptor (`toString`) matches the same
`div#id.class` string. `make_dom_event` deliberately does **not** copy the
legacy `target` field over the handle.

## Milestones

- **M8 — DOM object model + mutation + element events** (this branch)
  - `runtime/ext_gui/dom_api.{h,cpp}`, `document.{h,cpp}`, `dom.{h,cpp}`,
    `gui_engine.h`, `gui.cpp`, `window.cpp`, e2e coverage.
  - No compiler changes; independently testable.
- **M9 — AOT `<script>`**
  - **M9a** — `Extension.assetLoaders` in `src/extensions/registry.ts` + bundler
    integration in `src/driver/bundler/graph.ts`.
  - **M9b** — gui `.html` loader (hash body, emit `data-xt-id` marker +
    `__xt_script_<hash>` module), `__registerScript` builtin, `loadHTML`
    execution and `DOMContentLoaded`/`load`.
  - **M9c** — `<script src>` resolved relative to the HTML file and compiled as
    a module import; `defer`/module ordering.
  - **M9d** *(optional)* — detect inline HTML in template literals passed to
    `win.loadHTML(...)` and transform them too (fragile; deferred).
- **M10 — polish** — `requestAnimationFrame`, a few more DOM helpers, docs.

### Landing order

1. M8 (this branch) — pure engine work, no compiler impact.
2. M9a — the generic hook + bundler wiring (unit-testable with a fake loader).
3. M9b — the minimal end-to-end script path (inline `<script>` only).

## Open risks

- **Handle lifetime.** Solved with a generation counter + detached pool; stale
  handles no-op. `Node*` addresses are never exposed to TS.
- **No sandbox.** A `<script>` can `import fs`. Documented as "not a browser";
  a real origin/sandbox model is out of scope.
- **Dynamic HTML scripts.** Scripts in runtime-created HTML (`innerHTML`,
  network fetches) do not run. Documented.
- **Template-literal HTML** passed to `win.loadHTML(…)` is not transformed in
  M9b (only imported assets are). M9d addresses this if needed.
- **Assets.** `<script src>` needs the bundler to resolve imports relative to
  the HTML asset; handled in M9c together with the dependency list returned by
  the loader.

## Testing

- **M8**: a new e2e case in `tests/e2e/gui.test.ts` covering handle identity,
  traversal, attributes, `classList`, inline `style`, `createElement` +
  `appendChild` + `removeChild`, `getBoundingClientRect`, element bubbling,
  `stopPropagation`, `el.click()` and **window-level `e.target` compatibility**.
- **M9a**: a unit test with a fake extension loader asserting the bundler
  rewrites `*.foo` and records dependencies.
- **M9b**: an e2e case with an inline `<script>` incrementing a counter on
  click, asserted through `console.log` from the handler.

## Docs to update

- `doc/gui.md` — Non-goals ("Executing page `<script>`" → "Executing *runtime*
  page scripts; compile-time scripts are AOT-compiled"), locked decision #1,
  TS-facing API, milestones, progress log.
- `doc/implemented.md` / `doc/unimplemented.md` — move the new DOM/script
  capabilities across.
- `README.md` — mention the DOM API + AOT scripts.
