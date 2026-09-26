/**
 * Runtime-coverage tests for `url`, `os`, `process` and `child_process`.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

const TIMEOUT = 120000;

describeE2E(
  "runtime coverage: system",
  (h) => {
    it("converts between file paths and file URLs", () => {
      const source = `
        import { pathToFileURL, fileURLToPath } from "url";
        import url from "url";

        const u = pathToFileURL("/tmp/a b");
        console.log(u.protocol, u.href.startsWith("file://"), u.href.includes("a b"));
        console.log(fileURLToPath("file:///tmp/a%20b"));
        console.log(fileURLToPath("file://localhost/tmp/c"));
        console.log(fileURLToPath("/tmp/plain"));
        console.log(url.pathToFileURL("/tmp/x").href.startsWith("file://"));
      `;
      const stdout = h.runProgram(source, { extensions: true, timeout: TIMEOUT });
      expect(stdout.split("\n")).toEqual([
        "file: true true",
        "/tmp/a b",
        "/tmp/c",
        "/tmp/plain",
        "true",
      ]);
    });

    it("reads operating-system and process information", () => {
      const source = `
        import os from "os";
        import * as os2 from "node:os";
        import process from "process";
        import * as proc from "node:process";

        console.log(os.platform().length > 0, os.type().length > 0, os.arch().length > 0, os.endianness().length > 0);
        console.log(os.homedir().length > 0, os.tmpdir().length > 0, os.hostname().length > 0, os.release().length > 0);
        console.log(os.totalmem() > 0, os.freemem() > 0, os.cpus().length > 0, os2.cpus()[0].model.length > 0);
        console.log(os2.platform().length > 0);
        console.log(process.platform.length > 0, process.arch.length > 0, process.pid > 0, process.ppid >= 0, process.version.length > 0);
        console.log(process.cwd().length > 0, process.uptime() >= 0, process.hrtime().length === 2);
        console.log(process.getuid() >= 0, process.argv.length >= 1, Object.keys(process.env).length > 0);
        process.stdout.write("stdout-write\\n");
        process.stderr.write("stderr-write\\n");
        console.log(proc.cwd().length > 0, proc.platform.length > 0, proc.getuid() >= 0);
      `;
      const result = h.runProgramFull(source, { extensions: true, timeout: TIMEOUT });
      expect(result.status).toBe(0);
      expect(result.stdout.trim().split("\n")).toEqual([
        "true true true true",
        "true true true true",
        "true true true true",
        "true",
        "true true true true true",
        "true true true",
        "true true true",
        "stdout-write",
        "true true true",
      ]);
      expect(result.stderr.trim()).toBe("stderr-write");
    });

    it("spawns child processes synchronously", () => {
      const source = `
        import { spawnSync } from "node:child_process";

        let isChild = false;
        for (const arg of process.argv) {
          if (arg === "xb-child-capture") isChild = true;
        }
        if (isChild) {
          console.log("from-child");
          console.error("child-err");
          console.log("x".repeat(5000));
        } else {
          const r = spawnSync(process.argv[0], ["xb-child-capture"], { encoding: "utf8" });
          console.log("status", r.status, "out", r.stdout.trim().length, "err", r.stderr.trim());
          const missing = spawnSync("definitely-not-a-real-command-xyz", [], {});
          console.log("missing", missing.status !== 0);
          const cd = spawnSync(process.argv[0], ["xb-child-capture"], { cwd: process.cwd() });
          console.log("cwd-status", cd.status);
        }
      `;
      const stdout = h.runProgram(source, { extensions: true, timeout: TIMEOUT });
      expect(stdout.split("\n")).toEqual([
        "status 0 out 5011 err child-err",
        "missing true",
        "cwd-status 0",
      ]);
    });
  },
  { tmpPrefix: "xbintsc-runtime-coverage-" },
);
