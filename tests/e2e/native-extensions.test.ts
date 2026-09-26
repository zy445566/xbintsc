/**
 * End-to-end tests for native (C++ / Rust) extensions.
 *
 * Each case compiles a tiny native library to an `extern "C"` object/archive,
 * describes it with a manifest and links it into a TypeScript program. The
 * suite is skipped when the corresponding native toolchain is unavailable, so
 * on a bare CI image nothing is compiled. Windows is skipped here: the suite's
 * manifests only carry Unix linker flags, and Windows uses the MSVC ABI. Windows
 * coverage lives in the `compile-examples` CI job, which links the example
 * manifests (with their `linkerFlagsByPlatform.win32` entries).
 */

import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { spawnSync } from "node:child_process";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { build } from "../../src/driver/compiler.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { nativeExtensionFromManifest } from "../../src/extensions/native.js";
import { findRuntimeDir } from "../../src/driver/paths.js";

function firstAvailable(candidates: readonly (string | undefined)[]): string | undefined {
  for (const candidate of candidates) {
    if (!candidate) continue;
    const result = spawnSync(candidate, ["--version"], { encoding: "utf8" });
    if (!result.error && result.status === 0) return candidate;
  }
  return undefined;
}

/** Like {@link firstAvailable}, but tolerates tools whose `--version` exits non-zero (Apple `ar`). */
function firstRunnable(candidates: readonly (string | undefined)[]): string | undefined {
  for (const candidate of candidates) {
    if (!candidate) continue;
    if (!spawnSync(candidate, ["--version"], { encoding: "utf8" }).error) return candidate;
  }
  return undefined;
}

const cxx = firstAvailable([process.env.xbintsc_CXX, "clang++", "c++", "g++"]);
const ar = firstRunnable([process.env.xbintsc_AR, "llvm-ar", "ar"]);
const cargo = firstAvailable([process.env.CARGO, "cargo"]);

/**
 * Compile a C++ source into a static archive next to it.
 */
function buildCppArchive(dir: string, source: string, stem: string): string {
  const sourcePath = join(dir, `${stem}.cpp`);
  const objectPath = join(dir, `${stem}.o`);
  const archivePath = join(dir, `lib${stem}.a`);
  writeFileSync(sourcePath, source);
  const compiled = spawnSync(
    cxx!,
    ["-O2", "-fPIC", "-I", findRuntimeDir(), "-c", sourcePath, "-o", objectPath],
    { encoding: "utf8" },
  );
  expect(compiled.status, compiled.stderr).toBe(0);
  const archived = spawnSync(ar!, ["rcs", archivePath, objectPath], { encoding: "utf8" });
  expect(archived.status, archived.stderr).toBe(0);
  return archivePath;
}

// Skipped on Windows: this suite's manifests only carry Unix linker flags.
describe.skipIf(!cxx || !ar || process.platform === "win32")("native C++ extension", () => {
  let workdir: string;
  beforeAll(() => {
    workdir = mkdtempSync(join(tmpdir(), "xbintsc-cxx-"));
  });
  afterAll(() => {
    rmSync(workdir, { recursive: true, force: true });
  });

  it("links a C++ archive and calls into it from TypeScript", () => {
    buildCppArchive(
      workdir,
      `
      #include "xt_ext.h"
      #include <string>
      #include <algorithm>

      XT_EXT_FN(mathx_add) {
        return xt_number(xt_ext_number(argc, argv, 0, 0) + xt_ext_number(argc, argv, 1, 0));
      }
      XT_EXT_FN(mathx_reverse) {
        size_t length = 0;
        const char *text = xt_ext_string(argc, argv, 0, &length);
        if (text == NULL) return xt_ext_string_value("", 0);
        std::string reversed(text, length);
        std::reverse(reversed.begin(), reversed.end());
        return xt_ext_string_from(reversed);
      }
      `,
      "mathx",
    );

    const manifestPath = join(workdir, "cpp.manifest.json");
    writeFileSync(
      manifestPath,
      JSON.stringify({
        name: "mathx-cpp",
        objects: ["libmathx.a"],
        linkerFlagsByPlatform: {
          linux: ["-lstdc++"],
          darwin: ["-lc++"],
        },
        builtins: { cppClamp: { symbol: "mathx_add" } },
        modules: {
          mathx: {
            exports: {
              add: { symbol: "mathx_add" },
              reverse: { symbol: "mathx_reverse" },
            },
          },
        },
      }),
    );

    const entry = join(workdir, "demo.ts");
    writeFileSync(
      entry,
      `
      import { add, reverse } from "mathx";
      console.log("add", add(2, 3));
      console.log("reverse", reverse("xbintsc"));
      console.log("builtin", cppClamp(7, 1));
      `,
    );

    const extensions = createDefaultRegistry().register(nativeExtensionFromManifest(manifestPath));
    const result = build(entry, { emit: "exe", outDir: join(workdir, "out"), cacheDir: join(workdir, ".cache"), extensions });
    expect(result.diagnostics.filter((d) => d.category === "error")).toEqual([]);

    const executed = spawnSync(result.outputPath, [], { encoding: "utf8" });
    expect(executed.status, executed.stderr).toBe(0);
    expect(executed.stdout.trim()).toBe(["add 5", "reverse cstnibx", "builtin 8"].join("\n"));
  });
});

// Skipped on Windows: this suite's manifests only carry Unix linker flags.
describe.skipIf(!cargo || !cxx || !ar || process.platform === "win32")("native Rust extension", () => {
  let workdir: string;
  beforeAll(() => {
    workdir = mkdtempSync(join(tmpdir(), "xbintsc-rust-"));
    writeFileSync(
      join(workdir, "Cargo.toml"),
      `[package]
name = "mathx"
version = "0.1.0"
edition = "2021"

[lib]
name = "mathx"
crate-type = ["staticlib"]

[profile.release]
opt-level = 3
`,
    );
    const sourceDir = join(workdir, "src");
    mkdirSync(sourceDir, { recursive: true });
    writeFileSync(
      join(sourceDir, "lib.rs"),
      `
      include!("${resolve(findRuntimeDir(), "xt_ext.rs").replace(/\\/g, "/")}");

      #[no_mangle]
      pub extern "C" fn mathx_add(argc: i32, argv: *const XtValue) -> XtValue {
          xt_number(arg_number(argc, argv, 0, 0.0) + arg_number(argc, argv, 1, 0.0))
      }
      `,
    );
  });
  afterAll(() => {
    rmSync(workdir, { recursive: true, force: true });
  });

  it("links a Rust staticlib and calls into it from TypeScript", () => {
    const built = spawnSync(cargo!, ["build", "--release"], { cwd: workdir, encoding: "utf8" });
    expect(built.status, built.stderr).toBe(0);

    const archive = join(workdir, "target", "release", "libmathx.a");
    expect(readFileSync(archive).length).toBeGreaterThan(0);

    const manifestPath = join(workdir, "rust.manifest.json");
    writeFileSync(
      manifestPath,
      JSON.stringify({
        name: "mathx-rust",
        objects: ["target/release/libmathx.a"],
        linkerFlagsByPlatform: {
          linux: ["-lpthread", "-ldl", "-lm"],
          darwin: ["-liconv", "-framework", "Security"],
        },
        modules: { mathx: { exports: { add: { symbol: "mathx_add" } } } },
      }),
    );

    const entry = join(workdir, "demo.ts");
    writeFileSync(entry, `import { add } from "mathx";\nconsole.log("add", add(20, 22));\n`);

    const extensions = createDefaultRegistry().register(nativeExtensionFromManifest(manifestPath));
    const result = build(entry, { emit: "exe", outDir: join(workdir, "out"), cacheDir: join(workdir, ".cache"), extensions });
    expect(result.diagnostics.filter((d) => d.category === "error")).toEqual([]);

    const executed = spawnSync(result.outputPath, [], { encoding: "utf8" });
    expect(executed.status, executed.stderr).toBe(0);
    expect(executed.stdout.trim()).toBe("add 42");
  });
});
