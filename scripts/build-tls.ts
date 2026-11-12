#!/usr/bin/env node
/**
 * Build the archives behind the `tls` and `http2` link features:
 *
 *   runtime/lib/<os>-<arch>/tls.{a,lib}      mbedTLS (TLS 1.2/1.3, X.509, PSA)
 *   runtime/lib/<os>-<arch>/nghttp2.{a,lib}  nghttp2 (HTTP/2 framing + HPACK)
 *
 * Both libraries are fetched as release tarballs into `vendor/` (gitignored,
 * like SDL/FFmpeg for `gui`/`media`) and compiled with the same toolchain the
 * driver uses, so the objects match the rest of the runtime:
 *
 *   - Windows: MSVC-compatible COFF objects, merged by `llvm-ar` into `.lib`.
 *   - Linux/macOS: ELF/Mach-O objects, merged by `llvm-ar`/`ar` into `.a`.
 *
 * The C wrappers that *use* these archives (`runtime/xt_tls/tls_core.c`,
 * `runtime/xt_node/tls/...`, `runtime/ext_node/http2/...`) are compiled on
 * demand by the driver, so this script only produces the third-party half.
 *
 * Environment:
 *   xbintsc_TLS              mbedTLS release tag (default 3.6.2)
 *   xbintsc_NGHTTP2          nghttp2 release tag (default 1.64.0)
 *   xbintsc_CLANG            C compiler override
 *   xbintsc_AR               archiver override
 *
 * Run `npm run tls` after changing either pin; the e2e suites that need the
 * archives skip when they are absent.
 */

import { existsSync, mkdirSync, readFileSync, readdirSync, renameSync, rmSync, statSync, writeFileSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { basename, dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { platformSlug } from "../src/driver/paths.js";
import { applyMacosDeploymentTarget } from "../src/driver/platform-support.js";
import { resolveToolchain } from "../src/driver/toolchain-provider.js";
import { RUNTIME_CFLAGS, sysrootFlags } from "../src/driver/toolchain.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const vendorDir = join(root, "vendor");
const outDir = join(root, "runtime", "lib", platformSlug());
const objRoot = join(root, "build", "tls-obj");
const isWindows = process.platform === "win32";
const archiveSuffix = isWindows ? ".lib" : ".a";

const MBEDTLS_VERSION = process.env.xbintsc_TLS ?? "3.6.2";
const NGHTTP2_VERSION = process.env.xbintsc_NGHTTP2 ?? "1.64.0";

applyMacosDeploymentTarget();

const toolchain = resolveToolchain();
const clang = toolchain.clang;
const childEnv = Object.assign({}, process.env, toolchain.env);

function fail(message: string): never {
  console.error(`xbintsc: ${message}`);
  process.exit(1);
}

function run(command: string, args: readonly string[]): void {
  const result = spawnSync(command, [...args], { stdio: "inherit", env: childEnv });
  if (result.error) fail(`failed to run ${command}: ${result.error.message}`);
  if (result.status !== 0) fail(`${command} ${args.join(" ")} failed (${result.status})`);
}

function isRunnable(candidate: string): boolean {
  return !spawnSync(candidate, [], { encoding: "utf8" }).error;
}

function findArchiver(clangPath: string): string {
  const dir = dirname(clangPath);
  const exe = isWindows ? ".exe" : "";
  const candidates = [
    process.env.xbintsc_AR,
    dir === "." ? undefined : join(dir, `llvm-ar${exe}`),
    "llvm-ar",
    isWindows ? undefined : "ar",
  ].filter((candidate): candidate is string => Boolean(candidate));
  for (const candidate of candidates) {
    if (isRunnable(candidate)) return candidate;
  }
  return fail("no archiver found; install llvm-ar or set xbintsc_AR to it");
}

/** Download a release tarball and extract it under `vendor/<name>`. */
function ensureSource(name: string, url: string, extractedName: string, marker: string): string {
  const destination = join(vendorDir, name);
  if (existsSync(join(destination, marker))) return destination;
  const tarball = join(vendorDir, `${name}.tar.gz`);
  console.log(`xbintsc: fetching ${name} from ${url}`);
  mkdirSync(vendorDir, { recursive: true });
  run("curl", ["-sSL", "--fail", "--max-time", "300", "-o", tarball, url]);
  run("tar", ["-xzf", tarball, "-C", vendorDir]);
  const extracted = join(vendorDir, extractedName);
  if (!existsSync(extracted)) fail(`expected ${extractedName} after extracting ${name}`);
  rmSync(destination, { recursive: true, force: true });
  renameSync(extracted, destination);
  rmSync(tarball, { force: true });
  return destination;
}

interface LibraryBuild {
  readonly name: string;
  readonly sources: readonly string[];
  readonly includeDirs: readonly string[];
  readonly defines: readonly string[];
}

/** Newest modification time of the inputs, used to reuse a fresh archive. */
function newestSource(sources: readonly string[], dirs: readonly string[]): number {
  let newest = 0;
  const visit = (path: string): void => {
    const info = statSync(path);
    if (info.isDirectory()) {
      for (const entry of readdirSync(path)) visit(join(path, entry));
      return;
    }
    if (info.mtimeMs > newest) newest = info.mtimeMs;
  };
  for (const source of sources) visit(source);
  for (const dir of dirs) visit(dir);
  return newest;
}

function archiveIsFresh(library: LibraryBuild, output: string): boolean {
  if (!existsSync(output)) return false;
  try {
    return statSync(output).mtimeMs >= newestSource(library.sources, library.includeDirs);
  } catch {
    return false;
  }
}

function compileLibrary(library: LibraryBuild, output: string, ar: string): void {
  if (archiveIsFresh(library, output)) {
    console.log(`xbintsc: reusing existing ${output}`);
    return;
  }
  const objDir = join(objRoot, library.name);
  rmSync(objDir, { recursive: true, force: true });
  mkdirSync(objDir, { recursive: true });
  const objects: string[] = [];
  library.sources.forEach((source, index) => {
    const object = join(objDir, `${index}_${basename(source, ".c")}.o`);
    const args = [
      ...RUNTIME_CFLAGS,
      ...sysrootFlags(toolchain.sdkRoot),
      ...library.defines,
      ...library.includeDirs.map((dir) => `-I${dir}`),
      "-c",
      source,
      "-o",
      object,
    ];
    const result = spawnSync(clang, args, { stdio: ["ignore", "ignore", "pipe"], env: childEnv, encoding: "utf8" });
    if (result.status !== 0) {
      process.stderr.write(result.stderr ?? "");
      fail(`failed to compile ${source}`);
    }
    objects.push(object);
  });
  mkdirSync(dirname(output), { recursive: true });
  rmSync(output, { force: true });
  run(ar, ["rcs", output, ...objects]);
  console.log(`xbintsc: wrote ${output}`);
}

function cSources(dir: string): string[] {
  return readdirSync(dir)
    .filter((name) => name.endsWith(".c"))
    .sort()
    .map((name) => join(dir, name));
}

/* -- mbedTLS -------------------------------------------------------------- */

const mbedtlsDir = ensureSource(
  "mbedtls",
  `https://codeload.github.com/Mbed-TLS/mbedtls/tar.gz/refs/tags/mbedtls-${MBEDTLS_VERSION}`,
  `mbedtls-mbedtls-${MBEDTLS_VERSION}`,
  join("library", "ssl_tls.c"),
);
const mbedtlsLibrary: LibraryBuild = {
  name: "tls",
  // The backend wrapper is archived too: its bridge (`tls_bridge.c`) is what
  // the driver compiles, so an installed npm package needs no vendor headers.
  sources: [...cSources(join(mbedtlsDir, "library")), join(root, "runtime", "xt_tls", "tls_core.c")],
  includeDirs: [join(mbedtlsDir, "include"), join(mbedtlsDir, "library"), join(root, "runtime")],
  defines: [],
};

/* -- nghttp2 -------------------------------------------------------------- */

const nghttp2Dir = ensureSource(
  "nghttp2",
  `https://codeload.github.com/nghttp2/nghttp2/tar.gz/refs/tags/v${NGHTTP2_VERSION}`,
  `nghttp2-${NGHTTP2_VERSION}`,
  join("lib", "nghttp2_session.c"),
);
/**
 * nghttp2's public headers expect a generated `nghttp2ver.h`; generate it
 * exactly as its autotools build does. The number is the 24-bit
 * major/minor/patch representation (1.64.0 -> 0x014000).
 */
const versionParts = NGHTTP2_VERSION.split(".").map((part) => Number(part) || 0);
const ngHeader = join(nghttp2Dir, "lib", "includes", "nghttp2", "nghttp2ver.h");
const ngTemplate = join(nghttp2Dir, "lib", "includes", "nghttp2", "nghttp2ver.h.in");
const versionNum = `0x${versionParts
  .map((part) => part.toString(16).padStart(2, "0"))
  .join("")}`;
writeFileSync(
  ngHeader,
  readFileSync(ngTemplate, "utf8")
    .replaceAll("@PACKAGE_VERSION@", NGHTTP2_VERSION)
    .replaceAll("@PACKAGE_VERSION_NUM@", versionNum),
);

const nghttp2Library: LibraryBuild = {
  name: "nghttp2",
  // `nghttp2_alpn.c` only wraps NPN/ALPN helper callbacks for TLS backends; it
  // is part of the public library surface, so keep it.
  sources: [
    ...cSources(join(nghttp2Dir, "lib")),
    join(root, "runtime", "ext_node", "http2", "http2_backend.c"),
  ],
  includeDirs: [
    join(nghttp2Dir, "lib", "includes"),
    join(nghttp2Dir, "lib"),
    join(root, "runtime"),
  ],
  // nghttp2's public header uses POSIX `ssize_t`; MSVC/UCRT has none.
  defines: [
    "-DNGHTTP2_STATICLIB",
    "-include",
    join(root, "runtime", "xt_http2", "nghttp2_compat.h"),
  ],
};

console.log(`xbintsc: TLS toolchain ${clang} (${toolchain.source})`);
const ar = findArchiver(clang);
compileLibrary(mbedtlsLibrary, join(outDir, `tls${archiveSuffix}`), ar);
compileLibrary(nghttp2Library, join(outDir, `nghttp2${archiveSuffix}`), ar);
