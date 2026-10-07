// Minimal application-icon example. The icon and app metadata come from
// `xbintsc.config.json` next to this file, so the command line stays short:
//
//   xbintsc run examples/gui/icon/app.ts
//
// It is equivalent to:
//
//   xbintsc run examples/gui/icon/app.ts --ext gui \
//     --icon examples/gui/icon/logo.png --app-name IconDemo
//
// Set XT_GUI_AUTOCLOSE_MS to auto-close after a delay (used by tests).

import { createWindow, run } from "gui";

const win = createWindow({ title: "IconDemo", width: 480, height: 320 });
win.on("ready", () => console.log("icon demo ready"));
win.setBackground("#14161c");
run();
