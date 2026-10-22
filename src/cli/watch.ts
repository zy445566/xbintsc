/**
 * `xbintsc watch` support: fingerprint a source tree and rebuild when it
 * changes.
 *
 * The scan is deliberately polling-based. A self-hosted xbintsc binary has no
 * `fs.watch` backend in the Node extension, and polling works identically on
 * every platform. The tree is the entry file's directory, with build outputs
 * and dependency directories excluded; package sources inside `node_modules`
 * are not watched (run the build once after installing a dependency).
 */

import { readdirSync, statSync, type Dirent } from "node:fs";
import { extname, join, resolve } from "node:path";

const WATCHED_EXTENSIONS = new Set([
  ".ts",
  ".tsx",
  ".js",
  ".mjs",
  ".cjs",
  ".json",
  ".html",
  ".css",
]);

const IGNORED_DIRECTORIES = new Set([
  "node_modules",
  ".git",
  ".github",
  ".xbintsc",
  ".xbintsc-cache",
  "build",
  "dist",
  "coverage",
  "scratch",
  "target",
  "vendor",
]);

/** One string that changes whenever a watched file changes. */
export function fingerprintTree(root: string): string {
  const entries: string[] = [];
  collect(resolve(root), entries);
  entries.sort();
  return entries.join("|");
}

function collect(directory: string, out: string[]): void {
  let entries: Dirent[];
  try {
    entries = readdirSync(directory, { withFileTypes: true });
  } catch {
    return; // the directory disappeared between scans
  }
  for (const entry of entries) {
    const path = join(directory, entry.name);
    if (entry.isDirectory()) {
      if (!IGNORED_DIRECTORIES.has(entry.name)) collect(path, out);
      continue;
    }
    if (!entry.isFile()) continue;
    if (!WATCHED_EXTENSIONS.has(extname(entry.name).toLowerCase())) continue;
    try {
      const stat = statSync(path);
      out.push(`${path}:${stat.mtimeMs}:${stat.size}`);
    } catch {
      // A file removed during the scan is picked up by the next iteration.
    }
  }
}

export interface WatchOptions {
  /** Current fingerprint of everything that should trigger a rebuild. */
  readonly scan: () => string;
  /** Called after the fingerprint changes. Must be synchronous. */
  readonly onRebuild: () => void;
  /** Poll interval; defaults to 300 ms. */
  readonly intervalMs?: number;
}

/**
 * Start polling. Returns a stop function. The caller performs the initial
 * build, so this never fires `onRebuild` for the state it starts in.
 */
export function startWatching(options: WatchOptions): () => void {
  let previous = options.scan();
  const timer = setInterval(() => {
    const next = options.scan();
    if (next === previous) return;
    previous = next;
    options.onRebuild();
  }, options.intervalMs ?? 300);
  return () => clearInterval(timer);
}
