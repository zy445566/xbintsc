/**
 * Pick the oldest installed Xcode that can build xbintsc.
 *
 * The release artifacts are built with the oldest toolchain available so a
 * newer Xcode on a user's machine can link them. The runner image installs
 * several Xcode versions, but the *default* one is the newest; this script
 * probes them all, drops anything older than clang 16 (xbintsc's opaque-pointer
 * floor) and exports the oldest survivor to the rest of the job.
 *
 * It runs from `.github/actions/xcode-baseline/action.yml`, after
 * `actions/setup-node` (it needs `node` on PATH).
 */

import { spawnSync } from "node:child_process";
import { appendFileSync, existsSync, readdirSync } from "node:fs";
import { join } from "node:path";

/** Xcode 16 ships Apple clang 16, the oldest compiler that accepts our IR. */
const MINIMUM_CLANG_MAJOR = 16;

/** Every installed Xcode, including the default `/Applications/Xcode.app`. */
function installedApplications() {
  return readdirSync("/Applications")
    .filter((name) => /^Xcode.*\.app$/.test(name))
    .map((name) => join("/Applications", name));
}

function candidates() {
  const found = [];
  for (const app of installedApplications()) {
    const clang = join(app, "Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang");
    if (!existsSync(clang)) continue;
    const result = spawnSync(clang, ["--version"], { encoding: "utf8" });
    if (result.status !== 0) continue;
    const match = /clang version (\d+)\.(\d+)\.(\d+)/.exec(`${result.stdout}\n${result.stderr}`);
    if (!match) continue;
    const version = [Number(match[1]), Number(match[2]), Number(match[3])];
    if (version[0] < MINIMUM_CLANG_MAJOR) continue;
    found.push({ app, clang, version });
  }
  /* Oldest clang first; the path breaks a tie deterministically. */
  found.sort(
    (a, b) =>
      a.version[0] - b.version[0] ||
      a.version[1] - b.version[1] ||
      a.version[2] - b.version[2] ||
      a.app.localeCompare(b.app),
  );
  return found;
}

const available = candidates();
if (available.length === 0) {
  console.error(
    `xbintsc: no installed Xcode provides clang ${MINIMUM_CLANG_MAJOR}+; ` +
      "install a newer Xcode or set xbintsc_CLANG explicitly.",
  );
  process.exit(1);
}

const { app, clang, version } = available[0];
console.log(`xbintsc: oldest Xcode ${app} (clang ${version.join(".")})`);

/* In CI the exports apply to every later step; a local invocation just prints. */
if (process.env.GITHUB_ENV) {
  appendFileSync(process.env.GITHUB_ENV, `DEVELOPER_DIR=${app}\n`);
  appendFileSync(process.env.GITHUB_ENV, `xbintsc_CLANG=${clang}\n`);
}
