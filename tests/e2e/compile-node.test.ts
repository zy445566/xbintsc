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

  it("operates on file descriptors", () => {
    const base = join(harness.workdir, `fd_${Math.random().toString(36).slice(2)}`);
    const source = `
      import { Buffer } from "buffer";
      import { closeSync, fchmodSync, fchownSync, fdatasyncSync, fstatSync, fsyncSync, ftruncateSync, futimesSync, linkSync, mkdirSync, openSync, readFileSync, readSync, readvSync, rmSync, statSync, writeSync, writevSync } from "fs";
      const base = ${JSON.stringify(base)};
      mkdirSync(base);
      const path = base + "/f.txt";
      const fd = openSync(path, "w+");
      writeSync(fd, "hello");
      writeSync(fd, Buffer.from(" world"), 0, 6, 5);
      const head = Buffer.alloc(5);
      console.log("read", readSync(fd, head, 0, 5, 0), head.toString());
      console.log("size", fstatSync(fd).size);
      fsyncSync(fd);
      fdatasyncSync(fd);
      fchmodSync(fd, 0o600);
      fchownSync(fd, -1, -1);
      futimesSync(fd, 1000, 2000);
      ftruncateSync(fd, 5);
      console.log("truncated", readFileSync(path), fstatSync(fd).size);
      closeSync(fd);

      const vectorFd = openSync(base + "/v.txt", "w+");
      writevSync(vectorFd, [Buffer.from("abc"), Buffer.from("def")]);
      const first = Buffer.alloc(3);
      const second = Buffer.alloc(3);
      console.log("readv", readvSync(vectorFd, [first, second], 0), first.toString(), second.toString());
      closeSync(vectorFd);

      linkSync(path, base + "/hard.txt");
      console.log("hard", readFileSync(base + "/hard.txt"), statSync(base + "/hard.txt").isFile());
      try { closeSync(9999); } catch (error) { console.log("close error", error.code); }
      rmSync(base, { recursive: true });
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      "read 5 hello\nsize 11\ntruncated hello 5\nreadv 6 abc def\nhard hello true\nclose error EBADF",
    );
  });

  it("changes file metadata", () => {
    const base = join(harness.workdir, `meta_${Math.random().toString(36).slice(2)}`);
    const source = `
      import { accessSync, chmodSync, chownSync, constants, lchmodSync, lchownSync, mkdirSync, mkdtempSync, readFileSync, rmSync, statSync, statfsSync, truncateSync, utimesSync, writeFileSync } from "fs";
      const base = ${JSON.stringify(base)};
      mkdirSync(base);
      const path = base + "/f.txt";
      writeFileSync(path, "abcdef");
      accessSync(path);
      accessSync(path, constants.R_OK | constants.W_OK);
      chmodSync(path, 0o600);
      lchmodSync(path, 0o644);
      console.log("mode", (statSync(path).mode & 0o777) === 0o644);
      chownSync(path, -1, -1);
      lchownSync(path, -1, -1);
      truncateSync(path, 3);
      utimesSync(path, 1000, 2000);
      console.log("trunc", readFileSync(path), Math.round(statSync(path).mtimeMs / 1000));
      utimesSync(path, new Date(1000), new Date(2000));
      console.log("date", statSync(path).mtimeMs === 2000);
      writeFileSync(path, "aGVsbG8", "base64url");
      console.log("b64url", readFileSync(path), readFileSync(path, "base64url"));
      const temporary = mkdtempSync(base + "/t-");
      console.log("mkdtemp", statSync(temporary).isDirectory(), statfsSync(base).bsize >= 0);
      try { accessSync(base + "/missing"); } catch (error) { console.log("access", error.code); }
      try { truncateSync(base + "/missing", 0); } catch (error) { console.log("truncate", error.code); }
      rmSync(base, { recursive: true });
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      "mode true\ntrunc abc 2000\ndate true\nb64url hello aGVsbG8\nmkdtemp true true\naccess ENOENT\ntruncate ENOENT",
    );
  });

  it("copies files and directories with cpSync", () => {
    const base = join(harness.workdir, `cp_${Math.random().toString(36).slice(2)}`);
    const source = `
      import { cpSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "fs";
      const base = ${JSON.stringify(base)};
      mkdirSync(base + "/src/sub", { recursive: true });
      writeFileSync(base + "/src/a.txt", "AAA");
      writeFileSync(base + "/src/sub/b.txt", "BBB");
      cpSync(base + "/src/a.txt", base + "/copy.txt");
      console.log("file", readFileSync(base + "/copy.txt"));
      cpSync(base + "/src", base + "/dst", { recursive: true });
      console.log("dir", readFileSync(base + "/dst/a.txt"), readFileSync(base + "/dst/sub/b.txt"));
      writeFileSync(base + "/copy.txt", "KEEP");
      cpSync(base + "/src/a.txt", base + "/copy.txt", { force: false });
      console.log("force", readFileSync(base + "/copy.txt"));
      try { cpSync(base + "/src/a.txt", base + "/copy.txt", { force: false, errorOnExist: true }); }
      catch (error) { console.log("exist", error.code); }
      try { cpSync(base + "/src", base + "/dst2"); } catch (error) { console.log("dir", error.code); }
      rmSync(base, { recursive: true });
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      "file AAA\ndir AAA BBB\nforce KEEP\nexist EEXIST\ndir EISDIR",
    );
  });

  it("lists directories and matches globs", () => {
    const base = join(harness.workdir, `list_${Math.random().toString(36).slice(2)}`);
    const source = `
      import { globSync, mkdirSync, opendirSync, rmSync, writeFileSync } from "fs";
      const base = ${JSON.stringify(base)};
      mkdirSync(base + "/sub/deep", { recursive: true });
      writeFileSync(base + "/a.txt", "a");
      writeFileSync(base + "/b.md", "b");
      writeFileSync(base + "/sub/c.txt", "c");
      writeFileSync(base + "/sub/deep/d.txt", "d");
      const dir = opendirSync(base);
      const names = [];
      let entry = dir.readSync();
      while (entry !== null) { names.push(entry.name); entry = dir.readSync(); }
      console.log("dir", names.sort().join(","));
      dir.closeSync();
      const asyncDir = opendirSync(base + "/sub");
      const seen = [];
      asyncDir.read((error, item) => { if (item) seen.push(item.name); });
      asyncDir.close(() => seen.push("closed"));
      console.log("async", seen.sort().join(","));
      console.log("star", globSync("*.txt", { cwd: base }).join(","));
      console.log("rec", globSync("**/*.txt", { cwd: base }).sort().join(","));
      console.log("question", globSync("?.txt", { cwd: base }).join(","));
      console.log("class", globSync("[ab].txt", { cwd: base }).join(","));
      console.log("array", globSync(["*.md", "*.txt"], { cwd: base }).sort().join(","));
      const entries = globSync("*.txt", { cwd: base, withFileTypes: true });
      console.log("dirent", entries.map((item) => item.name + ":" + item.isFile()).join(","));
      try { opendirSync(base + "/missing"); } catch (error) { console.log("opendir", error.code); }
      rmSync(base, { recursive: true });
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      "dir a.txt,b.md,sub\nasync closed,deep\nstar a.txt\nrec a.txt,sub/c.txt,sub/deep/d.txt\nquestion a.txt\nclass a.txt\narray a.txt,b.md\ndirent a.txt:true\nopendir ENOENT",
    );
  });

  it("supports symbolic links and readlinkSync", () => {
    if (process.platform === "win32") return;
    const base = join(harness.workdir, `sym_${Math.random().toString(36).slice(2)}`);
    const source = `
      import { cpSync, lstatSync, mkdirSync, readFileSync, readlinkSync, rmSync, statSync, symlinkSync, writeFileSync } from "fs";
      const base = ${JSON.stringify(base)};
      mkdirSync(base);
      writeFileSync(base + "/target.txt", "TARGET");
      symlinkSync(base + "/target.txt", base + "/link.txt");
      console.log("readlink", readlinkSync(base + "/link.txt") === base + "/target.txt");
      console.log("types", lstatSync(base + "/link.txt").isSymbolicLink(), statSync(base + "/link.txt").isFile());
      cpSync(base + "/link.txt", base + "/copied.txt");
      console.log("kept", lstatSync(base + "/copied.txt").isSymbolicLink());
      cpSync(base + "/link.txt", base + "/deref.txt", { dereference: true });
      console.log("deref", lstatSync(base + "/deref.txt").isFile(), readFileSync(base + "/deref.txt"));
      rmSync(base, { recursive: true });
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      "readlink true\ntypes true true\nkept true\nderef true TARGET",
    );
  });

  it("exposes fs constants and the watch API", () => {
    const source = `
      import { constants, unwatchFile, watch, watchFile } from "fs";
      console.log("consts", constants.F_OK, constants.R_OK, constants.W_OK, constants.X_OK, constants.COPYFILE_EXCL, constants.O_RDONLY !== undefined, constants.S_IFREG !== undefined);
      const watcher = watch(".", () => {});
      console.log("watch", typeof watcher.close, typeof watcher.on);
      watcher.close();
      const poll = watchFile(".", { interval: 100 }, () => {});
      console.log("watchFile", typeof poll.close, poll.kind === "file");
      poll.close();
      unwatchFile(".");
      console.log("unwatch ok");
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      "consts 0 4 2 1 1 true true\nwatch function function\nwatchFile function true\nunwatch ok",
    );
  });

  it("supports fs/promises and FileHandle", () => {
    const base = join(harness.workdir, `promises_${Math.random().toString(36).slice(2)}`);
    const source = `
      import { Buffer } from "buffer";
      import { constants, promises as fsp } from "fs";
      import { constants as promisesConstants, readFile as readFileAsync } from "fs/promises";
      const base = ${JSON.stringify(base)};
      async function main() {
        console.log("constants", fsp.constants.R_OK === constants.R_OK, promisesConstants.F_OK === 0);
        await fsp.mkdir(base, { recursive: true });
        await fsp.writeFile(base + "/a.txt", "hello");
        await fsp.appendFile(base + "/a.txt", " world");
        console.log("read", await readFileAsync(base + "/a.txt"));
        console.log("stat", (await fsp.stat(base + "/a.txt")).size, (await fsp.lstat(base + "/a.txt")).isFile());
        await fsp.access(base + "/a.txt");
        console.log("readdir", (await fsp.readdir(base, { withFileTypes: true })).length, (await fsp.realpath(base)).length > 0);
        console.log("statfs", (await fsp.statfs(base)).bsize >= 0);
        console.log("mkdtemp", (await fsp.mkdtemp(base + "/t-")).length > base.length);
        await fsp.copyFile(base + "/a.txt", base + "/copy.txt");
        await fsp.rename(base + "/copy.txt", base + "/renamed.txt");
        await fsp.cp(base + "/renamed.txt", base + "/cp.txt");
        await fsp.chmod(base + "/a.txt", 0o600);
        await fsp.lchmod(base + "/a.txt", 0o644);
        await fsp.chown(base + "/a.txt", -1, -1);
        await fsp.lchown(base + "/a.txt", -1, -1);
        await fsp.truncate(base + "/a.txt", 5);
        await fsp.utimes(base + "/a.txt", 1000, 2000);
        await fsp.lutimes(base + "/a.txt", 1000, 2000);
        await fsp.link(base + "/a.txt", base + "/hard.txt");
        console.log("trunc", await fsp.readFile(base + "/a.txt"), (await fsp.glob("*.txt", { cwd: base })).length >= 1);
        const dir = await fsp.opendir(base);
        console.log("opendir", dir.readSync().name.length > 0);
        dir.closeSync();
        (await fsp.watch(base)).close();
        await fsp.unlink(base + "/hard.txt");
        try { await fsp.readFile(base + "/missing.txt"); } catch (error) { console.log("reject", error.code); }

        const handle = await fsp.open(base + "/h.txt", "w+");
        console.log("fd", handle.fd > 0);
        await handle.writeFile("abcdef");
        await handle.sync();
        await handle.datasync();
        await handle.truncate(3);
        await handle.chmod(0o600);
        await handle.chown(-1, -1);
        await handle.utimes(1000, 2000);
        await handle.write(Buffer.from("XY"), 0, 2, 1);
        console.log("handle", (await handle.stat()).size);
        const bytes = Buffer.alloc(3);
        await handle.read(bytes, 0, 3, 0);
        console.log("handle read", bytes.toString());
        await handle.close();
        const reader = await fsp.open(base + "/h.txt", "r");
        console.log("handle readall", await reader.readFile("utf8"));
        await reader.close();
        try { await fsp.open(base + "/missing.txt"); } catch (error) { console.log("open reject", error.code); }
        await fsp.rm(base, { recursive: true, force: true });
      }
      main();
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      [
        "constants true true",
        "read hello world",
        "stat 11 true",
        "readdir 1 true",
        "statfs true",
        "mkdtemp true",
        "trunc hello true",
        "opendir true",
        "reject ENOENT",
        "fd true",
        "handle 3",
        "handle read aXY",
        "handle readall aXY",
        "open reject ENOENT",
      ].join("\n"),
    );
  });
});
