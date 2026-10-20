/**
 * Content fingerprints for the incremental cache.
 *
 * The executable cache is keyed on everything that can change the linked
 * binary but is not part of the entry source: the C runtime, the compiler's own
 * sources, and whatever a native extension contributes. The per-object cache
 * cannot express these, because the freshness check runs before compilation.
 */

import { existsSync, readFileSync, readdirSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import type { ExtensionRegistry } from "../extensions/registry.js";
import { hashParts } from "./cache.js";

/** Runtime sources, headers and `#include`d `.inc` fragments, plus prebuilt archives. */
export function runtimeFingerprint(runtimeDir: string): string {
  if (!existsSync(runtimeDir)) return "";
  const parts: string[] = [];
  const walk = (dir: string): void => {
    for (const entry of readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
      const full = join(dir, entry.name);
      if (entry.isDirectory()) {
        walk(full);
      } else if (/\.(c|h|inc)$/.test(entry.name)) {
        parts.push(entry.name, readFileSync(full, "utf8"));
      } else {
        parts.push(entry.name, readFileSync(full).toString("base64"));
      }
    }
  };
  walk(runtimeDir);
  return hashParts(parts);
}

/**
 * Content hash of the compiler's own TypeScript sources, when they are present.
 *
 * `COMPILER_VERSION` is bumped per release, but a developer editing codegen
 * between releases would otherwise reuse a binary produced by the previous
 * compiler for the same input. Hashing `src/` closes that hole in a source
 * checkout. A released archive ships only `bin/` and `runtime/`, so the marker
 * check fails there and the version alone keys the cache — which is correct,
 * because release automation keeps it in sync with package.json.
 */
export function compilerFingerprint(): string {
  try {
    // In a checkout this module is `<root>/src/driver/fingerprint.ts`.
    const sourceRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
    if (!existsSync(join(sourceRoot, "codegen", "llvm.ts"))) return "";
    if (!existsSync(join(sourceRoot, "driver", "compiler.ts"))) return "";
    const parts: string[] = [];
    const walk = (dir: string): void => {
      for (const entry of readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
        const full = join(dir, entry.name);
        if (entry.isDirectory()) {
          walk(full);
        } else if (entry.name.endsWith(".ts")) {
          parts.push(relative(sourceRoot, full).replace(/\\/g, "/"), readFileSync(full, "utf8"));
        }
      }
    };
    walk(sourceRoot);
    return hashParts(parts);
  } catch {
    // A self-hosted or packaged compiler may not expose its sources.
    return "";
  }
}

/**
 * Everything an extension contributes besides its C sources: name, linker flags
 * and the contents of pre-built C++/Rust objects.
 */
export function registryFingerprint(registry: ExtensionRegistry): string {
  const parts: string[] = [];
  for (const extension of registry.all()) {
    parts.push(extension.name);
    parts.push(...(extension.linkerFlags?.() ?? []));
    for (const object of extension.nativeObjects?.() ?? []) {
      parts.push(object, existsSync(object) ? readFileSync(object).toString("base64") : "");
    }
  }
  return hashParts(parts);
}
