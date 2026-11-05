# Input and events (M5)

> Part of the [GUI extension](../gui.md); the engine overview, locked
> decisions and roadmap live there. The public event API is in
> [api.md](./api.md).

`LayoutTree::hitTest` returns the deepest box containing a point (probing later
siblings first so the topmost element wins), and `xt_dom_describe` turns the
element into the `div#id.class` descriptor carried by event payloads. A
transformed box maps the point through its inverse matrix first, so a rotated,
scaled or translated element is hit exactly where it is drawn — and `:hover`
follows the artwork, not the box flow left behind.

- SDL pointer/wheel/key events are routed to the owning window, hit tested, and
  delivered to the handlers registered with `on`. `mousedown` also moves focus
  and places the caret in a text field.
- Keyboard events also follow the DOM path: the target is the focused element (or
  `<body>` when nothing is focused), the event bubbles through its ancestors to
  `document`, and finally reaches `window` listeners. This is what lets a page
  written for a browser use `document.addEventListener("keydown", …)`. The
  payload carries browser `KeyboardEvent` spellings — `code` is the physical key
  (`"KeyW"`, `"ArrowUp"`, `"Space"`) and `key` the produced value (`"w"`, `"W"`,
  `" "`) — not SDL's scancode/key names.
- `:hover` matches the hovered element **and its ancestors** (so hovering a child
  lights up its parents); `:focus` matches the focused element. When either
  changes, `XtDocument::setHover`/`setFocus` recompute styles and layout and mark
  the window's geometry dirty, so the change is painted on the next frame.
- `win.hitTest(x, y)` and `win.sendEvent(type, options)` expose hit testing and
  synthetic input so the whole path is testable headlessly (the e2e suite drives
  clicks, wheels and keys without a real mouse).

## Form controls (M23)

`<input>`, `<textarea>` and `<select>` are native leaf boxes: their value,
selection and checked state live in `Node::form` (`FormState`) and are painted
by the engine, so no browser/DOM layer is involved.

```ts
const user = document.getElementById("user"); // <input id="user" value="abc">
user.focus();
user.value;                 // "abc"
user.selectionStart;        // 3
user.setSelectionRange(1, 3);
user.placeholder = "name";
user.disabled = true;
```

- **`<input>` types**: `text`, `search`, `tel`, `url`, `email`, `password`,
  `number`, `date`, `month`, `week`, `time`, `datetime-local`, `range`, `color`,
  `checkbox`, `radio`, `file`, `submit`, `reset`, `button`, `image` and
  `hidden`. Text-like types are editable; checkbox/radio paint their box and
  toggle on click (radios uncheck the rest of their `name` group); range paints a
  track and thumb and updates from a drag; color paints a swatch; submit/reset/
  button paint their label. `hidden` has no box.
- **`<textarea>`** is multi-line (`\n` breaks a line), **`<select>`** paints the
  selected `<option>`'s label and cycles the selection on click.
- **`value`** is the live value, separate from the `value` attribute (the
  default). `<textarea>` reads its default from its text content and `<select>`
  from the selected/first `<option>`. Assigning `value` moves the caret to the
  end, as browsers do.
- **`selectionStart` / `selectionEnd` / `selectionDirection`**,
  `setSelectionRange(start, end, direction)` and `select()` use UTF-16 code-unit
  offsets (the DOM convention), backed by UTF-8 storage.
- **`checked`**, `type`, `name`, `disabled`, `readOnly`, `required`,
  `placeholder`, `min`, `max`, `step`, `maxLength`, `tabIndex`, `form`,
  `options` and `selectedIndex` are live DOM properties.
- **`document.activeElement`** is the focused element, or `<body>` when nothing
  is focused. `tabindex` establishes the Tab order (`tabindex="1"` before the
  implicit `0` stops; `-1` is click-focusable but skipped by Tab); Tab and
  Shift+Tab move focus even when no text field is focused. A disabled or hidden
  control cannot be focused, is skipped by Tab and paints dimmed.
- **Text input** comes from `SDL_EVENT_TEXT_INPUT`, which carries the text the
  keyboard layout or IME actually produced. `keydown` handles the editing
  commands (Backspace/Delete/arrows/Home/End/Ctrl+A/C/X/V, with Shift extending
  the selection) and `keypress` + `input` are dispatched after an edit.
- **IME** uses `SDL_EVENT_TEXT_EDITING`: the pre-edit text is displayed at the
  caret and `compositionstart` / `compositionupdate` / `compositionend` fire;
  the commit is inserted into the value on `compositionend`.
- **Clipboard** is the system clipboard through SDL: Ctrl+C/X/V.
- **Events** fire automatically: `beforeinput` (cancelable) before a text edit,
  `input` after every value change (typing, deletion, checkbox/radio toggle,
  range drag, select), and `change` when the value is committed (a text field
  losing focus after a change, Enter in a single-line field, a checkbox/radio
  click, a range release or a select change). `input`/`change` payloads carry
  `value`; `change` also carries `checked`.
- **Forms**: a submit button (or Enter in a single-line field) fires a
  cancelable `submit` on the enclosing `<form>` with the `submitter` when one
  was clicked; there is no navigation, so preventing the event or not has the
  same visible result. A reset button or `form.reset()` fires `reset` and
  restores every control's default value/checked state. `form.submit()` and
  `form.requestSubmit(submitter?)` dispatch the same event.

`win.sendEvent` also accepts `{ type: "textInput", text }` (as if SDL delivered
that text), `{ type: "compositionstart" | "compositionupdate" | "compositionend",
data }` and printable `keydown` events. A real physical `keydown` never inserts
its US-layout `key`: the matching text-input event does, so an IME or a non-US
layout cannot double a character.

Still to do for full input: drag-selection across several lines and undo/redo.
