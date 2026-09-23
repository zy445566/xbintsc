/**
 * Native (C++ / Rust) extensions.
 *
 * xbintsc can consume code that was compiled outside the TypeScript pipeline,
 * as long as it exposes `extern "C"` entry points with the runtime calling
 * convention:
 *
 *     xt_value my_fn(int32_t argc, xt_value *argv);
 *
 * A C++ or Rust project is built into an object file or static archive, and a
 * small JSON manifest describes how those symbols map onto global functions
 * (`builtins`) and importable modules (`modules`). The driver links the
 * artifacts verbatim and the code generator wires the bindings exactly like it
 * does for the built-in `node` extension — the core compiler never needs to
 * know the extension was written in C++ or Rust.
 *
 * ```jsonc
 * {
 *   "name": "mathx",
 *   "description": "C++ math helpers",
 *   "objects": ["build/libmathx.a"],
 *   "linkerFlags": ["-lm"],
 *   "linkerFlagsByPlatform": { "linux": ["-lstdc++"], "darwin": ["-lc++"] },
 *   "builtins": { "fastAdd": { "symbol": "mathx_add" } },
 *   "modules": {
 *     "mathx": {
 *       "exports": {
 *         "add": { "symbol": "mathx_add" },
 *         "reverse": { "symbol": "mathx_reverse" }
 *       }
 *     }
 *   }
 * }
 * ```
 *
 * `objects` paths are resolved relative to the manifest file, so a manifest can
 * live next to the project it describes.
 */

import { existsSync, readFileSync } from "node:fs";
import { dirname, isAbsolute, resolve } from "node:path";
import type { Extension, ExtensionModule, ModuleExport, ModuleExports } from "./registry.js";

/** A global function an extension provides (callable without an import). */
export interface NativeBuiltin {
  /** Exported C symbol, called as `xt_value symbol(int32_t argc, xt_value *argv)`. */
  readonly symbol: string;
  readonly returnVoid?: boolean;
}

/** One binding inside a `modules` entry; mirrors the core `ModuleExport`. */
export type NativeModuleExport = ModuleExport;

/** An importable module supplied by the native library. */
export interface NativeModule {
  /** Named exports for `import { x } from "..."`. */
  readonly exports?: Readonly<Record<string, NativeModuleExport>>;
  /** Namespace name for `import * as ns` / `import ns from`. */
  readonly namespace?: string;
}

/** The on-disk schema of a native extension manifest. */
export interface NativeManifest {
  /** Unique extension name (used by `--ext`/diagnostics). */
  readonly name: string;
  readonly description?: string;
  /**
   * Object files (`.o`) or static archives (`.a`/`.lib`) to link. Resolved
   * relative to the manifest unless absolute.
   */
  readonly objects?: readonly string[];
  /** Linker flags appended verbatim, e.g. `["-lm"]`. */
  readonly linkerFlags?: readonly string[];
  /**
   * Linker flags added only on the given platform (`darwin`, `linux`, `win32`),
   * for C++ standard libraries or Rust's native dependencies that differ per OS.
   */
  readonly linkerFlagsByPlatform?: Readonly<Record<string, readonly string[]>>;
  /** Global functions the extension binds. */
  readonly builtins?: Readonly<Record<string, NativeBuiltin>>;
  /** Modules importable as `import ... from "<specifier>"`. */
  readonly modules?: Readonly<Record<string, NativeModule>>;
}

/** Raised for a malformed manifest or a missing build artifact. */
export class NativeExtensionError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "NativeExtensionError";
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function stringArray(value: unknown, field: string): readonly string[] {
  if (value === undefined) return [];
  if (!Array.isArray(value) || value.some((item) => typeof item !== "string")) {
    throw new NativeExtensionError(`Native manifest field '${field}' must be an array of strings`);
  }
  return value as readonly string[];
}

function parseBuiltins(value: unknown): Readonly<Record<string, NativeBuiltin>> | undefined {
  if (value === undefined) return undefined;
  if (!isRecord(value)) throw new NativeExtensionError("Native manifest field 'builtins' must be an object");
  const builtins: Record<string, NativeBuiltin> = {};
  for (const [name, entry] of Object.entries(value)) {
    if (!isRecord(entry) || typeof entry.symbol !== "string" || entry.symbol.length === 0) {
      throw new NativeExtensionError(`Native manifest builtin '${name}' needs a non-empty 'symbol'`);
    }
    builtins[name] = {
      symbol: entry.symbol,
      ...(entry.returnVoid === true ? { returnVoid: true } : {}),
    };
  }
  return builtins;
}

function parseExports(value: unknown, moduleName: string): ModuleExports | undefined {
  if (value === undefined) return undefined;
  if (!isRecord(value)) {
    throw new NativeExtensionError(`Native manifest module '${moduleName}'.exports must be an object`);
  }
  const exports: Record<string, ModuleExport> = {};
  for (const [name, entry] of Object.entries(value)) {
    if (!isRecord(entry)) {
      throw new NativeExtensionError(`Native manifest export '${moduleName}.${name}' must be an object`);
    }
    const symbol = entry.symbol;
    const namespace = entry.namespace;
    const method = entry.method;
    if (symbol !== undefined && typeof symbol !== "string") {
      throw new NativeExtensionError(`Native manifest export '${moduleName}.${name}'.symbol must be a string`);
    }
    if (namespace !== undefined && typeof namespace !== "string") {
      throw new NativeExtensionError(`Native manifest export '${moduleName}.${name}'.namespace must be a string`);
    }
    if (method !== undefined && typeof method !== "string") {
      throw new NativeExtensionError(`Native manifest export '${moduleName}.${name}'.method must be a string`);
    }
    if (symbol === undefined && namespace === undefined) {
      throw new NativeExtensionError(
        `Native manifest export '${moduleName}.${name}' needs a 'symbol' or a 'namespace'`,
      );
    }
    exports[name] = {
      ...(symbol !== undefined ? { symbol } : {}),
      ...(namespace !== undefined ? { namespace } : {}),
      ...(method !== undefined ? { method } : {}),
      ...(entry.returnVoid === true ? { returnVoid: true } : {}),
      ...(entry.isConstructor === true ? { isConstructor: true } : {}),
    };
  }
  return exports;
}

function parseModules(value: unknown): Readonly<Record<string, ExtensionModule>> | undefined {
  if (value === undefined) return undefined;
  if (!isRecord(value)) throw new NativeExtensionError("Native manifest field 'modules' must be an object");
  const modules: Record<string, ExtensionModule> = {};
  for (const [specifier, entry] of Object.entries(value)) {
    if (!isRecord(entry)) {
      throw new NativeExtensionError(`Native manifest module '${specifier}' must be an object`);
    }
    if (entry.namespace !== undefined && typeof entry.namespace !== "string") {
      throw new NativeExtensionError(`Native manifest module '${specifier}'.namespace must be a string`);
    }
    modules[specifier] = {
      ...(entry.namespace !== undefined ? { namespace: entry.namespace } : {}),
      ...(entry.exports !== undefined ? { exports: parseExports(entry.exports, specifier) } : {}),
    };
  }
  return modules;
}

/** Validate parsed JSON and normalise it into a {@link NativeManifest}. */
export function parseNativeManifest(value: unknown, source = "<manifest>"): NativeManifest {
  if (!isRecord(value)) throw new NativeExtensionError(`Native manifest ${source} must contain a JSON object`);
  if (typeof value.name !== "string" || value.name.trim().length === 0) {
    throw new NativeExtensionError(`Native manifest ${source} needs a non-empty 'name'`);
  }
  if (value.description !== undefined && typeof value.description !== "string") {
    throw new NativeExtensionError(`Native manifest '${value.name}'.description must be a string`);
  }

  let linkerFlagsByPlatform: Record<string, readonly string[]> | undefined;
  if (value.linkerFlagsByPlatform !== undefined) {
    if (!isRecord(value.linkerFlagsByPlatform)) {
      throw new NativeExtensionError("Native manifest field 'linkerFlagsByPlatform' must be an object");
    }
    linkerFlagsByPlatform = {};
    for (const [platform, flags] of Object.entries(value.linkerFlagsByPlatform)) {
      linkerFlagsByPlatform[platform] = stringArray(flags, `linkerFlagsByPlatform.${platform}`);
    }
  }

  const builtins = parseBuiltins(value.builtins);
  const modules = parseModules(value.modules);

  return {
    name: value.name,
    ...(value.description !== undefined ? { description: value.description } : {}),
    objects: stringArray(value.objects, "objects"),
    linkerFlags: stringArray(value.linkerFlags, "linkerFlags"),
    ...(linkerFlagsByPlatform ? { linkerFlagsByPlatform } : {}),
    ...(builtins ? { builtins } : {}),
    ...(modules ? { modules } : {}),
  };
}

/** Read and validate a manifest from disk. */
export function loadNativeManifest(manifestPath: string): NativeManifest {
  const absolute = resolve(manifestPath);
  let text: string;
  try {
    text = readFileSync(absolute, "utf8");
  } catch (error) {
    throw new NativeExtensionError(`Unable to read native extension manifest '${absolute}': ${String(error)}`);
  }
  let parsed: unknown;
  try {
    parsed = JSON.parse(text);
  } catch (error) {
    throw new NativeExtensionError(`Native extension manifest '${absolute}' is not valid JSON: ${String(error)}`);
  }
  return parseNativeManifest(parsed, absolute);
}

function resolveArtifact(baseDir: string, path: string): string {
  return isAbsolute(path) ? path : resolve(baseDir, path);
}

/**
 * Turn a manifest into an {@link Extension}: resolve and verify every native
 * object, then expose the manifest's builtins/modules to the code generator.
 *
 * The manifest is read eagerly so a typo surfaces at registration time rather
 * than at link time.
 */
export function nativeExtensionFromManifest(manifestPath: string): Extension {
  const absolute = resolve(manifestPath);
  const manifest = loadNativeManifest(absolute);
  const baseDir = dirname(absolute);

  const objects = (manifest.objects ?? []).map((object) => resolveArtifact(baseDir, object));
  for (const object of objects) {
    if (!existsSync(object)) {
      throw new NativeExtensionError(
        `Native extension '${manifest.name}' references missing object '${object}'.\n` +
          `Build the C++/Rust library first (see the project next to ${absolute}).`,
      );
    }
  }

  const linkerFlags = () => [
    ...(manifest.linkerFlags ?? []),
    ...(manifest.linkerFlagsByPlatform?.[process.platform] ?? []),
  ];

  return {
    name: manifest.name,
    description: manifest.description ?? `Native extension from ${absolute}`,
    nativeObjects: () => objects,
    linkerFlags,
    ...(manifest.builtins ? { builtins: () => manifest.builtins! } : {}),
    ...(manifest.modules ? { modules: () => manifest.modules! } : {}),
  };
}
