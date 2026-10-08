#!/usr/bin/env node
/**
 * Enforce the per-file line budget for code ESLint cannot parse.
 *
 * ESLint's `max-lines` rule already caps the TypeScript/JavaScript sources at
 * 600 lines (see `eslint.config.js`). This script applies the same budget to
 * the runtime's C, C++, Rust and `#include`d `.inc` fragments plus shell
 * scripts, so *every* code file stays focused and small.
 *
 * Vendored third-party sources that must ship verbatim (notably the
 * single-header `stb_image.h`) are exempt: they are not ours to split.
 *
 * Run via `npm run lint` (or `tsx scripts/check-file-length.ts`).
 */

import { readdirSync, readFileSync } from "node:fs";
import { join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(fileURLToPath(new URL(".", import.meta.url)), "..");

/** Maximum number of lines allowed in a single code file. */
const MAX_LINES = 600;

const CODE_EXTENSIONS = new Set([".c", ".h", ".cc", ".cpp", ".hpp", ".rs", ".inc", ".sh"]);

const IGNORED_DIRECTORIES = new Set([
  ".git",
  "build",
  "coverage",
  "dist",
  "node_modules",
  "scratch",
  "target",
  "vendor",
]);

/** Vendored third-party sources that ship verbatim and are not ours to split. */
const VENDORED_FILES = new Set([join("runtime", "ext_gui", "stb_image.h")]);

/** Machine-generated files: they are reproduced from their sources by a script,
 * so their size is not a sign of a module that needs splitting. */
const GENERATED_FILES = new Set([join("runtime", "ext_gui", "renderer_shaders_data.h")]);

interface Offense {
  file: string;
  lines: number;
}

function countLines(path: string): number {
  const content = readFileSync(path, "utf8");
  if (content.length === 0) return 0;
  // A trailing newline terminates the last line instead of starting a new one.
  const lines = content.split("\n");
  return content.endsWith("\n") ? lines.length - 1 : lines.length;
}

function collect(dir: string, offenses: Offense[]): void {
  for (const entry of readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
    const full = join(dir, entry.name);
    if (entry.isDirectory()) {
      if (IGNORED_DIRECTORIES.has(entry.name)) continue;
      // `runtime/lib` holds prebuilt archives, never sources.
      if (relative(root, full) === join("runtime", "lib")) continue;
      collect(full, offenses);
      continue;
    }
    if (!entry.isFile()) continue;
    const dot = entry.name.lastIndexOf(".");
    if (dot < 0 || !CODE_EXTENSIONS.has(entry.name.slice(dot))) continue;
    const file = relative(root, full);
    if (VENDORED_FILES.has(file) || GENERATED_FILES.has(file)) continue;
    const lines = countLines(full);
    if (lines > MAX_LINES) offenses.push({ file, lines });
  }
}

const offenses: Offense[] = [];
collect(root, offenses);

if (offenses.length > 0) {
  console.error(`xbintsc: code files must stay under ${MAX_LINES} lines - split them by responsibility:`);
  for (const offense of offenses.sort((a, b) => b.lines - a.lines)) {
    console.error(`  ${offense.file}: ${offense.lines} lines`);
  }
  process.exit(1);
}

console.log(`xbintsc: all code files are within ${MAX_LINES} lines`);
