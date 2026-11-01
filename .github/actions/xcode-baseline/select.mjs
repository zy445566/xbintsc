/**
 * Pick the oldest installed Xcode that can build xbintsc.
 *
 * The release artifacts are built with the oldest toolchain available so a
 * newer Xcode on a user's machine can link them. The runner image installs
 * several Xcode versions, but the *default* one is the newest; this script
 * probes them all, drops anything older than clang 16 (xbintsc's opaque-pointer
 * floor) and exports the oldest survivor to the rest of the job.
 *
 * A candidate is only usable when its clang can actually compile a C file
 * against its own SDK. Older Xcode bundles on the runner do not always ship a
 * complete SDK, and an Apple clang invoked by absolute path (rather than the
 * `/usr/bin/clang` shim) does not infer `-isysroot` by itself: without an
 * explicit `SDKROOT` the runtime build fails with `'setjmp.h' file not found`.
 * Both are checked here, and a candidate that cannot compile is skipped.
 *
 * It runs from `.github/actions/xcode-baseline/action.yml`, after
 * `actions/setup-node` (it needs `node` on PATH).
 */

import { spawnSync } from "node:child_process";
import { appendFileSync, existsSync, readdirSync } from "node:fs";
import { join } from "node:path";

/** Xcode 16 ships Apple clang 16, the oldest compiler that accepts our IR. */
const MINIMUM_CLANG_MAJOR = 16;

/** A one-line C program that needs a real macOS SDK (`setjmp.h`). */
const SDK_PROBE = "#include <setjmp.h>\nint main(void) { return 0; }\n";

/** The macOS SDK bundled with an Xcode installation. */
function macosSdk(app) {
  return join(app, "Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk");
}

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
    const sdk = macosSdk(app);
    if (!existsSync(clang) || !existsSync(sdk)) continue;
    const result = spawnSync(clang, ["--version"], { encoding: "utf8" });
    if (result.status !== 0) continue;
    const match = /clang version (\d+)\.(\d+)\.(\d+)/.exec(`${result.stdout}\n${result.stderr}`);
    if (!match) continue;
    const version = [Number(match[1]), Number(match[2]), Number(match[3])];
    if (version[0] < MINIMUM_CLANG_MAJOR) continue;
    /* Prove the toolchain can see its SDK; a partial Xcode install fails here. */
    const probe = spawnSync(clang, ["-x", "c", "-fsyntax-only", "-"], {
      encoding: "utf8",
      input: SDK_PROBE,
      env: { ...process.env, DEVELOPER_DIR: app, SDKROOT: sdk },
    });
    if (probe.status !== 0) {
      console.log(`xbintsc: skipping ${app}: clang cannot compile against ${sdk}`);
      continue;
    }
    found.push({ app, clang, sdk, version });
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
  /* Do not fail the job: the image's default Xcode still builds xbintsc, and
   * the release still targets the pinned deployment target. The oldest-Xcode
   * baseline is an extra compatibility margin, not a correctness requirement. */
  console.log(
    `::warning::no installed Xcode provides clang ${MINIMUM_CLANG_MAJOR}+ with a usable macOS SDK; ` +
      "using the image's default toolchain",
  );
  process.exit(0);
}

const { app, clang, sdk, version } = available[0];
console.log(`xbintsc: oldest Xcode ${app} (clang ${version.join(".")})`);

/* In CI the exports apply to every later step; a local invocation just prints. */
if (process.env.GITHUB_ENV) {
  appendFileSync(process.env.GITHUB_ENV, `DEVELOPER_DIR=${app}\n`);
  appendFileSync(process.env.GITHUB_ENV, `SDKROOT=${sdk}\n`);
  appendFileSync(process.env.GITHUB_ENV, `xbintsc_CLANG=${clang}\n`);
}
