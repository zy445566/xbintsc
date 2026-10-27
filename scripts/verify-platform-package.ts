#!/usr/bin/env node
/**
 * Verify the `@xbintsc/<os>-<arch>` npm tarballs staged for a release.
 *
 * `release.yml` publishes one platform package per host before `xbintsc` and
 * then points the main manifest's `optionalDependencies` at exactly those
 * versions, so every tarball has to prove it was packed from this release: its
 * manifest must name `@xbintsc/<slug>@<version>` and it must carry the runtime
 * archives the driver links (`core` and `ext_node`, see `requiredArchives`).
 *
 * This runs in Node on purpose. The check used to be a shell pipeline,
 * `tar -tf x.tgz | grep -q <pattern>`, under `set -o pipefail`: `grep -q` closes
 * the pipe on its first match, so `tar` — still decompressing the tens of
 * megabytes that follow `core.a` — dies of SIGPIPE, and the pipeline reports a
 * perfectly good tarball as "has no core runtime archive". Reading the gzip
 * stream here has no such race, needs no external `tar` and behaves the same on
 * every host.
 *
 * Usage: `npm run verify:platform -- --dir platform-packages [--version x.y.z]`
 */

import { existsSync, readFileSync } from "node:fs";
import { basename, dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { gunzipSync } from "node:zlib";
import {
  PLATFORM_SLUGS,
  platformPackageFileName,
  platformPackageName,
  requiredArchives,
} from "./package-platform.js";

/** One member of a tar archive; `data` is a view into the decompressed stream. */
export interface TarEntry {
  readonly path: string;
  readonly data: Buffer;
}

const BLOCK = 512;

/** Read a header field, stopping at its NUL padding. */
function readString(block: Buffer, offset: number, length: number): string {
  const field = block.subarray(offset, offset + length);
  const end = field.indexOf(0);
  return field.subarray(0, end === -1 ? field.length : end).toString("utf8");
}

/** Decode a numeric header field (octal, or base-256 for very large values). */
function readNumber(block: Buffer, offset: number, length: number): number {
  if ((block[offset]! & 0x80) !== 0) {
    let value = block[offset]! & 0x7f;
    for (let index = 1; index < length; index += 1) value = value * 256 + block[offset + index]!;
    return value;
  }
  const text = readString(block, offset, length).trim();
  return text === "" ? 0 : Number.parseInt(text, 8);
}

/** The `path=` record of a pax extended header, when it carries one. */
function paxPath(data: Buffer): string | undefined {
  // Records are `<byte length> <key>=<value>\n`; only the path matters here.
  const match = /(?:^|\n)\d+ path=([^\n]*)\n/.exec(data.toString("utf8"));
  return match?.[1];
}

/** List the regular files of a gzip-compressed tar archive, in archive order. */
export function tarballEntries(tgz: Buffer, source = "tarball"): TarEntry[] {
  let tar: Buffer;
  try {
    tar = gunzipSync(tgz);
  } catch (error) {
    const reason = error instanceof Error ? error.message : String(error);
    throw new Error(`${source} is not a gzip-compressed tar archive (${reason})`);
  }
  const entries: TarEntry[] = [];
  let offset = 0;
  let override: string | undefined;
  while (offset + BLOCK <= tar.length) {
    const header = tar.subarray(offset, offset + BLOCK);
    if (header.every((byte) => byte === 0)) break;
    const size = readNumber(header, 124, 12);
    const type = header[156] === 0 ? "0" : String.fromCharCode(header[156]!);
    const dataStart = offset + BLOCK;
    if (dataStart + size > tar.length) throw new Error(`${source} is truncated`);
    const data = tar.subarray(dataStart, dataStart + size);
    offset = dataStart + Math.ceil(size / BLOCK) * BLOCK;

    // A long path arrives in its own preceding header: GNU `L` stores it
    // verbatim, pax `x` records it as `path=`. `K` only renames a link target.
    if (type === "L") {
      override = readString(data, 0, data.length);
      continue;
    }
    if (type === "x") {
      override = paxPath(data) ?? override;
      continue;
    }
    if (type === "K") continue;

    const prefix = readString(header, 345, 155);
    const name = readString(header, 0, 100);
    const path = override ?? (prefix === "" ? name : `${prefix}/${name}`);
    override = undefined;
    // Only regular files can be the archives checked below; directories and
    // links are skipped instead of being reported by their trailing slash.
    if (type === "0" || type === "7") entries.push({ path, data });
  }
  return entries;
}

/** Read the manifest out of an already-parsed platform tarball. */
function manifestOf(entries: readonly TarEntry[], source: string): { name?: string; version?: string } {
  const manifest = entries.find((entry) => entry.path === "package/package.json");
  if (!manifest) throw new Error(`${source} has no package/package.json`);
  try {
    return JSON.parse(manifest.data.toString("utf8")) as { name?: string; version?: string };
  } catch {
    throw new Error(`${source} has an unreadable package/package.json`);
  }
}

/**
 * Check one platform tarball's manifest identity and runtime archives.
 * `source` names the tarball in error messages (its path, or a fixture name).
 */
export function verifyPlatformEntries(
  entries: readonly TarEntry[],
  slug: string,
  version: string,
  source = "platform package",
): void {
  const parsed = manifestOf(entries, source);
  const expectedName = platformPackageName(slug);
  if (parsed.name !== expectedName || parsed.version !== version) {
    throw new Error(`${source} contains ${parsed.name}@${parsed.version}, expected ${expectedName}@${version}`);
  }

  const paths = new Set(entries.map((entry) => entry.path));
  for (const archive of requiredArchives(slug)) {
    const expected = `package/runtime/lib/${slug}/${archive}`;
    if (paths.has(expected)) continue;
    // Keep the historical wording: `core.a`/`core.lib` is "the core archive".
    const label = archive.replace(/\.(a|lib)$/, "");
    throw new Error(`${source} has no ${label} runtime archive (expected ${expected})`);
  }
}

/** Read `tgzPath` and verify it as the platform package for `slug`/`version`. */
export function verifyPlatformPackage(tgzPath: string, slug: string, version: string): TarEntry[] {
  const entries = tarballEntries(readFileSync(tgzPath), tgzPath);
  verifyPlatformEntries(entries, slug, version, tgzPath);
  return entries;
}

/** Value of `--name <value>`, or `undefined` when the flag is absent. */
function argument(name: string): string | undefined {
  const index = process.argv.indexOf(name);
  return index >= 0 ? process.argv[index + 1] : undefined;
}

const invokedDirectly =
  process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href;

if (invokedDirectly) {
  try {
    const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
    const dir = argument("--dir") ?? join(root, "platform-packages");
    const version =
      argument("--version") ??
      (JSON.parse(readFileSync(join(root, "package.json"), "utf8")) as { version: string }).version;
    for (const slug of PLATFORM_SLUGS) {
      const file = join(dir, platformPackageFileName(slug, version));
      if (!existsSync(file)) throw new Error(`Missing npm platform package ${file}.`);
      const entries = verifyPlatformPackage(file, slug, version);
      console.log(`xbintsc: ${basename(file)} carries ${entries.length} files`);
    }
    console.log(`xbintsc: verified ${PLATFORM_SLUGS.length} platform packages at ${version}`);
  } catch (error) {
    console.error(`xbintsc: ${error instanceof Error ? error.message : String(error)}`);
    process.exit(1);
  }
}
