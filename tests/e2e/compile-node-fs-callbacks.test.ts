/**
 * The callback forms of `fs` (`readFile(path, cb)`, `stat(path, cb)`, …) run on
 * the asynchronous I/O pool: `callback(null, value)` on success and
 * `callback(error)` on failure. `exists` keeps Node's `callback(boolean)` shape.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end compilation (fs callbacks)", (harness) => {
  const { runProgram } = harness;

  it("reports values through callbacks, chained across operations", () => {
    const source = `
      import { writeFile, readFile, stat, realpath, exists } from "node:fs";
      import { tmpdir } from "node:os";
      import { join } from "node:path";
      const file = join(tmpdir(), "xbintsc-fs-callbacks.txt");
      writeFile(file, "hello", (error: Error | null): void => {
        if (error) return console.log("write failed");
        readFile(file, "utf8", (readError: Error | null, text: string): void => {
          if (readError) return console.log("read failed");
          stat(file, (statError: Error | null, info: { size: number }): void => {
            if (statError) return console.log("stat failed");
            realpath(file, (realError: Error | null, resolved: string): void => {
              console.log("read", text, "size", info.size);
              console.log("realpath", resolved.endsWith("xbintsc-fs-callbacks.txt"));
              exists(file, (present: boolean): void => console.log("exists", present));
            });
          });
        });
      });
    `;
    expect(runProgram(source, { extensions: "node" })).toBe(
      ["read hello size 5", "realpath true", "exists true"].join("\n"),
    );
  });

  it("passes a Node-shaped error to the callback", () => {
    const source = `
      import { readFile } from "node:fs";
      import { join } from "node:path";
      import { tmpdir } from "node:os";
      readFile(join(tmpdir(), "xbintsc-missing-file.txt"), (error: Error | null): void => {
        const shaped = error as (Error & { code?: string; syscall?: string }) | null;
        console.log("code", shaped ? shaped.code : "none", "syscall", shaped ? shaped.syscall : "none");
      });
    `;
    expect(runProgram(source, { extensions: "node" })).toBe("code ENOENT syscall open");
  });
});
