#!/usr/bin/env node
/**
 * xbintsc launcher.
 *
 * Resolution order:
 *   1. a native compiler for this platform — an `xbintsc_BINARY` override, an
 *      installed `@xbintsc/<os>-<arch>` package, or a co-located `bin/xbintsc`.
 *      This is what a packaged install provides; it needs no Node at runtime.
 *   2. the compiled CLI in `dist/` (a checkout that ran `npm run build`).
 *   3. the TypeScript sources through `tsx` (a plain checkout).
 */

import { existsSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { spawnSync } from "node:child_process";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const exe = process.platform === "win32" ? ".exe" : "";
const slug = `${process.platform}-${process.arch}`;

/** A native compiler binary for this platform, if one ships with the package. */
function findNativeBinary() {
  const override = process.env.xbintsc_BINARY;
  if (override && existsSync(override)) return override;

  const require = createRequire(import.meta.url);
  for (const request of [`@xbintsc/${slug}/bin/xbintsc${exe}`, `@xbintsc/${slug}`]) {
    try {
      const resolved = require.resolve(request);
      if (existsSync(resolved)) return resolved;
    } catch {
      // Not installed for this platform; keep looking.
    }
  }

  for (const candidate of [
    join(root, "bin", `xbintsc${exe}`),
    join(root, "dist", `xbintsc${exe}`),
  ]) {
    if (existsSync(candidate)) return candidate;
  }
  return undefined;
}

const args = process.argv.slice(2);

const native = findNativeBinary();
if (native) {
  const result = spawnSync(native, args, { stdio: "inherit" });
  process.exit(result.status ?? 1);
}

const compiled = join(root, "dist", "src", "cli", "main.js");
if (existsSync(compiled)) {
  const { run } = await import(pathToFileURL(compiled).href);
  process.exit(run(args));
}

const tsx = join(root, "node_modules", ".bin", process.platform === "win32" ? "tsx.cmd" : "tsx");
const entry = join(root, "src", "cli", "main.ts");
const result = spawnSync(tsx, [entry, ...args], { stdio: "inherit" });
process.exit(result.status ?? 1);
