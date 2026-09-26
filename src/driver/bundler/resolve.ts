/**
 * Module resolution for the bundler: relative paths, directory `index`
 * entries, package `exports` maps and `node_modules` lookup.
 */

import { existsSync, readFileSync, statSync } from "node:fs";
import { dirname, isAbsolute, join, resolve } from "node:path";
import type { DependencyResolution, PackageJson } from "./types.js";

/**
 * Source extensions probed for an extension-less specifier. TypeScript suffixes
 * are tried before their JavaScript counterparts so a project that ships both
 * `foo.ts` and a compiled `foo.js` prefers the source it is compiled from.
 */
const RESOLVE_SUFFIXES = ["", ".ts", ".tsx", ".mts", ".cts", ".js", ".jsx", ".mjs", ".cjs"];
/** File extensions a directory `index` entry may use. */
const INDEX_SUFFIXES = [".ts", ".tsx", ".mts", ".cts", ".js", ".jsx", ".mjs", ".cjs"];
/** Conditions consulted, in order, when reading a package `exports` map. */
const EXPORT_CONDITIONS = ["import", "module", "default", "node", "require"];

/** A specifier is a file path (`./x`, `../x`, `/x`) rather than a package. */
function isRelativeSpecifier(specifier: string): boolean {
  return specifier.startsWith(".") || isAbsolute(specifier);
}

/**
 * Resolve a path on disk, tolerating TypeScript's `.js` import convention
 * (`import "./foo.js"` pointing at `foo.ts`) and Node's directory/`index`
 * conventions.
 */
function resolvePath(base: string): string | undefined {
  // A JavaScript specifier may refer to a TypeScript source that will emit it
  // (`import "./foo.js"` pointing at `foo.ts`). Probe the matching TypeScript
  // extensions after the literal path: `.js`/`.jsx` -> `.ts`/`.tsx`,
  // `.mjs` -> `.mts`, `.cjs` -> `.cts`.
  const candidates = [base];
  const jsExtension = /\.((?:m|c)?jsx?)$/.exec(base);
  if (jsExtension) {
    const stem = base.slice(0, jsExtension.index);
    switch (jsExtension[1]) {
      case "jsx":
        candidates.push(`${stem}.tsx`, `${stem}.ts`);
        break;
      case "mjs":
        candidates.push(`${stem}.mts`);
        break;
      case "cjs":
        candidates.push(`${stem}.cts`);
        break;
      default:
        candidates.push(`${stem}.ts`, `${stem}.tsx`);
        break;
    }
  }
  for (const candidate of candidates) {
    if (existsSync(candidate) && statSync(candidate).isFile()) return candidate;
  }
  for (const suffix of RESOLVE_SUFFIXES) {
    const candidate = base + suffix;
    if (existsSync(candidate) && statSync(candidate).isFile()) return candidate;
  }
  if (existsSync(base) && statSync(base).isDirectory()) return resolveDirectory(base);
  return undefined;
}

/** Resolve a directory to its package entry or an `index` file. */
function resolveDirectory(directory: string): string | undefined {
  const pkg = readPackageJson(directory);
  if (pkg && typeof pkg.main === "string") {
    const main = resolvePath(resolve(directory, pkg.main));
    if (main) return main;
  }
  for (const suffix of INDEX_SUFFIXES) {
    const candidate = join(directory, `index${suffix}`);
    if (existsSync(candidate) && statSync(candidate).isFile()) return candidate;
  }
  return undefined;
}

function resolveModule(fromDir: string, specifier: string): string | undefined {
  return resolvePath(resolve(fromDir, specifier));
}

function readPackageJson(directory: string): PackageJson | undefined {
  const file = join(directory, "package.json");
  if (!existsSync(file) || !statSync(file).isFile()) return undefined;
  try {
    const parsed = JSON.parse(readFileSync(file, "utf8")) as unknown;
    return parsed && typeof parsed === "object" ? (parsed as PackageJson) : undefined;
  } catch {
    return undefined;
  }
}

/** Split `pkg/sub/path` or `@scope/pkg/sub/path` into package name and subpath. */
function splitPackageSpecifier(specifier: string): { packageName: string; subpath: string } {
  const parts = specifier.split("/");
  if (specifier.startsWith("@")) {
    return { packageName: parts.slice(0, 2).join("/"), subpath: parts.slice(2).join("/") };
  }
  return { packageName: parts[0] ?? specifier, subpath: parts.slice(1).join("/") };
}

/**
 * Resolve a bare specifier against the `node_modules` directories above
 * `fromDir`, following a package's `exports` map and falling back to
 * `module`/`main`/`index`. Packages that only ship CommonJS resolve here too;
 * their `require` calls are then rejected during code generation.
 */
function resolveNodePackage(fromDir: string, specifier: string): string | undefined {
  const { packageName, subpath } = splitPackageSpecifier(specifier);
  if (!packageName || packageName.startsWith(".")) return undefined;
  let directory = fromDir;
  for (;;) {
    const packageDir = join(directory, "node_modules", packageName);
    if (existsSync(packageDir) && statSync(packageDir).isDirectory()) {
      const resolved = subpath
        ? resolvePackageSubpath(packageDir, subpath)
        : resolvePackageEntry(packageDir);
      if (resolved) return resolved;
    }
    const parent = dirname(directory);
    if (parent === directory) return undefined;
    directory = parent;
  }
}

function resolvePackageEntry(packageDir: string): string | undefined {
  const pkg = readPackageJson(packageDir);
  if (pkg) {
    const exported = resolveExportTarget(subpathValue(pkg.exports, "."));
    if (exported) {
      const resolved = resolvePath(resolve(packageDir, exported));
      if (resolved) return resolved;
    }
    if (typeof pkg.module === "string") {
      const resolved = resolvePath(resolve(packageDir, pkg.module));
      if (resolved) return resolved;
    }
    if (typeof pkg.main === "string") {
      const resolved = resolvePath(resolve(packageDir, pkg.main));
      if (resolved) return resolved;
    }
  }
  return resolveDirectory(packageDir);
}

function resolvePackageSubpath(packageDir: string, subpath: string): string | undefined {
  const pkg = readPackageJson(packageDir);
  const exported = resolveExportTarget(subpathValue(pkg?.exports, `./${subpath}`));
  if (exported) {
    const resolved = resolvePath(resolve(packageDir, exported));
    if (resolved) return resolved;
  }
  return resolvePath(join(packageDir, subpath));
}

/** Pick the export target for a subpath (`"."` or `"./sub"`) from an exports map. */
function subpathValue(exportsField: unknown, subpath: string): unknown {
  if (exportsField === undefined) return undefined;
  if (typeof exportsField === "string" || Array.isArray(exportsField)) {
    return subpath === "." ? exportsField : undefined;
  }
  if (!exportsField || typeof exportsField !== "object") return undefined;
  const record = exportsField as Record<string, unknown>;
  const keys = Object.keys(record);
  // A map without `.`-prefixed keys is a condition map for the root export.
  if (!keys.some((key) => key.startsWith("."))) {
    return subpath === "." ? exportsField : undefined;
  }
  if (subpath in record) return record[subpath];
  // Wildcard patterns such as `"./*": "./dist/*.js"`.
  for (const key of keys) {
    const star = key.indexOf("*");
    if (star === -1) continue;
    const prefix = key.slice(0, star);
    const suffix = key.slice(star + 1);
    if (subpath.startsWith(prefix) && subpath.endsWith(suffix)) {
      const matched = subpath.slice(prefix.length, subpath.length - suffix.length);
      return substituteStar(record[key], matched);
    }
  }
  return undefined;
}

function substituteStar(target: unknown, matched: string): unknown {
  if (typeof target === "string") return target.split("*").join(matched);
  if (Array.isArray(target)) return target.map((entry) => substituteStar(entry, matched));
  if (target && typeof target === "object") {
    const record: Record<string, unknown> = {};
    for (const [key, value] of Object.entries(target)) record[key] = substituteStar(value, matched);
    return record;
  }
  return target;
}

/** Pick a runtime string from a (possibly conditional) exports target. */
function resolveExportTarget(target: unknown): string | undefined {
  if (typeof target === "string") return target;
  if (Array.isArray(target)) {
    for (const entry of target) {
      const resolved = resolveExportTarget(entry);
      if (resolved) return resolved;
    }
    return undefined;
  }
  if (target && typeof target === "object") {
    const record = target as Record<string, unknown>;
    for (const condition of EXPORT_CONDITIONS) {
      if (condition in record) {
        const resolved = resolveExportTarget(record[condition]);
        if (resolved) return resolved;
      }
    }
  }
  return undefined;
}

/**
 * Classify an import specifier. Known platform modules (extension modules and
 * `node:` builtins) are left for code generation; relative paths and
 * `node_modules` packages are resolved to a source file to bundle; a relative
 * path that does not exist is an error.
 */
export function classifyDependency(
  fromDir: string,
  specifier: string,
  externalSpecifiers: ReadonlySet<string>,
): DependencyResolution {
  if (specifier.startsWith("node:") || externalSpecifiers.has(specifier)) {
    return { kind: "external" };
  }
  const resolved = isRelativeSpecifier(specifier)
    ? resolveModule(fromDir, specifier)
    : resolveNodePackage(fromDir, specifier);
  if (resolved) return { kind: "file", path: resolved };
  // A bare specifier that is not a file may still be a platform module the
  // generator knows about (e.g. when the caller passes no extension registry).
  return isRelativeSpecifier(specifier) ? { kind: "missing" } : { kind: "external" };
}
