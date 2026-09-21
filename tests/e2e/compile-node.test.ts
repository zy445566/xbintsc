/**
 * End-to-end tests for programs that use the Node compatibility extension:
 * `fs`, `path`, `os`, `process` and `child_process`. Each program is compiled
 * to a native binary and executed through the shared {@link describeE2E}
 * harness, so the suite is skipped when no clang-compatible compiler is found.
 */

import { writeFileSync } from "node:fs";
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
      const info = statSync(base + "/a/b/file.txt");
      console.log(info.size, info.isFile(), info.isDirectory());
      rmSync(base + "/a", { recursive: true });
      console.log(existsSync(base + "/a"));
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      "hello world\n68656c6c6f20776f726c64\ntrue false\nb,moved.txt\n11 true false\nfalse",
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
});
