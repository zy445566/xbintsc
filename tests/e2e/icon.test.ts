/**
 * End-to-end icon tests: compile a GUI program with `app.icon`, run it headless
 * and verify the icon bytes really ended up in the binary. The macOS bundle
 * branch is exercised on darwin only. Skipped where the GUI archive is missing
 * (run `npm run gui`) or on Windows.
 */

import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { build } from "../../src/driver/compiler.js";
import type { AppConfig } from "../../src/driver/config.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { guiExtension } from "../../src/extensions/gui/index.js";
import { guiAvailable } from "./gui-helpers.js";

const LOGO_PNG_BASE64 =
  "iVBORw0KGgoAAAANSUhEUgAAAAQAAAAECAYAAACp8Z5+AAAAFElEQVR42mP4z8DwH4Sh1H8G0gUALFAf4eNWqTEAAAAASUVORK5CYII=";

const SOURCE = [
  'import { createWindow, run } from "gui";',
  "const win = createWindow({ title: 'icon', width: 320, height: 240 });",
  "console.log(`window=${win !== undefined}`);",
  "run();",
].join("\n");

describe.skipIf(!guiAvailable)("application icon", () => {
  let workdir = "";
  let logoPath = "";
  let png: Buffer;

  beforeAll(() => {
    workdir = mkdtempSync(join(tmpdir(), "xbintsc-icon-e2e-"));
    png = Buffer.from(LOGO_PNG_BASE64, "base64");
    logoPath = join(workdir, "logo.png");
    writeFileSync(logoPath, png);
  });

  afterAll(() => {
    rmSync(workdir, { recursive: true, force: true });
  });

  function compile(name: string, app: AppConfig): string {
    const entry = join(workdir, `${name}.ts`);
    writeFileSync(entry, SOURCE);
    const result = build(entry, {
      emit: "exe",
      outDir: join(workdir, `out-${name}`),
      cacheDir: join(workdir, `.cache-${name}`),
      extensions: createDefaultRegistry().register(guiExtension),
      preferPrebuilt: true,
      app,
    });
    expect(result.diagnostics.filter((diagnostic) => diagnostic.category === "error")).toEqual([]);
    return result.outputPath;
  }

  it("embeds the icon bytes and runs", () => {
    const outputPath = compile("withicon", { icon: logoPath });
    expect(readFileSync(outputPath).includes(png)).toBe(true);

    const executed = spawnSync(outputPath, [], {
      encoding: "utf8",
      env: { ...process.env, XT_GUI_AUTOCLOSE_MS: "400" },
    });
    expect(executed.status, executed.stderr).toBe(0);
    expect(executed.stdout).toContain("window=true");
  });

  it.skipIf(process.platform !== "darwin")("produces a macOS .app bundle with an icon", () => {
    const entry = join(workdir, "bundled.ts");
    writeFileSync(entry, SOURCE);
    const result = build(entry, {
      emit: "exe",
      outDir: join(workdir, "out-bundled"),
      cacheDir: join(workdir, ".cache-bundled"),
      extensions: createDefaultRegistry().register(guiExtension),
      app: { name: "IconDemo", icon: logoPath, bundle: true, bundleId: "com.example.icondemo" },
    });
    expect(result.diagnostics.filter((diagnostic) => diagnostic.category === "error")).toEqual([]);
    expect(result.bundlePath).toBeDefined();
    const bundle = result.bundlePath!;
    expect(existsSync(join(bundle, "Contents", "MacOS", "IconDemo"))).toBe(true);
    expect(existsSync(join(bundle, "Contents", "Resources", "AppIcon.icns"))).toBe(true);
    const plist = readFileSync(join(bundle, "Contents", "Info.plist"), "utf8");
    expect(plist).toContain("<string>com.example.icondemo</string>");
    expect(plist).toContain("<string>AppIcon</string>");
  });
});
