import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { build, compileEntry, compileString, resolvePreferPrebuilt } from "../../src/driver/compiler.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { nodeExtension } from "../../src/extensions/node/index.js";
import { DiagnosticCode } from "../../src/diagnostics/diagnostic.js";
import { ToolchainError, type CommandResult, type Runner } from "../../src/driver/toolchain.js";

const directories: string[] = [];

function temporaryDirectory(): string {
  const directory = mkdtempSync(join(tmpdir(), "xbintsc-build-"));
  directories.push(directory);
  return directory;
}

afterEach(() => {
  while (directories.length > 0) rmSync(directories.pop()!, { recursive: true, force: true });
});

describe("compileString", () => {
  it("turns source into LLVM IR", () => {
    const { ir, diagnostics } = compileString("console.log(1 + 2);", "inline.ts");
    expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
    expect(ir).toContain("define i32 @main");
    expect(ir).toContain("@xt_add");
  });

  it("reports parse errors without producing a broken module", () => {
    const { diagnostics } = compileString("const = ;", "bad.ts");
    expect(diagnostics.some((d) => d.category === "error")).toBe(true);
  });

  it("does not cascade a name error from the parser's recovery node", () => {
    const { diagnostics } = compileString("const value = ;", "bad.ts");
    const errors = diagnostics.filter((d) => d.category === "error");
    expect(errors.some((d) => d.code === DiagnosticCode.UnexpectedToken)).toBe(true);
    expect(errors.some((d) => d.message.includes("<error>"))).toBe(false);
  });

  it("hints the enabling flag for a known module from an unregistered extension", () => {
    const registry = createDefaultRegistry().hintExtension(nodeExtension);
    const { diagnostics } = compileString(
      'import { createServer } from "node:http";\ncreateServer(() => {});',
      "server.ts",
      registry,
    );
    const errors = diagnostics.filter((d) => d.category === "error");
    expect(errors).toHaveLength(1);
    expect(errors[0]!.code).toBe(DiagnosticCode.ModuleNotFound);
    expect(errors[0]!.message).toBe(
      "module 'node:http' is provided by the 'node' extension; pass --ext node",
    );
  });

  it("does not hint modules whose extension is registered", () => {
    const registry = createDefaultRegistry().hintExtension(nodeExtension).register(nodeExtension);
    const { diagnostics } = compileString(
      'import { createServer } from "node:http";\ncreateServer(() => {});',
      "server.ts",
      registry,
    );
    expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
  });

  it("explains an unsupported third-party import without a value error", () => {
    const { diagnostics } = compileString(
      'import { z } from "zod";\nz.string();',
      "third-party.ts",
    );
    const errors = diagnostics.filter((d) => d.category === "error");
    expect(errors).toHaveLength(1);
    expect(errors[0]!.code).toBe(DiagnosticCode.ModuleNotFound);
    expect(errors[0]!.message).toContain("module 'zod' is not supported");
    expect(errors[0]!.message).toContain("node_modules");
    expect(errors.map((d) => d.message).join("\n")).not.toContain("cannot be used as a value");
  });

  it("keeps a type-only third-party import compiling", () => {
    const { diagnostics } = compileString(
      'import { ZodString } from "zod";\nlet value: ZodString;\nconsole.log(1);',
      "third-party-types.ts",
    );
    expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
  });

  it("steers a CommonJS require() at the ESM import form", () => {
    const { diagnostics } = compileString('const fs = require("fs");\nfs;', "require.ts");
    const errors = diagnostics.filter((d) => d.category === "error");
    expect(errors).toHaveLength(1);
    expect(errors[0]!.code).toBe(DiagnosticCode.UnsupportedFeature);
    expect(errors[0]!.message).toContain("`require()` is not supported");
    expect(errors[0]!.message).toContain('import value from "fs"');
  });

  it("steers a bare require reference at the ESM import form", () => {
    const { diagnostics } = compileString("const loader = require;\nloader;", "require-value.ts");
    const errors = diagnostics.filter((d) => d.category === "error");
    expect(errors).toHaveLength(1);
    expect(errors[0]!.message).toContain("`require()` is not supported");
    expect(errors[0]!.message).toContain("ESM `import` statement");
  });

  it("reads `globalThis.require` as the absent global property it is", () => {
    /* Bundled CommonJS probes the loader with `globalThis.require`; that is a
     * property read, not a CommonJS call, so it must not be reported. */
    const { diagnostics } = compileString(
      'const probe = (globalThis as any).require;\nconsole.log(typeof probe);',
      "require-probe.ts",
    );
    expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
  });

  it("still steers a globalThis.require() call at the ESM import form", () => {
    const { diagnostics } = compileString('(globalThis as any).require("fs");', "require-global-call.ts");
    const errors = diagnostics.filter((d) => d.category === "error");
    expect(errors).toHaveLength(1);
    expect(errors[0]!.code).toBe(DiagnosticCode.UnsupportedFeature);
    expect(errors[0]!.message).toContain("`require()` is not supported");
  });

  it("names a missing export of a registered module", () => {
    const registry = createDefaultRegistry().register(nodeExtension);
    const { diagnostics } = compileString(
      'import { nope } from "node:fs";\nconsole.log(nope);',
      "missing-export.ts",
      registry,
    );
    const errors = diagnostics.filter((d) => d.category === "error");
    expect(errors).toHaveLength(1);
    expect(errors[0]!.message).toBe(`Module '"node:fs"' has no exported member 'nope'`);
  });
});

describe("compileEntry", () => {
  it("bundles relative modules, resolving `.js` specifiers to `.ts` sources", () => {
    const directory = temporaryDirectory();
    writeFileSync(join(directory, "dep.ts"), "export const value = 40;\n");
    const entry = join(directory, "main.ts");
    writeFileSync(entry, 'import { value } from "./dep.js";\nconsole.log(value + 2);\n');

    const { ir, diagnostics } = compileEntry(entry);
    expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
    // Both the imported constant and the message are lowered into one module.
    expect(ir).toContain("@xt_add");
  });

  it("does not let a dependency's function-local class hijack a type name", () => {
    /* Bundling merges every module into one source file, and minified packages
     * declare short-named classes inside functions all the time
     * (`babel.cjs` has its own `class Token`). Those are invisible to an
     * importer, so a `Token` annotation in the entry must stay `any` instead of
     * resolving to the dependency's class and reporting a bogus property. */
    const directory = temporaryDirectory();
    writeFileSync(
      join(directory, "dep.js"),
      'export function make() { class Token { constructor() { this.raw = ""; } } return new Token(); }\n',
    );
    const entry = join(directory, "main.ts");
    writeFileSync(
      entry,
      [
        'import { make } from "./dep.js";',
        "function describe(token: Token): string { return token.type; }",
        "console.log(describe(make() as Token));",
      ].join("\n"),
    );

    const { diagnostics } = compileEntry(entry);
    const errors = diagnostics.filter((d) => d.category === "error");
    expect(errors.map((error) => error.message)).toEqual([]);
  });

  it("bundles a package from node_modules", () => {
    const directory = temporaryDirectory();
    const packageDir = join(directory, "node_modules", "mathx");
    mkdirSync(packageDir, { recursive: true });
    writeFileSync(join(packageDir, "package.json"), '{ "name": "mathx", "main": "index.js" }');
    writeFileSync(join(packageDir, "index.js"), "export const value = 40;\n");
    const entry = join(directory, "main.ts");
    writeFileSync(entry, 'import { value } from "mathx";\nconsole.log(value + 2);\n');

    const { ir, diagnostics } = compileEntry(entry);
    expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
    expect(ir).toContain("@xt_add");
  });
});

describe("build", () => {
  it("emits an IR file and skips work on the second run", () => {
    const directory = temporaryDirectory();
    const entry = join(directory, "main.ts");
    writeFileSync(entry, "console.log(21 * 2);");
    const outDir = join(directory, "out");

    const first = build(entry, { emit: "ir", outDir, cacheDir: join(directory, ".cache") });
    expect(first.cached).toBe(false);
    expect(first.outputPath.endsWith(".ll")).toBe(true);
    expect(existsSync(first.outputPath)).toBe(true);
    expect(readFileSync(first.outputPath, "utf8")).toContain("@xt_mul");

    const second = build(entry, { emit: "ir", outDir, cacheDir: join(directory, ".cache") });
    expect(second.cached).toBe(true);

    const forced = build(entry, { emit: "ir", outDir, cacheDir: join(directory, ".cache"), force: true });
    expect(forced.cached).toBe(false);
  });

  it("honours xbintsc_CACHE_DIR when no cacheDir is given", () => {
    const directory = temporaryDirectory();
    const entry = join(directory, "main.ts");
    writeFileSync(entry, "console.log(1);");
    const envCache = join(directory, "env-cache");
    const previous = process.env.xbintsc_CACHE_DIR;
    process.env.xbintsc_CACHE_DIR = envCache;
    try {
      build(entry, { emit: "ir", outDir: join(directory, "out") });
      expect(existsSync(join(envCache, "build-cache.json"))).toBe(true);
    } finally {
      if (previous === undefined) delete process.env.xbintsc_CACHE_DIR;
      else process.env.xbintsc_CACHE_DIR = previous;
    }
  });

  it("resolves the prebuilt-archive preference from the option then the environment", () => {
    const previous = process.env.xbintsc_PREFER_PREBUILT;
    try {
      delete process.env.xbintsc_PREFER_PREBUILT;
      expect(resolvePreferPrebuilt(undefined)).toBe(true);
      expect(resolvePreferPrebuilt(false)).toBe(false);
      expect(resolvePreferPrebuilt(true)).toBe(true);

      process.env.xbintsc_PREFER_PREBUILT = "0";
      expect(resolvePreferPrebuilt(undefined)).toBe(false);
      process.env.xbintsc_PREFER_PREBUILT = "FALSE";
      expect(resolvePreferPrebuilt(undefined)).toBe(false);
      process.env.xbintsc_PREFER_PREBUILT = "1";
      expect(resolvePreferPrebuilt(undefined)).toBe(true);
      process.env.xbintsc_PREFER_PREBUILT = "";
      expect(resolvePreferPrebuilt(undefined)).toBe(true);

      // An explicit option still wins over the environment.
      process.env.xbintsc_PREFER_PREBUILT = "0";
      expect(resolvePreferPrebuilt(true)).toBe(true);
    } finally {
      if (previous === undefined) delete process.env.xbintsc_PREFER_PREBUILT;
      else process.env.xbintsc_PREFER_PREBUILT = previous;
    }
  });

  it("returns diagnostics for invalid programs", () => {
    const directory = temporaryDirectory();
    const entry = join(directory, "bad.ts");
    writeFileSync(entry, "const = ;");
    const result = build(entry, { emit: "ir", outDir: join(directory, "out"), cacheDir: join(directory, ".cache") });
    expect(result.diagnostics.some((d) => d.category === "error")).toBe(true);
  });

  it("drives clang through the toolchain to link an executable", () => {
    const directory = temporaryDirectory();
    const entry = join(directory, "main.ts");
    writeFileSync(entry, "console.log(1);");
    const calls: { command: string; args: string[] }[] = [];
    const runner: Runner = {
      run(command, args): CommandResult {
        calls.push({ command, args: [...args] });
        return { status: 0, stdout: "", stderr: "" };
      },
    };

    const result = build(entry, {
      emit: "exe",
      outDir: join(directory, "out"),
      cacheDir: join(directory, ".cache"),
      runner,
      clang: "clang",
      // Force runtime compilation so we exercise the clang C path rather than a
      // prebuilt archive that may exist in `runtime/lib/`.
      preferPrebuilt: false,
    });

    expect(result.diagnostics).toHaveLength(0);
    // clang was used for the runtime object, the module object and the link.
    const compileCalls = calls.filter((call) => call.args.includes("-c"));
    expect(compileCalls.length).toBeGreaterThanOrEqual(2);
    const linkCall = calls.at(-1)!;
    expect(linkCall.args).toContain("-o");
    expect(linkCall.args).toContain(result.outputPath);
    if (process.platform !== "win32") expect(linkCall.args).toContain("-lm");
  });

  it("recompiles the runtime from source when a prebuilt archive cannot link", () => {
    const directory = temporaryDirectory();
    const entry = join(directory, "main.ts");
    writeFileSync(entry, "console.log(1);");
    const libDir = join(directory, "runtime-lib");
    mkdirSync(libDir, { recursive: true });
    writeFileSync(join(libDir, "core.a"), "archive built by a newer Xcode");

    const calls: { command: string; args: string[] }[] = [];
    let links = 0;
    const runner: Runner = {
      run(command, args): CommandResult {
        calls.push({ command, args: [...args] });
        if (args.includes("-c")) return { status: 0, stdout: "", stderr: "" };
        links += 1;
        // The prebuilt archive was produced by a newer SDK: reject that link,
        // then accept the retry built from the shipped C sources.
        if (links === 1) {
          return {
            status: 1,
            stdout: "",
            stderr: "ld: warning: core.a(xt_alloc.o) was built for newer macOS version (15.0)\n" +
              "ld: Undefined symbols:\n  _xt_newer_only",
          };
        }
        return { status: 0, stdout: "", stderr: "" };
      },
    };

    const result = build(entry, {
      emit: "exe",
      outDir: join(directory, "out"),
      cacheDir: join(directory, ".cache"),
      runner,
      clang: "clang",
      preferPrebuilt: true,
      runtimeLibDirs: [libDir],
    });

    expect(result.diagnostics).toHaveLength(0);
    const linkCalls = calls.filter((call) => !call.args.includes("-c"));
    expect(linkCalls).toHaveLength(2);
    expect(linkCalls[0]!.args).toContain(join(libDir, "core.a"));
    expect(linkCalls[1]!.args).not.toContain(join(libDir, "core.a"));
    // The retry actually compiled the runtime instead of dropping it.
    expect(calls.some((call) => call.args.some((arg) => arg.endsWith("xt_alloc.c")))).toBe(true);
  });

  it("rebuilds from source when a successful link warns about a newer SDK", () => {
    const directory = temporaryDirectory();
    const entry = join(directory, "main.ts");
    writeFileSync(entry, "console.log(1);");
    const libDir = join(directory, "runtime-lib");
    mkdirSync(libDir, { recursive: true });
    writeFileSync(join(libDir, "core.a"), "archive built by a newer Xcode");
    let links = 0;
    const runner: Runner = {
      run(_command, args): CommandResult {
        if (args.includes("-c")) return { status: 0, stdout: "", stderr: "" };
        links += 1;
        return links === 1
          ? {
              status: 0,
              stdout: "",
              stderr:
                "ld: warning: object file (core.a(xt_alloc.o)) was built for newer macOS version " +
                "(15.0) than being linked (13.0)",
            }
          : { status: 0, stdout: "", stderr: "" };
      },
    };

    build(entry, {
      emit: "exe",
      outDir: join(directory, "out"),
      cacheDir: join(directory, ".cache"),
      runner,
      clang: "clang",
      preferPrebuilt: true,
      runtimeLibDirs: [libDir],
    });

    expect(links).toBe(2);
  });

  it("does not retry a link failure that is not a toolchain mismatch", () => {
    const directory = temporaryDirectory();
    const entry = join(directory, "main.ts");
    writeFileSync(entry, "console.log(1);");
    const libDir = join(directory, "runtime-lib");
    mkdirSync(libDir, { recursive: true });
    writeFileSync(join(libDir, "core.a"), "archive");
    let links = 0;
    const runner: Runner = {
      run(_command, args): CommandResult {
        if (args.includes("-c")) return { status: 0, stdout: "", stderr: "" };
        links += 1;
        return { status: 1, stdout: "", stderr: "clang: error: no such file or directory: 'nope.o'" };
      },
    };

    expect(() =>
      build(entry, {
        emit: "exe",
        outDir: join(directory, "out"),
        cacheDir: join(directory, ".cache"),
        runner,
        clang: "clang",
        preferPrebuilt: true,
        runtimeLibDirs: [libDir],
      }),
    ).toThrow(ToolchainError);
    expect(links).toBe(1);
  });
});
