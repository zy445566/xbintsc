/**
 * Unhandled promise rejections are fatal, like Node's default
 * `--unhandled-rejections=throw`.
 *
 * The case that matters is the entry point: a compiled program's `main()` is
 * usually `async`, so an exception escaping it rejects *its* promise, and
 * nothing else ever looks at that promise. The process used to print nothing
 * and exit 0, so a failing program looked like a program that did nothing.
 *
 * The runtime reports whatever a handler never observed once the event loop
 * drains. A `.catch`, a `try` around an `await`, or even a handler attached
 * later from a timer counts as observed and must stay silent.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("unhandled promise rejections", (harness) => {
  it("reports a rejected async entry point and exits non-zero", () => {
    const result = harness.runProgramFull(`
      async function inner(): Promise<void> {
        throw new Error("boom");
      }
      async function main() {
        console.log("started");
        await inner();
        console.log("unreachable");
      }
      main();
    `);
    expect(result.status).toBe(1);
    expect(result.stdout.trim()).toBe("started");
    expect(result.stderr).toContain("Unhandled promise rejection");
    expect(result.stderr).toContain("Error: boom");
    expect(result.stderr).toContain("at module");
    expect(result.stdout).not.toContain("unreachable");
  });

  it("stays silent when a handler observes the rejection", () => {
    const result = harness.runProgramFull(`
      async function boom(): Promise<void> {
        throw new Error("body-throw");
      }
      async function main() {
        await boom().catch((e: Error) => console.log("catch", e.message));
        try {
          await boom();
        } catch (e) {
          console.log("try", (e as Error).message);
        }
        const late = boom();
        await new Promise<void>((resolve) =>
          setTimeout(() => {
            late.catch((e: Error) => console.log("late", e.message));
            resolve();
          }, 5),
        );
        await Promise.reject(new Error("rejected")).catch((e: Error) => console.log("reject", e.message));
        console.log("done");
      }
      main();
    `);
    expect(result.status).toBe(0);
    expect(result.stderr).toBe("");
    expect(result.stdout.trim().split("\n")).toEqual([
      "catch body-throw",
      "try body-throw",
      "late body-throw",
      "reject rejected",
      "done",
    ]);
  });

  it("reports a top-level rejection that nothing awaits", () => {
    const result = harness.runProgramFull(`
      Promise.reject(new Error("top-level"));
      setTimeout(() => console.log("timer"), 5);
    `);
    expect(result.status).toBe(1);
    // The event loop still ran to completion before the report.
    expect(result.stdout.trim()).toBe("timer");
    expect(result.stderr).toContain("Unhandled promise rejection");
    expect(result.stderr).toContain("Error: top-level");
  });

  it("prints non-Error reasons and one report per rejection", () => {
    const result = harness.runProgramFull(`
      Promise.reject("plain string");
      Promise.reject(42);
    `);
    expect(result.status).toBe(1);
    const reports = result.stderr.split("Unhandled promise rejection").length - 1;
    expect(reports).toBe(2);
    expect(result.stderr).toContain("plain string");
    expect(result.stderr).toContain("42");
  });

  it("treats an awaited rejection caught by the caller as handled", () => {
    const result = harness.runProgramFull(`
      async function boom(): Promise<void> {
        throw new Error("inner");
      }
      async function wrapper(): Promise<void> {
        await boom();
      }
      wrapper().catch((e: Error) => console.log("wrapper", e.message));
    `);
    expect(result.status).toBe(0);
    expect(result.stderr).toBe("");
    expect(result.stdout.trim()).toBe("wrapper inner");
  });
});
