/**
 * Optional link features: runtime capabilities an extension can contribute
 * without making every program pay for them.
 *
 * A feature is *not* an extension: the extension is the compile-time unit
 * (`--ext gui`), while a feature is a heavy optional half of it (`FFmpeg`,
 * `glslang`). The driver decides which features a program may use from a static
 * scan of the bundled AST (see `src/driver/usage.ts`) and then links the
 * feature's registration object and archives only for those.
 *
 * The registration object is the "strong reference": it is a C translation unit
 * linked directly only when the feature is active. Its constructor installs the
 * feature's dispatch table into the core, and its references force the linker to
 * extract the archive members that implement the feature. This is the same
 * mechanism the former `media` extension used for `media_bridge.c`.
 */

/** Facts about a bundled program that features use to decide whether they are needed. */
export interface ProgramUsage {
  /**
   * Every string literal value in the bundle, including the HTML/CSS of
   * imported `.html` assets and the static chunks of template literals.
   */
  readonly strings: readonly string[];
  /** Identifiers and property names referenced anywhere in the bundle. */
  readonly identifiers: ReadonlySet<string>;
  /**
   * One entry per `getContext(...)` call: the context kind when the argument
   * could be resolved to a literal, or `undefined` when it is decided at run
   * time (which must be treated conservatively as "might be WebGL").
   */
  readonly getContext: readonly (string | undefined)[];
  /**
   * True when the program can build HTML/CSS from data the compiler cannot
   * see (`innerHTML = fetched`, `loadHTML(buildPage())`, …). Features must
   * assume they may be used when this is set.
   */
  readonly dynamicMarkup: boolean;
}

/**
 * One optional capability an extension can contribute.
 *
 * `used()` is only consulted when the providing extension is enabled. When it
 * returns true (or the user forces the feature) the driver compiles
 * `runtimeSources()` and links `nativeObjects()`, which makes the implementations
 * reachable. Nothing else about the feature exists at compile time.
 */
export interface LinkFeature {
  /** Stable name used by `--feature` / `--no-feature`, e.g. `media`. */
  readonly name: string;
  /** One-line description for diagnostics and `--verbose`. */
  readonly description: string;
  /**
   * C sources compiled and linked only when the feature is active. At least one
   * of them must reference the feature's implementation so the linker extracts
   * it from its archive.
   */
  runtimeSources?(): readonly string[];
  /**
   * Pre-built objects/archives linked only when the feature is active. Throwing
   * here means "the feature is unavailable on this host"; the driver turns that
   * into a warning for auto-detected features and an error for forced ones.
   */
  nativeObjects?(): readonly string[];
  /** Extra linker flags contributed only when the feature is active. */
  linkerFlags?(): readonly string[];
  /** Other link features this one needs (activated with it). */
  readonly requires?: readonly string[];
  /** Whether the program may use this feature. */
  used(usage: ProgramUsage): boolean;
}
