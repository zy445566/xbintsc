/**
 * End-to-end test for `XT_GUI_URL`.
 *
 * A compiled program has no browser address bar, so the query string and hash
 * a page addresses itself with (`?bench`, `?q=low`, `#shot`) need a host-side
 * hook. `XT_GUI_URL` supplies one: a bare `?query`, a `#hash` or a
 * `path?query#hash`; the origin stays `xbintsc://localhost` and the unset case
 * keeps the historical empty URL.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

const URL_PAGE = `
  import { createWindow, run } from "gui";

  const win = createWindow({ title: "url", width: 200, height: 150 });
  win.loadHTML("<html><body style='margin:0'></body></html>");
  win.on("ready", () => {
    console.log("search=" + window.location.search);
    console.log("hash=" + window.location.hash);
    console.log("pathname=" + window.location.pathname);
    win.close();
  });
  run();
`;

describe.skipIf(!guiAvailable)("gui extension — initial URL", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("exposes the query and hash supplied by XT_GUI_URL", () => {
    const result = harness.compileAndRun("initial-url", URL_PAGE, {
      autocloseMs: 4000,
      console: true,
      env: { XT_GUI_URL: "?bench=1&q=low#frag" },
    });
    expect(result.value("search")).toBe("?bench=1&q=low");
    expect(result.value("hash")).toBe("#frag");
    expect(result.value("pathname")).toBe("/");
  }, 120000);

  it("keeps the empty URL when XT_GUI_URL is unset", () => {
    const result = harness.compileAndRun("initial-url-empty", URL_PAGE, {
      autocloseMs: 4000,
      console: true,
    });
    expect(result.value("search")).toBe("");
    expect(result.value("hash")).toBe("");
    expect(result.value("pathname")).toBe("/");
  }, 120000);
});
