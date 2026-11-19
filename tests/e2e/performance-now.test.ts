/**
 * `performance.now()` is a DOMHighResTimeStamp relative to the *time origin*, not
 * to machine boot. The Windows implementation used to return the raw QPC
 * counter, so a process started six days into an uptime printed
 * `performance.now() == 521,000,000` on its first call: every delta was still
 * right (three.js kept animating), but `now() / 1000` — `clock.elapsedTime`, a
 * timestamp printed to a log — started at six days.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("performance.now", (harness) => {
  it("counts from the program's own start and advances monotonically", () => {
    const stdout = harness.runProgram(`
      const first = performance.now();
      const second = performance.now();
      console.log("first=" + (first >= 0 && first < 60000));
      console.log("monotonic=" + (second >= first));
      const wall = Date.now();
      console.log("wall=" + (wall > 1600000000000 && Math.abs(wall - Date.now()) < 60000));
    `);
    expect(stdout).toContain("first=true");
    expect(stdout).toContain("monotonic=true");
    expect(stdout).toContain("wall=true");
  });

  it("measures an elapsed interval across a timer", () => {
    const stdout = harness.runProgram(`
      const started = performance.now();
      setTimeout(() => {
        const elapsed = performance.now() - started;
        console.log("elapsed=" + (elapsed >= 40 && elapsed < 5000));
      }, 80);
    `);
    expect(stdout).toContain("elapsed=true");
  });
});
