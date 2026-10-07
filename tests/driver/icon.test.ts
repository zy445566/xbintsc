import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import { build } from "../../src/driver/compiler.js";
import {
  EMPTY_ICON,
  ICON_SYMBOLS,
  IconError,
  ensureIconObject,
  iconSource,
  pngToIco,
  readIcon,
  toIcoBytes,
} from "../../src/driver/icon.js";
import { ensureWindowsIconResource, resolveResourceCompiler } from "../../src/driver/win-icon.js";
import { bundlePathFor, infoPlist, packageMacApp } from "../../src/driver/mac-bundle.js";
import { DiagnosticCode } from "../../src/diagnostics/diagnostic.js";
import { guiExtension } from "../../src/extensions/gui/index.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { findRuntimeDir, platformSlug } from "../../src/driver/paths.js";
import type { CommandResult, Runner } from "../../src/driver/toolchain.js";

/** The GUI suite only runs when the native archive has been built (see gui-helpers). */
const guiAvailable =
  existsSync(join(findRuntimeDir(), "lib", platformSlug(), "gui.a")) &&
  process.platform !== "win32";

const LOGO_PNG_BASE64 =
  "iVBORw0KGgoAAAANSUhEUgAAAAQAAAAECAYAAACp8Z5+AAAAFElEQVR42mP4z8DwH4Sh1H8G0gUALFAf4eNWqTEAAAAASUVORK5CYII=";

const directories: string[] = [];

function temporaryDirectory(): string {
  const directory = mkdtempSync(join(tmpdir(), "xbintsc-icon-"));
  directories.push(directory);
  return directory;
}

function writePng(directory: string, name = "icon.png"): string {
  const path = join(directory, name);
  writeFileSync(path, Buffer.from(LOGO_PNG_BASE64, "base64"));
  return path;
}

interface Recorded {
  command: string;
  args: readonly string[];
}

function recordingRunner(
  handler?: (command: string, args: readonly string[]) => Partial<CommandResult>,
): { runner: Runner; calls: Recorded[] } {
  const calls: Recorded[] = [];
  const runner: Runner = {
    run(command, args): CommandResult {
      calls.push({ command, args: [...args] });
      return { status: 0, stdout: "", stderr: "", ...handler?.(command, args) };
    },
  };
  return { runner, calls };
}

afterEach(() => {
  while (directories.length > 0) rmSync(directories.pop()!, { recursive: true, force: true });
});

describe("readIcon", () => {
  it("reads a PNG and sniffs its size", () => {
    const path = writePng(temporaryDirectory());
    const icon = readIcon(path);
    expect(icon.format).toBe("png");
    expect(icon.width).toBe(4);
    expect(icon.height).toBe(4);
    expect(icon.path).toBe(path);
  });

  it("reads an ICO directory entry", () => {
    const directory = temporaryDirectory();
    const path = join(directory, "icon.ico");
    writeFileSync(path, Buffer.from([0, 0, 1, 0, 1, 0, 16, 16, 0, 0, 1, 0, 32, 0]));
    const icon = readIcon(path);
    expect(icon.format).toBe("ico");
    expect(icon.width).toBe(16);
    expect(icon.height).toBe(16);
  });

  it("treats a zero-sized ICO entry as 256 pixels", () => {
    const directory = temporaryDirectory();
    const path = join(directory, "big.ico");
    writeFileSync(path, Buffer.from([0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 32, 0]));
    const icon = readIcon(path);
    expect(icon.width).toBe(256);
    expect(icon.height).toBe(256);
  });

  it("reads an ICNS file", () => {
    const directory = temporaryDirectory();
    const path = join(directory, "icon.icns");
    writeFileSync(path, Buffer.from("icns\0\0\0\x08test"));
    const icon = readIcon(path);
    expect(icon.format).toBe("icns");
    expect(icon.width).toBe(0);
  });

  it("rejects missing, empty and unsupported files", () => {
    const directory = temporaryDirectory();
    expect(() => readIcon(join(directory, "nope.png"))).toThrow(IconError);

    const empty = join(directory, "empty.png");
    writeFileSync(empty, "");
    expect(() => readIcon(empty)).toThrow(/empty/);

    const bogus = join(directory, "icon.svg");
    writeFileSync(bogus, "<svg/>");
    expect(() => readIcon(bogus)).toThrow(/Unsupported icon format/);
  });
});

describe("iconSource", () => {
  it("embeds the bytes and metadata", () => {
    const source = iconSource(readIcon(writePng(temporaryDirectory())));
    expect(source).toContain(`const unsigned char ${ICON_SYMBOLS.data}[]`);
    expect(source).toContain(`const unsigned long long ${ICON_SYMBOLS.size}`);
    expect(source).toContain('const char xt_app_icon_format[] = "png";');
    expect(source).toContain("const unsigned int xt_app_icon_width = 4u;");
  });

  it("emits a placeholder and size 0 for an empty icon", () => {
    const source = iconSource(EMPTY_ICON);
    expect(source).toContain("  0x00,");
    expect(source).toContain(`const unsigned long long ${ICON_SYMBOLS.size} = 0ull;`);
  });
});

describe("ICO conversion", () => {
  it("wraps a PNG in an ICO container", () => {
    const png = Buffer.from(LOGO_PNG_BASE64, "base64");
    const ico = Buffer.from(pngToIco(png, 4, 4));
    expect(ico.readUInt16LE(0)).toBe(0);
    expect(ico.readUInt16LE(2)).toBe(1);
    expect(ico.readUInt16LE(4)).toBe(1);
    expect(ico[6]).toBe(4);
    expect(ico[7]).toBe(4);
    expect(ico.readUInt32LE(14)).toBe(png.length);
    expect(ico.readUInt32LE(18)).toBe(22);
  });

  it("marks large PNGs as 256", () => {
    const ico = Buffer.from(pngToIco(Buffer.from([1, 2, 3]), 512, 512));
    expect(ico[6]).toBe(0);
    expect(ico[7]).toBe(0);
  });

  it("passes ICO through and rejects ICNS", () => {
    const directory = temporaryDirectory();
    const icoPath = join(directory, "a.ico");
    const icoBytes = Buffer.from([0, 0, 1, 0, 1, 0, 16, 16, 0, 0, 1, 0, 32, 0]);
    writeFileSync(icoPath, icoBytes);
    expect(toIcoBytes(readIcon(icoPath))).toEqual(icoBytes);

    const icnsPath = join(directory, "a.icns");
    writeFileSync(icnsPath, Buffer.from("icns\0\0\0\x08test"));
    expect(() => toIcoBytes(readIcon(icnsPath))).toThrow(/PE resource/);
  });
});

describe("ensureIconObject", () => {
  it("writes the generated C and compiles it once", () => {
    const directory = temporaryDirectory();
    const { runner, calls } = recordingRunner();
    const icon = readIcon(writePng(directory));
    const objectPath = ensureIconObject(runner, "clang", join(directory, "cache"), icon);
    expect(existsSync(objectPath)).toBe(false);
    const compile = calls.find((call) => call.args.includes("-c"))!;
    expect(compile.command).toBe("clang");
    expect(readFileSync(compile.args[compile.args.indexOf("-c") + 1]!, "utf8")).toContain(
      ICON_SYMBOLS.data,
    );
  });

  it("reuses an existing object", () => {
    const directory = temporaryDirectory();
    const cacheDir = join(directory, "cache");
    const { runner, calls } = recordingRunner();
    const icon = readIcon(writePng(directory));
    const first = ensureIconObject(runner, "clang", cacheDir, icon);
    writeFileSync(first, "");
    const before = calls.length;
    const second = ensureIconObject(runner, "clang", cacheDir, icon);
    expect(second).toBe(first);
    expect(calls.length).toBe(before);
  });
});

describe("resolveResourceCompiler", () => {
  it("honours the xbintsc_RC override", () => {
    const previous = process.env.xbintsc_RC;
    process.env.xbintsc_RC = "my-rc";
    try {
      const { runner } = recordingRunner();
      expect(resolveResourceCompiler(runner, "clang")).toEqual({ tool: "my-rc", kind: "llvm-rc" });
    } finally {
      if (previous === undefined) delete process.env.xbintsc_RC;
      else process.env.xbintsc_RC = previous;
    }
  });

  it("detects windres overrides and skips missing sibling paths", () => {
    const previous = process.env.xbintsc_RC;
    process.env.xbintsc_RC = "/opt/mingw/bin/windres";
    try {
      const { runner } = recordingRunner();
      expect(resolveResourceCompiler(runner, "clang")).toEqual({
        tool: "/opt/mingw/bin/windres",
        kind: "windres",
      });
    } finally {
      if (previous === undefined) delete process.env.xbintsc_RC;
      else process.env.xbintsc_RC = previous;
    }

    const { runner, calls } = recordingRunner();
    const clang = join(temporaryDirectory(), "bin", "clang");
    expect(resolveResourceCompiler(runner, clang)).toEqual({ tool: "llvm-rc", kind: "llvm-rc" });
    expect(calls.some((call) => call.command === clang)).toBe(false);
  });

  it("returns undefined when nothing responds", () => {
    const { runner } = recordingRunner(() => ({ status: 127, stderr: "not found" }));
    expect(resolveResourceCompiler(runner, "clang")).toBeUndefined();
  });

  it("falls back when a broken override cannot run", () => {
    const previous = process.env.xbintsc_RC;
    process.env.xbintsc_RC = "broken-rc";
    try {
      const { runner } = recordingRunner((command) => (command === "broken-rc" ? { status: 1 } : {}));
      expect(resolveResourceCompiler(runner, "clang")).toEqual({ tool: "llvm-rc", kind: "llvm-rc" });
    } finally {
      if (previous === undefined) delete process.env.xbintsc_RC;
      else process.env.xbintsc_RC = previous;
    }
  });
});

describe("ensureWindowsIconResource", () => {
  const compiler = { tool: "llvm-rc", kind: "llvm-rc" } as const;

  it("writes an .rc and compiles it", () => {
    const directory = temporaryDirectory();
    const cacheDir = join(directory, "cache");
    const icon = readIcon(writePng(directory));
    mkdirSync(cacheDir, { recursive: true });
    const { runner, calls } = recordingRunner((_command, args) => {
      const output = args[args.indexOf("/fo") + 1]!;
      writeFileSync(output, "");
      return {};
    });
    const objectPath = ensureWindowsIconResource(runner, cacheDir, icon, compiler);
    expect(existsSync(objectPath)).toBe(true);
    const rcPath = calls[0]!.args.at(-1)!;
    const rcContents = readFileSync(rcPath, "utf8");
    expect(rcContents).toMatch(/^1 ICON "icon-.*\.ico"\n$/);
    // The generated .ico sits next to the .rc so the relative reference resolves.
    const icoName = rcContents.match(/"([^"]+)"/)![1]!;
    expect(existsSync(join(cacheDir, icoName))).toBe(true);
  });

  it("uses windres argument style", () => {
    const directory = temporaryDirectory();
    const cacheDir = join(directory, "cache");
    const icon = readIcon(writePng(directory));
    const { runner, calls } = recordingRunner((_command, args) => {
      writeFileSync(args[args.indexOf("-o") + 1]!, "");
      return {};
    });
    ensureWindowsIconResource(runner, cacheDir, icon, { tool: "windres", kind: "windres" });
    expect(calls[0]!.args).toContain("-O");
    expect(calls[0]!.args).toContain("coff");
  });

  it("throws when the resource compiler fails", () => {
    const directory = temporaryDirectory();
    const icon = readIcon(writePng(directory));
    const { runner } = recordingRunner(() => ({ status: 1, stderr: "bad rc" }));
    expect(() => ensureWindowsIconResource(runner, join(directory, "cache"), icon, compiler)).toThrow(
      IconError,
    );
  });
});

describe("mac bundle", () => {
  it("lays out a bundle and falls back to shipping the PNG", () => {
    const directory = temporaryDirectory();
    const executable = join(directory, "Demo");
    writeFileSync(executable, "#!/bin/sh\n");
    const icon = readIcon(writePng(directory));
    const { runner } = recordingRunner();
    const bundle = packageMacApp({
      executablePath: executable,
      name: "Demo",
      bundleId: "com.example.demo",
      icon,
      runner,
    });
    expect(bundle).toBe(bundlePathFor(executable, "Demo"));
    expect(existsSync(join(bundle, "Contents", "MacOS", "Demo"))).toBe(true);
    expect(existsSync(join(bundle, "Contents", "Resources", "AppIcon.png"))).toBe(true);
    const plist = readFileSync(join(bundle, "Contents", "Info.plist"), "utf8");
    expect(plist).toContain("<string>com.example.demo</string>");
    expect(plist).toContain("<string>AppIcon.png</string>");
  });

  it("copies an ICNS icon and converts a PNG when iconutil works", () => {
    const directory = temporaryDirectory();
    const executable = join(directory, "Demo");
    writeFileSync(executable, "#!/bin/sh\n");

    const icns = join(directory, "icon.icns");
    writeFileSync(icns, Buffer.from("icns\0\0\0\x08test"));
    const first = packageMacApp({
      executablePath: executable,
      name: "Demo",
      bundleId: "com.example.demo",
      icon: readIcon(icns),
      runner: recordingRunner().runner,
    });
    expect(existsSync(join(first, "Contents", "Resources", "AppIcon.icns"))).toBe(true);

    const png = writePng(directory, "icon2.png");
    const { runner } = recordingRunner((command, args) => {
      if (command === "iconutil" && args[0] === "-c") {
        writeFileSync(args[args.indexOf("-o") + 1]!, "icns");
      }
      return {};
    });
    const second = packageMacApp({
      executablePath: executable,
      name: "Demo",
      bundleId: "com.example.demo",
      icon: readIcon(png),
      runner,
    });
    const plist = readFileSync(join(second, "Contents", "Info.plist"), "utf8");
    expect(plist).toContain("<string>AppIcon</string>");
    expect(plist).not.toContain("AppIcon.png");
  });

  it("omits the icon key without an icon and survives missing tools", () => {
    const directory = temporaryDirectory();
    const executable = join(directory, "Demo");
    writeFileSync(executable, "#!/bin/sh\n");
    const clean = packageMacApp({
      executablePath: executable,
      name: "Demo",
      bundleId: "com.example.demo",
      runner: recordingRunner().runner,
    });
    expect(readFileSync(join(clean, "Contents", "Info.plist"), "utf8")).not.toContain(
      "CFBundleIconFile",
    );

    const icon = readIcon(writePng(directory));
    const throwing: Runner = {
      run() {
        throw new Error("missing");
      },
    };
    const fallback = packageMacApp({
      executablePath: executable,
      name: "Demo",
      bundleId: "com.example.demo",
      icon,
      runner: throwing,
    });
    expect(existsSync(join(fallback, "Contents", "Resources", "AppIcon.png"))).toBe(true);
  });

  it("falls back to a PNG when the converters report failure", () => {
    const directory = temporaryDirectory();
    const executable = join(directory, "Demo");
    writeFileSync(executable, "#!/bin/sh\n");
    const icon = readIcon(writePng(directory));

    const sipsFails = packageMacApp({
      executablePath: executable,
      name: "Demo",
      bundleId: "com.example.demo",
      icon,
      runner: recordingRunner((command) =>
        command === "sips" ? { status: 1, stderr: "bad image" } : {},
      ).runner,
    });
    expect(existsSync(join(sipsFails, "Contents", "Resources", "AppIcon.png"))).toBe(true);

    const iconutilFails = packageMacApp({
      executablePath: executable,
      name: "Demo",
      bundleId: "com.example.demo",
      icon,
      runner: recordingRunner((command) =>
        command === "iconutil" ? { status: 1, stderr: "no icns" } : {},
      ).runner,
    });
    expect(existsSync(join(iconutilFails, "Contents", "Resources", "AppIcon.png"))).toBe(true);
  });
  it("renders and escapes the plist", () => {
    const plist = infoPlist({ name: "A & B", bundleId: "com.<x>", executable: "A&B" });
    expect(plist).toContain("<string>A &amp; B</string>");
    expect(plist).toContain("<string>com.&lt;x&gt;</string>");
    expect(plist).toContain("<string>A&amp;B</string>");
  });
});

describe("build integration", () => {
  it("links the embedded icon object", () => {
    const directory = temporaryDirectory();
    const entry = join(directory, "main.ts");
    writeFileSync(entry, "console.log(1);");
    const icon = writePng(directory, "logo.png");
    const { runner, calls } = recordingRunner();

    const result = build(entry, {
      emit: "exe",
      outDir: join(directory, "out"),
      cacheDir: join(directory, ".cache"),
      runner,
      clang: "clang",
      preferPrebuilt: false,
      icon,
    });

    expect(result.diagnostics).toHaveLength(0);
    const linkCall = calls.at(-1)!;
    expect(linkCall.args.some((arg) => arg.includes("icon-") && arg.endsWith(".o"))).toBe(true);
  });

  it.skipIf(!guiAvailable)("links an empty icon object for GUI programs without an icon", () => {
    const directory = temporaryDirectory();
    const entry = join(directory, "main.ts");
    writeFileSync(entry, "console.log(1);");
    const { runner, calls } = recordingRunner();
    const registry = createDefaultRegistry().register(guiExtension);

    const result = build(entry, {
      emit: "exe",
      outDir: join(directory, "out"),
      cacheDir: join(directory, ".cache"),
      runner,
      clang: "clang",
      preferPrebuilt: false,
      extensions: registry,
    });

    expect(result.diagnostics).toHaveLength(0);
    const linkCall = calls.at(-1)!;
    expect(linkCall.args.some((arg) => arg.includes("icon-") && arg.endsWith(".o"))).toBe(true);
  });

  it("reports a missing icon as an IO diagnostic", () => {
    const directory = temporaryDirectory();
    const entry = join(directory, "main.ts");
    writeFileSync(entry, "console.log(1);");
    const result = build(entry, {
      emit: "ir",
      outDir: join(directory, "out"),
      cacheDir: join(directory, ".cache"),
      icon: join(directory, "missing.png"),
    });
    expect(result.diagnostics).toHaveLength(1);
    expect(result.diagnostics[0]!.code).toBe(DiagnosticCode.IOError);
  });
});
