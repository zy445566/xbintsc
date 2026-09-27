// Minimal GUI example: opens a GPU-backed window and reacts to its "ready"
// event. Build and run with:
//
//   xbintsc run examples/gui/hello.ts --ext gui
//
// The window stays open until it is closed (or the process is interrupted).
// Set XT_GUI_AUTOCLOSE_MS to auto-close after a delay (used by tests):
//
//   XT_GUI_AUTOCLOSE_MS=500 xbintsc run examples/gui/hello.ts --ext gui

import { createWindow, run } from "gui";

const win = createWindow({ title: "xbintsc gui", width: 640, height: 480 });

win.on("ready", () => {
  console.log("gui ready");
});

win.on("close", () => {
  console.log("gui closed");
});

win.setBackground("#14161c");
win.loadHTML("<h1>Hello from xbintsc</h1>");

run();
console.log("gui exited");
