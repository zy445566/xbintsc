/**
 * GUI e2e: the optional `shaping` link feature installs the HarfBuzz + FreeType
 * backend instead of the default stb_truetype one.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — optional shaping backend", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("shapes and measures CJK text with the shaping feature", () => {
    const { value } = harness.compileAndRun(
      "shaping",
      `
      import { createWindow, run } from "gui";

      const win = createWindow({ title: "shaping", width: 400, height: 200 });
      win.on("ready", () => {
        const fm = win.fontMetrics(16);
        console.log("ready=" + fm.ready);
        console.log("m=" + win.measureText("中文测试", 16));
        console.log("has-text=" + win.paintList().includes("text x="));
        win.close();
      });
      win.loadHTML("<html><body style='font-size:20px'>中文测试</body></html>");
      run();
      `,
      { features: ["shaping"], console: true },
    );

    expect(value("ready")).toBe("true");
    expect(Number(value("m"))).toBeGreaterThan(0);
    expect(value("has-text")).toBe("true");
  });

  it("can disable shaping even for non-ASCII text", () => {
    const { value } = harness.compileAndRun(
      "shaping-disabled",
      `
      import { createWindow, run } from "gui";

      const win = createWindow({ title: "shaping-disabled", width: 400, height: 200 });
      win.on("ready", () => {
        console.log("m=" + win.measureText("中文测试", 16));
        win.close();
      });
      win.loadHTML("<html><body style='font-size:20px'>中文测试</body></html>");
      run();
      `,
      { noFeatures: ["shaping"], console: true },
    );

    // The stb_truetype fallback still measures CJK when the system TrueType
    // face covers it; the point is that `--no-feature shaping` does not fail.
    expect(Number(value("m"))).toBeGreaterThan(0);
  });
});
