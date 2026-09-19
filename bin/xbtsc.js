#!/usr/bin/env node
/**
 * xbintsc launcher.
 *
 * Prefers the compiled CLI in `dist/` (production installs run `npm run build`
 * first). In a checkout, falls back to running the TypeScript sources through
 * `tsx` so `npx xbintsc` works without a build step.
 */

import { existsSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { spawnSync } from "node:child_process";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const compiled = join(root, "dist", "src", "cli", "main.js");

if (existsSync(compiled)) {
  const { run } = await import(pathToFileURL(compiled).href);
  process.exit(run(process.argv.slice(2)));
} else {
  const tsx = join(root, "node_modules", ".bin", process.platform === "win32" ? "tsx.cmd" : "tsx");
  const entry = join(root, "src", "cli", "main.ts");
  const result = spawnSync(tsx, [entry, ...process.argv.slice(2)], { stdio: "inherit" });
  process.exit(result.status ?? 1);
}
