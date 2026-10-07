/**
 * macOS `.app` bundle generation.
 *
 * A bare Mach-O executable cannot carry a Finder icon, so when `app.bundle` is
 * enabled on macOS the driver lays out a minimal bundle next to the executable:
 *
 *     Demo.app/Contents/Info.plist
 *     Demo.app/Contents/MacOS/Demo
 *     Demo.app/Contents/Resources/AppIcon.icns
 *
 * An `.icns` input is copied verbatim; a `.png` is converted with the system
 * `sips` + `iconutil` tools when available, otherwise the PNG is shipped and the
 * app relies on the runtime Dock icon (set by the `gui` extension).
 */

import { chmodSync, copyFileSync, existsSync, mkdirSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import type { IconInfo } from "./icon.js";
import type { Runner } from "./toolchain.js";

export interface MacBundleOptions {
  readonly executablePath: string;
  /** Display name; also the executable name inside the bundle. */
  readonly name: string;
  readonly bundleId: string;
  readonly icon?: IconInfo;
  readonly runner: Runner;
  readonly env?: Record<string, string>;
}

/** `<dir>/<name>.app` for an executable at `<dir>/<name>`. */
export function bundlePathFor(executablePath: string, name: string): string {
  return join(dirname(executablePath), `${name}.app`);
}

/** Escape the five XML predefined entities for the plist values. */
function xmlEscape(text: string): string {
  return text
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

export interface PlistOptions {
  readonly name: string;
  readonly bundleId: string;
  readonly executable: string;
  readonly iconFile?: string;
}

/** Render a minimal `Info.plist` for a GUI application bundle. */
export function infoPlist(options: PlistOptions): string {
  const entries: [string, string][] = [
    ["CFBundleName", options.name],
    ["CFBundleDisplayName", options.name],
    ["CFBundleIdentifier", options.bundleId],
    ["CFBundleExecutable", options.executable],
    ["CFBundlePackageType", "APPL"],
    ["CFBundleInfoDictionaryVersion", "6.0"],
    ["CFBundleShortVersionString", "1.0.0"],
    ["CFBundleVersion", "1"],
    ["LSMinimumSystemVersion", "10.13"],
    ["NSHighResolutionCapable", "true"],
  ];
  if (options.iconFile) entries.push(["CFBundleIconFile", options.iconFile]);
  const body = entries
    .map(([key, value]) => `\t<key>${key}</key>\n\t<string>${xmlEscape(value)}</string>`)
    .join("\n");
  return [
    '<?xml version="1.0" encoding="UTF-8"?>',
    '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">',
    '<plist version="1.0">',
    "<dict>",
    body,
    "</dict>",
    "</plist>",
    "",
  ].join("\n");
}

const ICONSET_SIZES = [16, 32, 64, 128, 256, 512] as const;

/** Convert a PNG to `.icns` via `sips` + `iconutil`; false when unavailable. */
function convertPngToIcns(
  runner: Runner,
  pngPath: string,
  outIcns: string,
  env?: Record<string, string>,
): boolean {
  const iconsetDir = join(dirname(outIcns), "AppIcon.iconset");
  mkdirSync(iconsetDir, { recursive: true });
  try {
    for (const size of ICONSET_SIZES) {
      for (const scale of [1, 2] as const) {
        const pixels = size * scale;
        if (pixels > 1024) continue;
        const suffix = scale === 1 ? "" : "@2x";
        const out = join(iconsetDir, `icon_${size}x${size}${suffix}.png`);
        const result = runner.run(
          "sips",
          ["-z", String(pixels), String(pixels), pngPath, "--out", out],
          { env },
        );
        if (result.status !== 0) return false;
      }
    }
    const result = runner.run("iconutil", ["-c", "icns", iconsetDir, "-o", outIcns], { env });
    return result.status === 0 && existsSync(outIcns);
  } catch {
    return false;
  } finally {
    rmSync(iconsetDir, { recursive: true, force: true });
  }
}

/**
 * Lay out the `.app` bundle containing a copy of `executablePath` and, when
 * available, the icon. Returns the bundle path. Never throws for a missing
 * image-conversion tool: it falls back to shipping the PNG.
 */
export function packageMacApp(options: MacBundleOptions): string {
  const { executablePath, name, bundleId, icon, runner } = options;
  const bundleDir = bundlePathFor(executablePath, name);
  const contents = join(bundleDir, "Contents");
  const macosDir = join(contents, "MacOS");
  const resourcesDir = join(contents, "Resources");
  mkdirSync(macosDir, { recursive: true });
  mkdirSync(resourcesDir, { recursive: true });

  const innerExecutable = join(macosDir, name);
  copyFileSync(executablePath, innerExecutable);
  chmodSync(innerExecutable, 0o755);

  let iconFile: string | undefined;
  if (icon) {
    const icnsPath = join(resourcesDir, "AppIcon.icns");
    if (icon.format === "icns") {
      copyFileSync(icon.path, icnsPath);
      iconFile = "AppIcon";
    } else if (icon.format === "png") {
      if (convertPngToIcns(runner, icon.path, icnsPath, options.env)) {
        iconFile = "AppIcon";
      } else {
        copyFileSync(icon.path, join(resourcesDir, "AppIcon.png"));
        iconFile = "AppIcon.png";
      }
    }
  }

  writeFileSync(
    join(contents, "Info.plist"),
    infoPlist({ name, bundleId, executable: name, ...(iconFile ? { iconFile } : {}) }),
  );
  return bundleDir;
}
