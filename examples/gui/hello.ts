// Minimal GUI example: opens a GPU-backed window, loads an HTML/CSS document
// and reads back computed styles. Build and run with:
//
//   xbintsc run examples/gui/hello.ts --ext gui
//
// Set XT_GUI_AUTOCLOSE_MS to auto-close after a delay (used by tests):
//
//   XT_GUI_AUTOCLOSE_MS=500 xbintsc run examples/gui/hello.ts --ext gui

import { createWindow, run } from "gui";

const INDEX_HTML = `
<html>
  <head>
    <style>
      body { margin: 0; background-color: #fafafa; font-size: 16px; }
      .card {
        display: flex;
        flex-direction: column;
        width: 50%;
        padding: 8px;
        background: #ffffff;
        border-radius: 6px;
      }
      .card h1 { font-size: 1.5em; color: rgb(20, 22, 28); }
      #title { font-weight: bold; }
      .card > .row { margin-top: 4px; }
    </style>
  </head>
  <body>
    <div class="card" id="card">
      <h1 id="title">Hello from xbintsc</h1>
      <div class="row">a flex row</div>
    </div>
  </body>
</html>
`;

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
});

win.on("close", () => {
  console.log("gui closed");
});

win.setBackground("#14161c");
win.loadHTML(INDEX_HTML);

run();
console.log("gui exited");
