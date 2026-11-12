#!/usr/bin/env node
/**
 * Fetch the third-party C sources that xbintsc compiles, into `vendor/`.
 *
 * Those libraries are vendored *by version*, not committed: the repository
 * tracks neither their code nor their line count. Each entry pins an immutable
 * upstream revision and the SHA-256 of the extracted file, so a changed pin is
 * a reviewable change and a corrupted download can never reach a build.
 *
 * Layout (sibling of `runtime/`, so `findVendorDir()` can find it in a source
 * checkout, an unpacked release and an npm install alike):
 *
 *   vendor/stb/stb_image.h, stb_truetype.h
 *   vendor/nanosvg/nanosvg.h, nanosvgrast.h
 *   vendor/miniz/…                    the `compression` link feature
 *   vendor/sqlite3/sqlite3.{c,h}      the `storage` link feature
 *
 * `build-gui.ts` fetches just `stb`/`nanosvg`; `npm test` fetches everything.
 * CI caches the four directories keyed on this script.
 *
 * Note: miniz's upstream tree has no `miniz_export.h` (its CMake build generates
 * one). The static-only shim below is written here rather than tracked.
 *
 * Run via `npm run vendor` (or `tsx scripts/fetch-vendored.ts`).
 */

import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { copyFileSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { basename, dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { downloadTarball } from "./fetch-tarball.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
export const vendorDir = join(root, "vendor");

/** The vendored packages, each a directory under `vendor/`. */
export const VENDOR_PACKAGES = ["stb", "nanosvg", "miniz", "sqlite3"] as const;
export type VendorPackage = (typeof VENDOR_PACKAGES)[number];

interface VendoredFile {
  readonly package: Exclude<VendorPackage, "sqlite3">;
  readonly name: string;
  readonly url: string;
  readonly sha256: string;
}

/* Pinned upstream revisions. STB and NanoSVG have no releases, so the commit
 * is the version; miniz is pinned to the commit whose files this tree uses. */
const STB_IMAGE = "013ac3beddff3dbffafd5177e7972067cd2b5083";
const STB_TRUETYPE = "c0c982601f40183e74d84a61237e968dca08380e";
const NANOSVG = "239e102ec2c691f2902e20ace2ed36ee4a35cfe6";
const MINIZ = "2c50092b3a4010760df02e7842960abdc3f25ee3";

const RAW = "https://raw.githubusercontent.com";

const FILES: readonly VendoredFile[] = [
  {
    package: "stb",
    name: "stb_image.h",
    url: `${RAW}/nothings/stb/${STB_IMAGE}/stb_image.h`,
    sha256: "594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3",
  },
  {
    package: "stb",
    name: "stb_truetype.h",
    url: `${RAW}/nothings/stb/${STB_TRUETYPE}/stb_truetype.h`,
    sha256: "a34d8d536ce7c11b9163ab2d524721c1f4df1452cce6595c4f11d3048384f925",
  },
  {
    package: "nanosvg",
    name: "nanosvg.h",
    url: `${RAW}/memononen/nanosvg/${NANOSVG}/src/nanosvg.h`,
    sha256: "e34fd5d084be106cea972d19ce5d27fd96d17ba89f8d06bdceee058420c8b2b0",
  },
  {
    package: "nanosvg",
    name: "nanosvgrast.h",
    url: `${RAW}/memononen/nanosvg/${NANOSVG}/src/nanosvgrast.h`,
    sha256: "79a9c5f4db19debf9f3a648a1589e96d92854f245a5cb4f3d823f263785234d8",
  },
  ...(
    [
      ["miniz.c", "29a0c08c4610d20ab18381cbf18190f91b4b41d05c242376364756061b631f1d"],
      ["miniz.h", "2b9c48355793988f0d12b3cef357e52b741d744385157b59cb422237ea226501"],
      ["miniz_common.h", "d0770cc478923e33bf22820fc51bbe22b6b99c3230ce4f5fc10eea3debc75623"],
      ["miniz_tdef.c", "7b3baaa59f50116eb3e72a23302c505b76cb01cc456672f7957a74d9e7b44047"],
      ["miniz_tdef.h", "123050e3c876519828d3934899e0b003ad13a98114919d94f1e06b4b9d02a925"],
      ["miniz_tinfl.c", "7113a08a7722d671ce89b8de61fe6d02d0cadca0ff274ba357bf2a9dc85745ca"],
      ["miniz_tinfl.h", "2c89615a4cbc5de4eac5b861051849392ec698264ee7dd399ea587c592cf68d1"],
      ["miniz_zip.h", "43d09867f79993a9ba1627cb1f3bbd78d3bf5ee0c91cd6e7527aa9ca1e3aed44"],
    ] as const
  ).map(([name, sha256]) => ({
    package: "miniz" as const,
    name,
    url: `${RAW}/richgel999/miniz/${MINIZ}/${name}`,
    sha256,
  })),
];

/** miniz's static-build export shim (upstream generates it with CMake). */
const MINIZ_EXPORT_H =
  "/* Minimal export macro for a static miniz build (no DLL import/export). */\n" +
  "#ifndef MINIZ_EXPORT\n" +
  "#define MINIZ_EXPORT\n" +
  "#endif\n";

/* SQLite publishes one amalgamation zip per release; only the two files below
 * are compiled. The zip itself is pinned as well: a truncated download is a
 * download to retry, not an amalgamation to build. */
const SQLITE_PACKAGE = "sqlite-amalgamation-3540100";
const SQLITE_URL = `https://www.sqlite.org/2026/${SQLITE_PACKAGE}.zip`;
const SQLITE_ZIP_SHA256 = "0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9";
const SQLITE_FILES = [
  {
    name: "sqlite3.c",
    sha256: "0c657666d819f261cc4007526ad57e1669de9949df7a8ea847aa284a2bf0ee52",
  },
  {
    name: "sqlite3.h",
    sha256: "d6c84557261893439b341d2e6c35187ae3ea24fbfd843ad94616f72d89242b8c",
  },
] as const;

function sha256(file: string): string {
  return createHash("sha256").update(readFileSync(file)).digest("hex");
}

function isPinned(file: string, digest: string): boolean {
  return existsSync(file) && sha256(file) === digest;
}

/** Download one file, refusing to leave a mismatched (stale or corrupted) file. */
function ensureFile(file: VendoredFile): void {
  const target = join(vendorDir, file.package, file.name);
  if (isPinned(target, file.sha256)) return;
  mkdirSync(dirname(target), { recursive: true });
  rmSync(target, { force: true });
  downloadTarball(target, file.url, `${file.package}/${file.name}`);
  if (isPinned(target, file.sha256)) return;
  const actual = sha256(target);
  rmSync(target, { force: true });
  throw new Error(
    `${file.package}/${file.name}: expected sha256 ${file.sha256}, got ${actual}. ` +
      "The upstream pin moved; update the URL and hash in scripts/fetch-vendored.ts deliberately.",
  );
}

/** Write the generated miniz export shim (no upstream file to download). */
function ensureMinizExportHeader(): void {
  const target = join(vendorDir, "miniz", "miniz_export.h");
  if (existsSync(target) && readFileSync(target, "utf8") === MINIZ_EXPORT_H) return;
  mkdirSync(dirname(target), { recursive: true });
  writeFileSync(target, MINIZ_EXPORT_H);
}

/** Extract a zip archive; Windows' bsdtar and POSIX's unzip cover the hosts. */
function extractZip(zip: string, destination: string): void {
  const attempts: readonly (readonly [string, readonly string[]])[] =
    process.platform === "win32"
      ? [
          ["tar", ["-xf", zip, "-C", destination]],
          ["unzip", ["-q", "-o", zip, "-d", destination]],
        ]
      : [
          ["unzip", ["-q", "-o", zip, "-d", destination]],
          ["tar", ["-xf", zip, "-C", destination]],
        ];
  for (const [command, args] of attempts) {
    if (spawnSync(command, args, { stdio: "ignore" }).status === 0) return;
  }
  throw new Error(`cannot extract ${basename(zip)}; install unzip (or a tar that reads zip archives)`);
}

/** Fetch the pinned SQLite amalgamation and copy out its two sources. */
function ensureSqlite(): void {
  const directory = join(vendorDir, "sqlite3");
  if (SQLITE_FILES.every((file) => isPinned(join(directory, file.name), file.sha256))) return;

  const cache = join(vendorDir, ".cache");
  mkdirSync(cache, { recursive: true });
  const zip = join(cache, `${SQLITE_PACKAGE}.zip`);
  if (!isPinned(zip, SQLITE_ZIP_SHA256)) {
    rmSync(zip, { force: true });
    downloadTarball(zip, SQLITE_URL, `${SQLITE_PACKAGE}.zip`);
    if (!isPinned(zip, SQLITE_ZIP_SHA256)) {
      const actual = sha256(zip);
      rmSync(zip, { force: true });
      throw new Error(`${SQLITE_PACKAGE}.zip: expected sha256 ${SQLITE_ZIP_SHA256}, got ${actual}`);
    }
  }

  const extracted = join(cache, SQLITE_PACKAGE);
  rmSync(extracted, { recursive: true, force: true });
  mkdirSync(extracted, { recursive: true });
  extractZip(zip, extracted);
  const sourceDir = join(extracted, SQLITE_PACKAGE);
  if (!existsSync(join(sourceDir, "sqlite3.c"))) {
    throw new Error(`expected ${SQLITE_PACKAGE}/sqlite3.c after extracting ${basename(zip)}`);
  }

  mkdirSync(directory, { recursive: true });
  for (const file of SQLITE_FILES) {
    const target = join(directory, file.name);
    copyFileSync(join(sourceDir, file.name), target);
    if (!isPinned(target, file.sha256)) {
      const actual = sha256(target);
      rmSync(target, { force: true });
      throw new Error(`sqlite3/${file.name}: expected sha256 ${file.sha256}, got ${actual}`);
    }
  }
  rmSync(extracted, { recursive: true, force: true });
  rmSync(zip, { force: true });
}

/**
 * Ensure the requested vendored packages exist and match their pins.
 * Downloads only what is missing; everything else is a content check.
 */
export function ensureVendoredSources(packages: readonly VendorPackage[] = VENDOR_PACKAGES): void {
  for (const name of packages) {
    if (name === "sqlite3") {
      ensureSqlite();
      continue;
    }
    for (const file of FILES) {
      if (file.package === name) ensureFile(file);
    }
    if (name === "miniz") ensureMinizExportHeader();
  }
}

const invokedDirectly =
  process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href;

if (invokedDirectly) {
  try {
    ensureVendoredSources();
    console.log("xbintsc: vendored sources are present and match their pins");
  } catch (error) {
    console.error(`xbintsc: ${error instanceof Error ? error.message : String(error)}`);
    process.exit(1);
  }
}
