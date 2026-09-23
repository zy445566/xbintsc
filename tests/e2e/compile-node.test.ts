/**
 * End-to-end tests for programs that use the Node compatibility extension:
 * `fs`, `path`, `os`, `process` and `child_process`. Each program is compiled
 * to a native binary and executed through the shared {@link describeE2E}
 * harness, so the suite is skipped when no clang-compatible compiler is found.
 */

import { readFileSync, writeFileSync } from "node:fs";
import { gunzipSync } from "node:zlib";
import { join } from "node:path";
import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end compilation (node extension)", (harness) => {
  const { runProgram, runProgramFull } = harness;

  it("reads files through the node extension", () => {
    const dataPath = join(harness.workdir, "data.txt");
    writeFileSync(dataPath, "from a file");
    const source = `import { readFileSync } from "fs";\nconsole.log(readFileSync(${JSON.stringify(dataPath)}));`;
    expect(runProgram(source, { extensions: true })).toBe("from a file");
  });

  it("writes and manages files through the node extension", () => {
    const base = join(harness.workdir, `fsops_${Math.random().toString(36).slice(2)}`);
    const source = `
      import { appendFileSync, copyFileSync, existsSync, mkdirSync, readdirSync, readFileSync, renameSync, rmSync, statSync, writeFileSync } from "fs";
      const base = ${JSON.stringify(base)};
      mkdirSync(base + "/a/b", { recursive: true });
      writeFileSync(base + "/a/b/file.txt", "hello");
      appendFileSync(base + "/a/b/file.txt", " world");
      console.log(readFileSync(base + "/a/b/file.txt"));
      console.log(readFileSync(base + "/a/b/file.txt", "hex"));
      console.log(existsSync(base + "/a/b/file.txt"), existsSync(base + "/missing"));
      copyFileSync(base + "/a/b/file.txt", base + "/a/copy.txt");
      renameSync(base + "/a/copy.txt", base + "/a/moved.txt");
      console.log(readdirSync(base + "/a").sort().join(","));
      const entries = readdirSync(base + "/a", { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name));
      console.log(entries.map((entry) => entry.name + ":" + entry.isDirectory()).join(","));
      const info = statSync(base + "/a/b/file.txt");
      console.log(info.size, info.isFile(), info.isDirectory());
      rmSync(base + "/a", { recursive: true });
      console.log(existsSync(base + "/a"));
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      "hello world\n68656c6c6f20776f726c64\ntrue false\nb,moved.txt\nb:true,moved.txt:false\n11 true false\nfalse",
    );
  });

  it("supports hex and base64 encodings for fs reads and writes", () => {
    const base = harness.workdir;
    const source = `
      import { readFileSync, rmSync, writeFileSync } from "fs";
      const base = ${JSON.stringify(base)};
      const hexPath = base + "/encoding_${Math.random().toString(36).slice(2)}.hex.txt";
      const b64Path = base + "/encoding_${Math.random().toString(36).slice(2)}.b64.txt";
      writeFileSync(hexPath, "68656c6c6f", "hex");
      writeFileSync(b64Path, "aGVsbG8=", "base64");
      console.log(readFileSync(hexPath), readFileSync(b64Path));
      console.log(readFileSync(hexPath, "base64"));
      rmSync(hexPath);
      rmSync(b64Path);
    `;
    expect(runProgram(source, { extensions: true })).toBe("hello hello\naGVsbG8=");
  });

  it("provides the path module", () => {
    const source = `
      console.log(path.join("a", "b", "..", "c"));
      console.log(path.resolve("/tmp", "a", "..", "b"));
      console.log(path.dirname("/x/y/z.txt"), path.basename("/x/y/z.txt"), path.extname("/x/y/z.txt"));
      console.log(path.basename("/foo/bar.test.js", ".js"));
      console.log(path.normalize("/a//b/./c/../d"));
      console.log(path.isAbsolute("/a"), path.isAbsolute("a"));
      console.log(path.relative("/a/b/c", "/a/d"));
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      "a/c\n/tmp/b\n/x/y z.txt .txt\nbar.test\n/a/b/d\ntrue false\n../../d",
    );
  });

  it("resolves native Windows paths (drive letters and backslashes)", () => {
    // The C path module keeps POSIX semantics on POSIX hosts; the backslash and
    // drive-letter handling only exists on Windows, so this case is skipped
    // elsewhere. Results are reported with the canonical `/` separator, which
    // Windows accepts, so the compiler can treat paths uniformly.
    if (process.platform !== "win32") return;
    const source = `
      console.log(path.isAbsolute("C:\\\\a"), path.isAbsolute("C:"));
      console.log(path.dirname("C:\\\\a\\\\b\\\\c.ts"));
      console.log(path.resolve("C:\\\\a\\\\b", "..\\\\c"));
      console.log(path.normalize("C:\\\\a\\\\.\\\\b\\\\..\\\\c"));
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      "true false\nC:\\a\\b\nC:/a/c\nC:/a/c",
    );
  });

  it("imports node modules by specifier", () => {
    const source = `
      import { readFileSync } from "node:fs";
      import path from "path";
      import { join as joinPath, basename } from "path";
      console.log(path.join("a", "b"));
      console.log(joinPath("a", "b", "..", "c"), basename("/x/y.txt"));
    `;
    expect(runProgram(source, { extensions: true })).toBe("a/b\na/c y.txt");
  });

  it("supports default and namespace imports of node: modules", () => {
    const dataPath = join(harness.workdir, `default-import-${Math.random().toString(36).slice(2)}.txt`);
    writeFileSync(dataPath, "via default import");
    const source = `
      import fs from "node:fs";
      import * as fs2 from "node:fs";
      console.log(fs.existsSync(${JSON.stringify(dataPath)}), fs2.existsSync(${JSON.stringify(dataPath)}));
      console.log(fs.readFileSync(${JSON.stringify(dataPath)}));
    `;
    expect(runProgram(source, { extensions: true })).toBe("true true\nvia default import");
  });

  it("provides the os and process modules", () => {
    const source = `
      console.log(os.platform().length > 0, os.arch().length > 0, os.homedir().length > 0, os.tmpdir().length > 0);
      console.log(process.platform.length > 0, process.arch.length > 0, process.pid > 0, process.cwd().length > 0);
      console.log(Object.keys(process.env).length > 0, process.argv.length >= 1);
    `;
    expect(runProgram(source, { extensions: true })).toBe("true true true true\ntrue true true true\ntrue true");
  });

  it("streams child output for spawnSync with stdio inherit", () => {
    // The child inherits the program's stdout (the pipe the test reads), so its
    // output must reach `stdout` directly rather than being captured and dropped.
    const source = `
      import { spawnSync } from "node:child_process";
      let isChild = false;
      for (const arg of process.argv) {
        if (arg === "xb-child") isChild = true;
      }
      if (isChild) {
        console.log("hello-from-child");
      } else {
        const result = spawnSync(process.argv[0], ["xb-child"], { stdio: "inherit" });
        console.log("status", result.status);
      }
    `;
    const { stdout, status } = runProgramFull(source, { extensions: true });
    expect(status).toBe(0);
    expect(stdout).toContain("hello-from-child");
    expect(stdout).toContain("status 0");
  });

  it("hashes with SHA-1 and the streaming crypto API", () => {
    const source = `
      import { createHash } from "node:crypto";
      const hasher = createHash("sha1");
      hasher.setEncoding("hex");
      hasher.write("abc");
      hasher.end();
      console.log(hasher.read());
      console.log(createHash("sha256").update("abc").digest("hex"));
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      "a9993e364706816aba3e25717850c26c9cd0d89d\nba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
    );
  });

  it("runs node:test assertions through node:assert", () => {
    const source = `
      import assert from "node:assert";
      import test from "node:test";
      test("strict and deep equality", () => {
        assert.strictEqual(1, 1);
        assert.deepStrictEqual({ a: [1, 2] }, { a: [1, 2] });
        assert.throws(() => { throw new Error("boom"); });
      });
    `;
    const result = runProgramFull(source, { extensions: true });
    expect(result.status).toBe(0);
    expect(result.stdout).toContain("ok 1 - strict and deep equality");
  });

  it("exits non-zero when a node:test assertion fails", () => {
    const source = `
      import assert from "node:assert";
      import test from "node:test";
      test("fails", () => { assert.strictEqual(1, 2); });
    `;
    const result = runProgramFull(source, { extensions: true });
    expect(result.status).toBe(1);
    expect(result.stdout).toContain("not ok 1 - fails");
  });

  it("pipes a file through gzip with stream/promises", () => {
    const sourcePath = join(harness.workdir, `pipe-src-${Math.random().toString(36).slice(2)}.txt`);
    const destPath = join(harness.workdir, `pipe-dest-${Math.random().toString(36).slice(2)}.gz`);
    writeFileSync(sourcePath, "hello gzip");
    const source = `
      import { createReadStream, createWriteStream } from "node:fs";
      import { pipeline } from "node:stream/promises";
      import { createGzip } from "node:zlib";
      await pipeline(
        createReadStream(${JSON.stringify(sourcePath)}),
        createGzip(),
        createWriteStream(${JSON.stringify(destPath)}),
      );
    `;
    expect(runProgram(source, { extensions: true })).toBe("");
    expect(gunzipSync(readFileSync(destPath)).toString("utf8")).toBe("hello gzip");
  });

  it("runs a worker_threads worker and relays its message", () => {
    const source = `
      import { Worker, isMainThread, workerData, parentPort } from "node:worker_threads";
      if (isMainThread) {
        const worker = new Worker(import.meta.filename, { workerData: "some data" });
        worker.on("message", (msg) => console.log("Reply from Thread:", msg));
      } else {
        parentPort.postMessage(btoa(workerData.toUpperCase()));
      }
    `;
    expect(runProgram(source, { extensions: true })).toBe("Reply from Thread: U09NRSBEQVRB");
  });
});
