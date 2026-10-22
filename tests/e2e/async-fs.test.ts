/**
 * End-to-end tests for the asynchronous `fs/promises` implementation.
 *
 * The promise must settle on the event loop, not inside the call, and a worker
 * running a blocking syscall must not stop timers from firing. The FIFO case
 * makes the blocking part deterministic: opening a FIFO for reading blocks until
 * a writer arrives, which happens only after the timer has had time to fire.
 */

import { execFileSync, spawn } from "node:child_process";
import { join } from "node:path";
import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

const itPosix = process.platform === "win32" ? it.skip : it;

describeE2E("async fs/promises", (harness) => {
  it("settles promises on the event loop instead of inside the call", () => {
    const target = JSON.stringify(join(harness.workdir, `settle_${Math.random().toString(36).slice(2)}.txt`));
    const output = harness.runProgram(
      `
      import { promises as fsp } from "node:fs";

      async function main(): Promise<void> {
        const target = ${target};
        await fsp.writeFile(target, "hello");

        let settled = false;
        const pending = fsp.stat(target);
        pending.then(() => { settled = true; });
        console.log("immediate", settled);
        await pending;
        console.log("later", settled);
        await fsp.unlink(target);
      }
      main();
      `,
      { extensions: true },
    );
    expect(output).toBe("immediate false\nlater true");
  });

  itPosix("fires timers while a read is blocked on a FIFO", () => {
    const suffix = Math.random().toString(36).slice(2);
    const fifo = join(harness.workdir, `pipe_${suffix}`);
    const marker = join(harness.workdir, `pipe_ready_${suffix}`);
    execFileSync("mkfifo", [fifo]);
    const quote = (path: string): string => `'${path.replace(/'/g, "'\\''")}'`;
    // `runProgram` compiles before it runs, so the writer must wait for the
    // compiled program to signal that it is about to read. It then waits long
    // enough for the 10ms timer to be due before unblocking the reader.
    const writer = spawn(
      "sh",
      ["-c", `while [ ! -f ${quote(marker)} ]; do sleep 0.01; done; sleep 0.5; printf hello > ${quote(fifo)}`],
      { stdio: "ignore" },
    );
    try {
      const output = harness.runProgram(
        `
        import { promises as fsp } from "node:fs";

        async function main(): Promise<void> {
          await fsp.writeFile(${JSON.stringify(marker)}, "ready");
          const order: string[] = [];
          const pending = fsp.readFile(${JSON.stringify(fifo)}, "utf8").then((text) => order.push("read:" + text));
          setTimeout(() => order.push("timer"), 10);
          await pending;
          await new Promise<void>((resolve) => setTimeout(resolve, 10));
          console.log(order.join(","));
        }
        main();
        `,
        { extensions: true, timeout: 60000 },
      );
      expect(output).toBe("timer,read:hello");
    } finally {
      writer.kill();
    }
  });

  it("runs independent file operations concurrently", () => {
    const base = JSON.stringify(join(harness.workdir, `concurrent_${Math.random().toString(36).slice(2)}`));
    const output = harness.runProgram(
      `
      import { promises as fsp } from "node:fs";

      async function main(): Promise<void> {
        const base = ${base};
        await fsp.mkdir(base, { recursive: true });
        await Promise.all([
          fsp.writeFile(base + "/a.txt", "alpha"),
          fsp.writeFile(base + "/b.txt", "beta"),
          fsp.writeFile(base + "/c.txt", "gamma"),
        ]);
        const [a, b, c, listing] = await Promise.all([
          fsp.readFile(base + "/a.txt", "utf8"),
          fsp.readFile(base + "/b.txt", "utf8"),
          fsp.readFile(base + "/c.txt", "utf8"),
          fsp.readdir(base),
        ]);
        console.log(a, b, c, listing.length);
        const entries = await fsp.readdir(base, { withFileTypes: true });
        console.log(entries.length, entries[0].isFile());
        await fsp.cp(base + "/a.txt", base + "/a-copy.txt");
        console.log(await fsp.readFile(base + "/a-copy.txt", "utf8"));
        await fsp.rm(base, { recursive: true, force: true });
      }
      main();
      `,
      { extensions: true },
    );
    expect(output).toBe("alpha beta gamma 3\n3 true\nalpha");
  });

  it("does not leave a pending job unrooted across a collection", () => {
    const target = JSON.stringify(join(harness.workdir, `fire_forget_${Math.random().toString(36).slice(2)}.txt`));
    // The promise returned by the fire-and-forget call is reachable only through
    // the job's root slot; a stress collection between submission and completion
    // would otherwise sweep it before the worker settles it.
    const output = harness.runProgram(
      `
      import { promises as fsp } from "node:fs";

      async function main(): Promise<void> {
        const target = ${target};
        await fsp.writeFile(target, "kept");
        fsp.readFile(target, "utf8");
        console.log("scheduled");
        await new Promise<void>((resolve) => setTimeout(resolve, 50));
        console.log("survived");
        await fsp.unlink(target);
      }
      main();
      `,
      { extensions: true, env: { XT_GC_THRESHOLD: "1" } },
    );
    expect(output).toBe("scheduled\nsurvived");
  });
});
