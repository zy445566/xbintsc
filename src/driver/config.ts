/**
 * Project build configuration (`xbintsc.config.json`).
 *
 * A single checked-in file makes a build reproducible and shareable:
 * `xbintsc build` with no arguments reads the config, so options like the entry
 * file, output directory, extensions and the application icon are not repeated
 * on every command line. CLI flags always override the config.
 *
 * The schema is intentionally small and strict JSON (no comments) so it needs
 * no parser beyond `JSON.parse`. Relative paths resolve against the config
 * file's directory, not the current working directory.
 */

import { existsSync, readFileSync } from "node:fs";
import { dirname, isAbsolute, join, resolve } from "node:path";

/** Well-known file name searched for by {@link findProjectConfig}. */
export const CONFIG_FILE_NAME = "xbintsc.config.json";

/** Application-level metadata, shared by the icon and (future) packaging. */
export interface AppConfig {
  readonly name?: string;
  readonly icon?: string;
  /** macOS only: produce a `<name>.app` bundle. */
  readonly bundle?: boolean;
  readonly bundleId?: string;
}

/** The on-disk schema of `xbintsc.config.json`. Every field is optional. */
export interface ProjectConfig {
  readonly entry?: string;
  readonly outDir?: string;
  readonly output?: string;
  readonly optimize?: "0" | "1" | "2" | "3";
  readonly extensions?: readonly string[];
  readonly extNative?: readonly string[];
  readonly force?: boolean;
  readonly app?: AppConfig;
}

export class ProjectConfigError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "ProjectConfigError";
  }
}

export interface LoadedProjectConfig {
  readonly config: ProjectConfig;
  readonly path: string;
  /** Directory the config lives in; the base for every relative path. */
  readonly directory: string;
}

function expectObject(value: unknown, what: string): Record<string, unknown> {
  if (typeof value !== "object" || value === null || Array.isArray(value)) {
    throw new ProjectConfigError(`${what} must be a JSON object`);
  }
  return value as Record<string, unknown>;
}

function readString(root: Record<string, unknown>, key: string, where: string): string | undefined {
  const value = root[key];
  if (value === undefined) return undefined;
  if (typeof value !== "string") throw new ProjectConfigError(`${where}: '${key}' must be a string`);
  return value;
}

function readBoolean(root: Record<string, unknown>, key: string, where: string): boolean | undefined {
  const value = root[key];
  if (value === undefined) return undefined;
  if (typeof value !== "boolean") throw new ProjectConfigError(`${where}: '${key}' must be a boolean`);
  return value;
}

function readStringArray(root: Record<string, unknown>, key: string, where: string): string[] | undefined {
  const value = root[key];
  if (value === undefined) return undefined;
  if (!Array.isArray(value) || value.some((entry) => typeof entry !== "string")) {
    throw new ProjectConfigError(`${where}: '${key}' must be an array of strings`);
  }
  return value as string[];
}

/** Parse and validate config text. Throws {@link ProjectConfigError}. */
export function parseProjectConfig(text: string, where: string = CONFIG_FILE_NAME): ProjectConfig {
  let raw: unknown;
  try {
    raw = JSON.parse(text);
  } catch (error) {
    throw new ProjectConfigError(`${where}: invalid JSON (${(error as Error).message})`);
  }
  const root = expectObject(raw, where);

  const optimize = readString(root, "optimize", where);
  if (optimize !== undefined && !["0", "1", "2", "3"].includes(optimize)) {
    throw new ProjectConfigError(`${where}: 'optimize' must be one of "0", "1", "2", "3"`);
  }

  let app: AppConfig | undefined;
  if (root.app !== undefined) {
    const appRoot = expectObject(root.app, `${where}: 'app'`);
    app = {
      name: readString(appRoot, "name", `${where}: 'app'`),
      icon: readString(appRoot, "icon", `${where}: 'app'`),
      bundle: readBoolean(appRoot, "bundle", `${where}: 'app'`),
      bundleId: readString(appRoot, "bundleId", `${where}: 'app'`),
    };
    for (const key of Object.keys(app) as (keyof AppConfig)[]) {
      if (app[key] === undefined) delete app[key];
    }
  }

  const config: ProjectConfig = {
    entry: readString(root, "entry", where),
    outDir: readString(root, "outDir", where),
    output: readString(root, "output", where),
    optimize: optimize as ProjectConfig["optimize"],
    extensions: readStringArray(root, "extensions", where),
    extNative: readStringArray(root, "extNative", where),
    force: readBoolean(root, "force", where),
    app,
  };
  for (const key of Object.keys(config) as (keyof ProjectConfig)[]) {
    if (config[key] === undefined) delete config[key];
  }
  return config;
}

/** Read and parse a config file. Throws {@link ProjectConfigError}. */
export function loadProjectConfig(path: string): LoadedProjectConfig {
  const absolute = resolve(path);
  if (!existsSync(absolute)) {
    throw new ProjectConfigError(`Project config not found: ${absolute}`);
  }
  const config = parseProjectConfig(readFileSync(absolute, "utf8"), absolute);
  return { config, path: absolute, directory: dirname(absolute) };
}

/** Walk up from `startDir` looking for {@link CONFIG_FILE_NAME}. */
export function findProjectConfig(startDir: string): string | undefined {
  let dir = resolve(startDir);
  for (;;) {
    const candidate = join(dir, CONFIG_FILE_NAME);
    if (existsSync(candidate)) return candidate;
    const parent = dirname(dir);
    if (parent === dir) return undefined;
    dir = parent;
  }
}

function resolvePath(base: string, value: string): string {
  return isAbsolute(value) ? value : resolve(base, value);
}

/**
 * Resolve every path in a loaded config against the config file's directory, so
 * downstream code can treat them as absolute regardless of the cwd.
 */
export function resolveConfigPaths(loaded: LoadedProjectConfig): ProjectConfig {
  const { config, directory } = loaded;
  const app: AppConfig | undefined = config.app
    ? { ...config.app, ...(config.app.icon ? { icon: resolvePath(directory, config.app.icon) } : {}) }
    : undefined;
  return {
    ...config,
    ...(config.entry ? { entry: resolvePath(directory, config.entry) } : {}),
    ...(config.outDir ? { outDir: resolvePath(directory, config.outDir) } : {}),
    ...(config.output ? { output: resolvePath(directory, config.output) } : {}),
    ...(config.extNative ? { extNative: config.extNative.map((path) => resolvePath(directory, path)) } : {}),
    ...(app ? { app } : {}),
  };
}
