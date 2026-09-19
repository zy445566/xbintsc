/**
 * Pluggable compile modules ("extensions").
 *
 * An extension teaches xbtsc about a host platform: additional C runtime
 * sources to link, libraries to pass to the linker, and global functions that
 * resolve to runtime symbols. Node's `fs`, Bun's `Bun.file`, ... all fit this
 * shape, so the core compiler never has to grow platform specific branches.
 *
 * The default registry ships an empty baseline; callers register the
 * extensions they need before invoking the driver.
 */

export interface BuiltinFunction {
  /** Exported C symbol, called as `xt_value symbol(int32_t argc, xt_value *argv)`. */
  readonly symbol: string;
  readonly returnVoid?: boolean;
}

export interface Extension {
  readonly name: string;
  /** Description shown by `xbtsc ext list`. */
  readonly description?: string;
  /** C/asm sources compiled and linked alongside the generated module. */
  runtimeSources?(): readonly string[];
  /** Extra linker flags (e.g. `["-lm"]`, `["-framework", "CoreFoundation"]`). */
  linkerFlags?(): readonly string[];
  /** Global identifiers that resolve to runtime symbols when called. */
  builtins?(): Readonly<Record<string, BuiltinFunction>>;
}

export class ExtensionRegistry {
  private readonly extensions = new Map<string, Extension>();

  register(extension: Extension): this {
    if (this.extensions.has(extension.name)) {
      throw new Error(`Extension '${extension.name}' is already registered`);
    }
    this.extensions.set(extension.name, extension);
    return this;
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

  runtimeSources(): readonly string[] {
    const sources: string[] = [];
    for (const extension of this.extensions.values()) {
      const extra = extension.runtimeSources?.();
      if (extra) sources.push(...extra);
    }
    return sources;
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
