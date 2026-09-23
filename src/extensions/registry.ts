/**
 * Pluggable compile modules ("extensions").
 *
 * An extension teaches xbintsc about a host platform: additional C runtime
 * sources to link, libraries to pass to the linker, global functions that
 * resolve to runtime symbols, and modules that can be pulled in with `import`.
 * Node's `fs`, Bun's `Bun.file`, ... all fit this shape, so the core compiler
 * never has to grow platform specific branches.
 *
 * The default registry ships an empty baseline; callers register the
 * extensions they need before invoking the driver.
 */

export interface BuiltinFunction {
  /** Exported C symbol, called as `xt_value symbol(int32_t argc, xt_value *argv)`. */
  readonly symbol: string;
  readonly returnVoid?: boolean;
}

/**
 * One binding a Node-style module exports. It is either a direct runtime
 * function with the uniform `(argc, argv)` ABI (`symbol`), or a named method
 * dispatched through a namespace table (`namespace` + `method`), e.g.
 * `path.join` -> `xt_path_static("join", ...)`.
 */
export interface ModuleExport {
  readonly symbol?: string;
  readonly returnVoid?: boolean;
  readonly namespace?: string;
  readonly method?: string;
  /**
   * Runtime getter used when the binding is read as a *value* rather than
   * called (e.g. `import { isMainThread } from "worker_threads"`). The symbol
   * has the uniform `(i32, i64*)` ABI and is invoked with no arguments.
   */
  readonly valueSymbol?: string;
  /**
   * Marks a runtime symbol as a constructor callable with `new` (it has the
   * `(i32, i64*)` signature). Lets `import { EventEmitter } from "events"` be
   * used as `new EventEmitter()`.
   */
  readonly isConstructor?: boolean;
}

export type ModuleExports = Readonly<Record<string, ModuleExport>>;

/** A module an extension makes importable (e.g. `import { x } from "fs"`). */
export interface ExtensionModule {
  /** Named exports for `import { x } from "..."`. */
  readonly exports?: ModuleExports;
  /** Namespace name for `import * as ns` / `import ns from`, e.g. `path`. */
  readonly namespace?: string;
}

export interface Extension {
  readonly name: string;
  /** Description shown by `xbintsc ext list`. */
  readonly description?: string;
  /** C/asm sources compiled and linked alongside the generated module. */
  runtimeSources?(): readonly string[];
  /**
   * Pre-built object files or static archives linked alongside the generated
   * module. This is the hook for native extensions written in C++ or Rust:
   * compile them to `extern "C"` objects (or a static library) that use the
   * `xt_value` ABI, then hand the artifacts to the driver through an
   * `Extension` such as the one built by `src/extensions/native.ts`.
   *
   * The paths must already exist; the driver passes them straight to the
   * linker and never recompiles them.
   */
  nativeObjects?(): readonly string[];
  /** Extra linker flags (e.g. `["-lm"]`, `["-framework", "CoreFoundation"]`). */
  linkerFlags?(): readonly string[];
  /** Global identifiers that resolve to runtime symbols when called. */
  builtins?(): Readonly<Record<string, BuiltinFunction>>;
  /** Modules importable as `import ... from "<specifier>"`. */
  modules?(): Readonly<Record<string, ExtensionModule>>;
}

export class ExtensionRegistry {
  private readonly extensions = new Map<string, Extension>();
  /**
   * Module specifier -> extension name for extensions that are known to the
   * caller but not registered. Lets the compiler turn a missing `import`
   * (`import { createServer } from "node:http"`) into an actionable hint
   * (`pass --ext node`) instead of a confusing downstream error.
   */
  private readonly hints = new Map<string, string>();

  register(extension: Extension): this {
    if (this.extensions.has(extension.name)) {
      throw new Error(`Extension '${extension.name}' is already registered`);
    }
    this.extensions.set(extension.name, extension);
    return this;
  }

  /**
   * Record every module an extension would provide if it were registered. The
   * extension itself is *not* enabled: this only feeds diagnostics, so a user
   * who forgot `--ext node` is told which flag to pass.
   */
  hintExtension(extension: Extension): this {
    for (const specifier of Object.keys(extension.modules?.() ?? {})) {
      this.hints.set(specifier, extension.name);
    }
    return this;
  }

  /** Module specifier -> extension name hints for unregistered extensions. */
  moduleHints(): Readonly<Record<string, string>> {
    const hints: Record<string, string> = {};
    for (const [specifier, name] of this.hints) {
      if (!this.extensions.has(name)) hints[specifier] = name;
    }
    return hints;
  }

  unregister(name: string): boolean {
    return this.extensions.delete(name);
  }

  get(name: string): Extension | undefined {
    return this.extensions.get(name);
  }

  has(name: string): boolean {
    return this.extensions.has(name);
  }

  all(): readonly Extension[] {
    return [...this.extensions.values()];
  }

  /** Flatten every registered builtin into one lookup table. */
  builtins(): Readonly<Record<string, BuiltinFunction>> {
    const merged: Record<string, BuiltinFunction> = {};
    for (const extension of this.extensions.values()) {
      const builtins = extension.builtins?.();
      if (builtins) Object.assign(merged, builtins);
    }
    return merged;
  }

  /** Flatten every registered extension's importable modules. */
  modules(): Readonly<Record<string, ExtensionModule>> {
    const merged: Record<string, ExtensionModule> = {};
    for (const extension of this.extensions.values()) {
      const modules = extension.modules?.();
      if (!modules) continue;
      for (const [specifier, module] of Object.entries(modules)) {
        const existing = merged[specifier];
        merged[specifier] = {
          namespace: module.namespace ?? existing?.namespace,
          exports: { ...existing?.exports, ...module.exports },
        };
      }
    }
    return merged;
  }

  runtimeSources(): readonly string[] {
    const sources: string[] = [];
    for (const extension of this.extensions.values()) {
      const extra = extension.runtimeSources?.();
      if (extra) sources.push(...extra);
    }
    return sources;
  }

  /** Flatten every registered extension's pre-built native objects. */
  nativeObjects(): readonly string[] {
    const objects: string[] = [];
    for (const extension of this.extensions.values()) {
      const extra = extension.nativeObjects?.();
      if (extra) objects.push(...extra);
    }
    return objects;
  }

  linkerFlags(): readonly string[] {
    const flags: string[] = [];
    for (const extension of this.extensions.values()) {
      const extra = extension.linkerFlags?.();
      if (extra) flags.push(...extra);
    }
    return flags;
  }
}

/**
 * A built-in extension that exposes the runtime's own console/IO helpers. It is
 * always present so `console.log` works even without a platform extension.
 */
export const coreExtension: Extension = {
  name: "core",
  description: "Core runtime helpers (console, print)",
  builtins: () => ({
    print: { symbol: "xt_println" },
  }),
};

export function createDefaultRegistry(): ExtensionRegistry {
  return new ExtensionRegistry().register(coreExtension);
}
