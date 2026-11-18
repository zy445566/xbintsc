/**
 * The hang watchdog: a compiled program that spins prints the JavaScript frames
 * it is stuck in.
 *
 * This is the one diagnostic that has to run a program that never returns, so
 * the case compiles a real binary, sets `XT_STACK_WATCHDOG_MS` and lets the
 * harness kill it after a few seconds. Before the watchdog existed the only
 * evidence was "it hangs" — no stack, no line, nothing.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("hang watchdog", (harness) => {
  it("prints the shadow stack when no function has run for the threshold", () => {
    const source = `
      function spin(): void {
        let i = 0;
        while (true) {
          i = i + 1;
          if (i < 0) break;
        }
      }
      console.log("before");
      spin();
    `;
    const result = harness.runProgramFull(source, {
      env: { XT_STACK_WATCHDOG_MS: "600" },
      timeout: 5000,
    });
    /* The process is killed by the timeout, so there is no exit status to
     * assert (and its buffered stdout may be lost) — the evidence is the dump
     * on stderr, which the watchdog flushes itself. */
    expect(result.stderr).toContain("no JavaScript call for");
    expect(result.stderr).toContain("shadow stack:");
    expect(result.stderr).toContain("at spin");
  });

  it("stays quiet when the variable is unset", () => {
    const source = `
      let total = 0;
      for (let i = 0; i < 1000; i++) total = total + i;
      console.log(total);
    `;
    const result = harness.runProgramFull(source);
    expect(result.status).toBe(0);
    expect(result.stderr).toBe("");
  });
});
