/**
 * End-to-end tests for the `process` object: the event-emitter surface, the exit
 * status and the three standard streams. Each program is compiled to a native
 * binary and executed through the shared {@link describeE2E} harness, so the
 * suite is skipped when no clang-compatible compiler is found.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end compilation (process streams)", (harness) => {
  const { runProgramFull } = harness;

  it("exposes process.versions, nextTick and an exit listener with exitCode", () => {
    const source = `
      console.log(typeof process.versions, process.versions.node, process.versions.bun);
      process.nextTick(() => console.log("tick"));
      console.log("sync");
      process.on("exit", (code: number) => console.log("exit", code));
      process.exitCode = 7;
    `;
    const { stdout, status } = runProgramFull(source, { extensions: true });
    expect(stdout.trimEnd()).toBe("object 0.0.0 undefined\nsync\ntick\nexit 7");
    // `main` returns the status `process.exitCode` asked for.
    expect(status).toBe(7);
  });

  it("runs the exit listeners when process.exit is called", () => {
    const source = `
      process.on("exit", (code: number) => console.log("bye", code));
      process.exit(3);
      console.log("unreachable");
    `;
    const { stdout, status } = runProgramFull(source, { extensions: true });
    expect(stdout.trimEnd()).toBe("bye 3");
    expect(status).toBe(3);
  });

  it("models process.stdout and process.stdin as streams", () => {
    const source = `
      const stdout: any = process.stdout;
      const stdin: any = process.stdin;
      console.log(typeof stdout.write, typeof stdout.on, typeof stdout.isTTY, typeof stdout.columns);
      console.log(typeof stdin.on, typeof stdin.setRawMode, typeof stdin.resume, typeof stdin.isTTY);
      stdin.setEncoding("utf8");
      stdin.setRawMode(false);
      stdin.on("data", (chunk: string) => console.log("data:" + chunk.trim()));
      stdin.on("end", () => console.log("end"));
    `;
    const { stdout, status } = runProgramFull(source, { extensions: true, input: "hello\n" });
    expect(stdout.trimEnd()).toBe(
      "function function boolean number\nfunction function function boolean\ndata:hello\nend",
    );
    expect(status).toBe(0);
  });
});
