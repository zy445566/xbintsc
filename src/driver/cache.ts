/**
 * Content-addressed incremental build cache.
 *
 * The cache keys a build on everything that can change the output: the entry
 * source hash, the compiler version, the resolved options and the set of active
 * extensions. If nothing changed and the recorded outputs still exist, the
 * build is skipped entirely.
 *
 * It is deliberately a single JSON manifest so it is easy to inspect, diff and
 * invalidate; a future content-addressed object store can replace it without
 * touching callers.
 */

import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

export interface CacheEntry {
  readonly key: string;
  readonly outputs: readonly string[];
  readonly createdAt: number;
}

export interface CacheManifest {
  readonly version: 1;
  readonly entries: Record<string, CacheEntry>;
}

export function hashString(text: string): string {
  return createHash("sha256").update(text).digest("hex").slice(0, 32);
}

export function hashParts(parts: readonly string[]): string {
  const hash = createHash("sha256");
  for (const part of parts) {
    hash.update(String(part.length));
    hash.update(":");
    hash.update(part);
    hash.update(";");
  }
  return hash.digest("hex").slice(0, 32);
}

export class BuildCache {
  private manifest: CacheManifest = { version: 1, entries: {} };
  private dirty = false;

  constructor(private readonly directory: string) {
    this.load();
  }

  private get manifestPath(): string {
    return join(this.directory, "build-cache.json");
  }

  private load(): void {
    if (!existsSync(this.manifestPath)) return;
    try {
      const parsed = JSON.parse(readFileSync(this.manifestPath, "utf8")) as CacheManifest;
      if (parsed.version === 1 && parsed.entries) this.manifest = parsed;
    } catch {
      // A corrupt cache is never fatal; start from scratch.
      this.manifest = { version: 1, entries: {} };
    }
  }

  save(): void {
    if (!this.dirty) return;
    mkdirSync(this.directory, { recursive: true });
    writeFileSync(this.manifestPath, JSON.stringify(this.manifest, null, 2));
    this.dirty = false;
  }

  /** True when `key` was built before and every recorded output still exists. */
  isFresh(key: string, outputs: readonly string[]): boolean {
    const entry = this.manifest.entries[key];
    if (!entry) return false;
    const recorded = outputs.length > 0 ? outputs : entry.outputs;
    if (recorded.length === 0) return false;
    return recorded.every((output) => existsSync(output));
  }

  record(key: string, outputs: readonly string[]): void {
    this.manifest.entries[key] = { key, outputs: [...outputs], createdAt: Date.now() };
    this.dirty = true;
  }

  clear(): void {
    this.manifest = { version: 1, entries: {} };
    this.dirty = true;
  }

  entries(): readonly CacheEntry[] {
    return Object.values(this.manifest.entries);
  }
}
