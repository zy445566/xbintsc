// 用户认证示例 (register / login / logout) — a native-window "form" demo for
// the `gui` extension: register, sign in and sign out, with user records stored
// in **IndexedDB** and the login state kept in **sessionStorage**.
//
// The whole UI lives in `index.html` (next to this file): a `<form>` with
// username/password fields and three buttons, plus an inline `<script lang="ts">`
// that the `.html` asset loader compiles ahead of time — there is no JS engine
// in the binary. This entry file only opens the window, loads the document and
// drives the main loop.
//
//   npx tsx src/cli/main.ts run examples/gui/auth/main.ts
//
// `xbintsc.config.json` enables the `gui` extension and points the SQLite-backed
// storage at `data/auth.db` (a file backend), so registered users survive a
// restart while the sessionStorage login state does not — exactly the browser
// semantics. Set XT_GUI_AUTOCLOSE_MS to auto-close the window for a smoke run.

import { createWindow, run } from "gui";
import page from "./index.html";

const win = createWindow({
  title: "用户注册 / 登录 / 注销",
  width: 420,
  height: 500,
});

win.setBackground("#eef1f6");
win.on("close", () => console.log("gui closed"));

win.loadHTML(page);

run();
console.log("gui exited");
