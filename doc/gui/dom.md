# DOM, HTML, CSS and AOT scripts

> Part of the [GUI extension](../gui.md); the engine overview, locked
> decisions and roadmap live there. The scripting surface has its own
> document, [gui-scripts.md](../gui-scripts.md).

## HTML/CSS subset (M3a)

**HTML parser** (`runtime/ext_gui/dom.{h,cpp}`): tags/attributes/text,
comments and doctype skipped, entity decoding (named + numeric/hex), void
elements, raw-text elements (`<style>`/`<script>`), and the common implicit
close rules (`li`, `dt`/`dd`, `option`, `p`, headings, table cells/rows).

**CSS parser** (`runtime/ext_gui/css.{h,cpp}`): comments are stripped; `@media`
blocks are parsed recursively (nested queries combine with `and`, and a comma
list distributes over the inner query) while other at-rules are skipped. Rules
support multiple selectors and declarations; a selector list may be formatted
across any number of lines (line breaks are ordinary whitespace, so a wrapped
combinator stays a combinator), and a quoted attribute value may contain
commas, brackets or parentheses without splitting the list. Selectors: type,
`.class`, `#id`, attribute (`=`, `~=`, `|=`, `^=`, `$=`, `*=`), the four
combinators (descendant, child, adjacent and general sibling), the pseudo-classes
`:first-child`, `:last-child`, `:only-child`, `:empty`, `:root`, `:not(...)`,
`:nth-child(an+b)`, `:disabled`, `:checked`, plus the stateful `:hover` and
`:focus` (see [Input](./input.md)), and the pseudo-elements `::before`/`::after`
(legacy `:before`/`:after` too). Values: lengths (`px`, `%`, `em`, `rem`, `vw`,
`vh`, `pt`, `pc`, `in`, `cm`, `mm`, `q`), colors (hex, `rgb()`/`rgba()`, a named
subset), numbers, keywords and shorthands (`margin`/`padding`/`border`/`flex`).
`transform` (`translate`/`translate3d`/`translateX`/`translateY`, `scale`/
`scale3d`/`scaleX`/`scaleY`, `rotate`/`rotateZ`, `matrix`; the `3d` spellings'
`z` argument is dropped) and `transform-origin` are parsed into the computed
style as a list of operations plus two (unresolved) origin lengths; an
unsupported function invalidates the whole declaration
([Layout](./layout.md#transforms-m15)). M19 adds `background-image` layer lists
(linear/radial), `clip-path` (`inset`/`circle`/`ellipse`/`polygon`), the
`filter`/`backdrop-filter` function lists, `text-shadow`, `box-shadow` and
`content`.

**Cascade & computed style** (`runtime/ext_gui/style.{h,cpp}`): a small built-in
UA stylesheet, author rules sorted by `(!important, specificity, source order)`,
then inline `style=""` (highest specificity, but non-`!important` inline loses to
`!important`), plus CSS inheritance of the text properties. `@media` rules are
filtered against the current viewport on every restyle, so a resize switches
them. Relative lengths are kept unresolved until layout, except `font-size`
(resolved against the *parent* font size) and `line-height`.

`::before`/`::after` styles are computed after their originating element: the
selector's last compound must name the pseudo-element, and the rest of the
selector is matched against the origin (so `.card.info::before` works). The
pseudo style inherits the origin's computed style, `content` is resolved
(strings, escapes, `attr()`, quotes), and a synthetic node is placed in a
per-restyle arena. Layout inserts its box before/after the real children;
traversal, queries, `innerHTML` and `elementIndex()` never see it, and hit
testing targets the origin element.

## Interactive DOM (M8)

`win.document` returns the document handle; element handles have stable identity
and read/write properties, attributes, traversal and geometry:

```ts
const doc = win.document;
const box = doc.querySelector("#box");
const inner = doc.querySelector("#inner");
inner.textContent = "hi";              // write
inner.classList.add("hot");
inner.style.setProperty("color", "#0f0");
inner.setAttribute("data-role", "lead");
console.log(inner.id, inner.tagName, doc.querySelector("#box") === box);

const created = doc.createElement("div");
created.textContent = "added";
box.appendChild(created);
box.removeChild(created);

// Parent/child helpers accept nodes and strings in one call.
box.append("text", doc.createElement("span"));
box.prepend(doc.createElement("b"));
box.insertAdjacentHTML("beforeend", "<i>tail</i>"); // beforebegin/afterbegin/beforeend/afterend
box.replaceChildren("fresh", doc.createElement("em"));

// Namespaces are recorded; foreign (SVG) element names keep their case.
const gradient = doc.createElementNS("http://www.w3.org/2000/svg", "svg:linearGradient");
console.log(gradient.namespaceURI, gradient.localName, gradient.prefix);

inner.addEventListener("click", (e) => console.log(e.target.id, e.currentTarget.id));
inner.click();                          // synthesise a click at the element
console.log(box.offsetWidth, box.offsetHeight);   // rounded border box
console.log(box.contains(inner));                 // descendant test
```

### Style properties, dataset, computed style and geometry

The inline `style` object is a CSSOM declaration block. Named access reads and
writes the same block as `setProperty` (`el.style.display = "none"`,
`el.style.backgroundColor = "#f00"`, camelCase mapped to the CSS spelling, and
`el.style["--accent"]` for custom properties); `setProperty(name, value,
priority)` stores `!important` when `priority` is `"important"` (any other
priority makes the call a no-op, and an empty value removes the property).
`getPropertyValue` / `getPropertyPriority` read it back, `removeProperty`
returns the previous value, `cssText` gets or replaces the whole block,
`style.length` / `style.item(i)` enumerate it, and an unset property answers
`""`.

`el.dataset` is a live `DOMStringMap` over the element's `data-*` attributes:
`dataset.fooBar` reads `data-foo-bar`, assignment writes the attribute, `delete`
removes it, and `Object.keys(dataset)` / `for...in` list only the data keys.

`getComputedStyle(el, pseudoElt)` answers from the **cascade** — the UA sheet,
author rules, inheritance and the inline declarations — rather than echoing the
inline block, so a stylesheet rule is visible. It exposes `getPropertyValue`,
`getPropertyPriority` and named properties (`cs.width`, `cs.backgroundColor`,
`cs["--accent"]`). Pass `"::before"`/`"::after"` to read generated content; any
other pseudo-element answers empty values. Every read flushes a pending restyle,
so a handle created early stays live after later mutations.

`getBoundingClientRect()` carries the legacy `top` / `right` / `bottom` / `left`
members next to `x` / `y` / `width` / `height`, and `document.title = …` writes
the document's `<title>` (creating the element, and a `<head>`, when the page
has none).

Mutations mark the document dirty; the engine restyles + relayouts lazily (before
the next read or frame). `inner = …`/`innerHTML = …` do **not** run scripts.
Element events support capture and bubble phases, `stopPropagation`, `once`,
`passive` and `signal` (an `AbortController`'s signal removes the listener), and
bubble up to `document`/`window`. `dispatchEvent` dispatches the same event
object and fills in `target`/`currentTarget`/`eventPhase`/`composedPath()`. The
legacy `win.on(type, fn)` payload keeps its **string** `e.target`
(`div#id.class`); the element `Event.target` is a handle whose descriptor
matches the same string.

`element.focus()` scrolls the element into view (pass `{ preventScroll: true }`
to keep the page offset), and `document.hasFocus()` reports the window state
toggled by `window.focus()` / `window.blur()`.

### Node lifetime

A mutation decides whether the nodes it removes can still be reached, and the
engine releases exactly those it cannot:

| Mutation | Removed nodes |
| --- | --- |
| `innerHTML = …`, `inner = …`, `textContent = …`, `replaceChild` | **released** — the script has no way to refer to them again |
| `removeChild`, `remove` | **kept** in the document's detached pool — the DOM keeps a removed node alive and usable, so it can be re-attached |

Released nodes free their C++ subtree as well as their handle, so the counters
from `win.stats()` (`detachedNodes`, `handles`, `handleSlots`) return to their
previous values. That is what makes the usual game pattern — rewriting a sprite
with `innerHTML` every frame — run in constant memory; before, every replaced
subtree stayed pooled forever and a loop writing ten nodes per frame leaked
several megabytes per second.

The one case the engine cannot see is a node the script creates and then drops
without ever attaching it: `document.createElement("div")` parks the node in the
pool *and* keeps its handle reachable, because the mark-sweep collector has no
weak references, so the engine cannot tell that the script threw the handle
away. A loop that creates elements without attaching them therefore grows the
pool. Reuse the elements (or build the content with `innerHTML`, which releases
what it replaces) instead of creating fresh ones per frame.

## AOT scripts (M9)

Import an `.html` file that contains inline `<script lang="ts">` bodies; the
loader compiles each body into a native function and `win.loadHTML(page)` runs
them once the document is parsed (no JavaScript engine, no runtime `eval`):

```ts
import { createWindow, run } from "gui";
import page from "./page.html";

const win = createWindow({ title: "counter", width: 320, height: 240 });
win.loadHTML(page);
run();
```

```html
<button id="b">0</button>
<script lang="ts">
  const b = document.getElementById("b");
  let n = 0;
  b.addEventListener("click", () => { b.textContent = String(++n); });
</script>
```

The two parameters (`window`, `document`) are ordinary function arguments, so
script locals need no global-object machinery. External scripts work too:
`<script src="./counter.ts">` is read at compile time, its imports are hoisted
(rewritten to resolve from the HTML file) and its body is wrapped the same way —
so a script module can `import` helpers and still see `document`. Scripts are
**compile-time assets**: HTML created at runtime (`innerHTML`, fetched over the
network) never executes, and `src` URLs (`https://…`, `data:…`) are ignored.
Everything runs in document order after parsing (effectively deferred). See
`doc/gui-scripts.md` for the full design.

