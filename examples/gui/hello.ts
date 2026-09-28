// Minimal GUI example: opens a GPU-backed window, loads an HTML/CSS document
// and reads back computed styles. The document lives in `hello.html` next to
// this file and carries an inline `<script lang="ts">` that wires up a
// reactive counter (no JS engine involved — the body is AOT-compiled). Build
// and run with:
//
//   xbintsc run examples/gui/hello.ts --ext gui
//
// Set XT_GUI_AUTOCLOSE_MS to auto-close after a delay (used by tests):
//
//   XT_GUI_AUTOCLOSE_MS=500 xbintsc run examples/gui/hello.ts --ext gui

import { createWindow, run } from "gui";
// The `.html` asset loader compiles the inline `<script>` into native code and
// replaces it with a marker; `loadHTML` runs it once the document is parsed.
import page from "./hello.html";

const win = createWindow({ title: "xbintsc gui", width: 640, height: 480 });

win.on("ready", () => {
  console.log("gui ready");
  console.log("card display   =", win.computedStyle(".card", "display"));
  console.log("card width     =", win.computedStyle(".card", "width"));
  console.log("card bg        =", win.computedStyle(".card", "background-color"));
  console.log("h1 font-size   =", win.computedStyle(".card h1", "font-size"));
  console.log("title weight   =", win.computedStyle("#title", "font-weight"));
  console.log("row margin-top =", win.computedStyle(".card > .row", "margin-top"));
  console.log("body margin    =", win.computedStyle("body", "margin-top"));
  console.log("card count     =", win.queryCount(".card"));
  const card = win.getBoundingClientRect(".card");
  console.log("card rect      =", card.x, card.y, card.width, card.height);
  console.log("paint shapes   =", win.paintCount());
  console.log(win.paintList().split("\n").filter(Boolean).slice(0, 3).join("\n"));

  // The counter is entirely driven by the page's inline script: clicking a
  // button dispatches an element event that the script handler reacts to.
  const count = win.document.getElementById("count");
  const inc = win.document.getElementById("inc");
  const dec = win.document.getElementById("dec");
  const reset = win.document.getElementById("reset");
  console.log("counter init   =", count.textContent, "class=", count.className);
  inc.click();
  inc.click();
  inc.click();
  dec.click();
  console.log("counter +3 -1  =", count.textContent, "class=", count.className);
  console.log("counter color  =", win.computedStyle("#count", "color"));
  reset.click();
  console.log("counter reset  =", count.textContent, "class=", count.className);
});

// Input events carry a payload (coordinates + the deepest element, as a
// CSS-like descriptor). Pointer input also drives `:hover` / `:focus`.
win.on("mousemove", (event: any) => console.log("hover:", event.target));
win.on("click", (event: any) => console.log("click:", event.x, event.y, event.target));
win.on("keydown", (event: any) => console.log("key:", event.key, "ctrl=", event.ctrl));

win.on("close", () => {
  console.log("gui closed");
});

win.setBackground("#14161c");
win.loadHTML(page);

run();
console.log("gui exited");
