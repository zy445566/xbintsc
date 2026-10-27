#!/usr/bin/env node
/**
 * Assemble the optional `@xbintsc/<os>-<arch>` npm package for the current host.
 *
 * The package carries the prebuilt archives in the same layout the compiler
 * already searches:
 *
 *   package/runtime/lib/<os>-<arch>/{core,ext_node}.{a,lib}
 *   package/runtime/lib/<os>-<arch>/gui.{a,lib}       (when the release ships it)
 *   package/runtime/lib/<os>-<arch>/media.{a,lib}     (when the release ships it)
 *
 * `xbintsc` lists these packages in `optionalDependencies`, so npm installs the
 * one matching the host and the driver links its archives instead of compiling
 * the C runtime. Release automation publishes the tarball produced here; see
 * `.github/workflows/release.yml`.
 *
 * Output: `dist/npm-platform/xbintsc-<os>-<arch>-<version>.tgz`.
 */

import { spawnSync } from "node:child_process";
import { cpSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { platformSlug } from "../src/driver/paths.js";

export interface PackageRepository {
  readonly type: string;
  readonly url: string;
}

export interface PlatformPackageOptions {
  readonly version: string;
  readonly slug: string;
  readonly repository?: PackageRepository;
}

export interface PlatformPackageManifest {
  readonly name: string;
  readonly version: string;
  readonly description: string;
  readonly license: string;
  readonly repository?: PackageRepository;
  readonly os: readonly string[];
  readonly cpu: readonly string[];
  readonly files: readonly string[];
  readonly publishConfig: { readonly access: "public" };
}

/** npm name of the platform package, e.g. `@xbintsc/linux-x64`. */
export function platformPackageName(slug: string): string {
  return `@xbintsc/${slug}`;
}

/** Split `linux-x64` into its npm `os` and `cpu` constraints. */
function splitSlug(slug: string): { os: string; arch: string } {
  const dash = slug.lastIndexOf("-");
  if (dash <= 0 || dash === slug.length - 1) {
    throw new Error(`invalid platform slug "${slug}" (expected <os>-<arch>)`);
  }
  return { os: slug.slice(0, dash), arch: slug.slice(dash + 1) };
}

/** Archives every platform package must contain. */
export function requiredArchives(slug: string): string[] {
  const suffix = slug.startsWith("win32-") ? ".lib" : ".a";
  return [`core${suffix}`, `ext_node${suffix}`];
}

/** The package manifest written into the staging tree and packed by npm. */
export function platformPackageManifest(options: PlatformPackageOptions): PlatformPackageManifest {
  const { os, arch } = splitSlug(options.slug);
  const name = platformPackageName(options.slug);
  return {
    name,
    version: options.version,
    description: `Prebuilt xbintsc C runtime archives for ${options.slug}. Installed automatically by xbintsc.`,
    license: "MIT",
    ...(options.repository ? { repository: options.repository } : {}),
    os: [os],
    cpu: [arch],
    files: ["runtime"],
    publishConfig: { access: "public" },
  };
}

/** Run npm through the same CLI that invoked this script, when available. */
function runNpm(args: readonly string[], cwd: string): { stdout: string } {
  const execPath = process.env.npm_execpath;
  const useCli = execPath !== undefined && /\.(cjs|mjs|js)$/.test(execPath);
  const command = useCli ? process.execPath : process.platform === "win32" ? "npm.cmd" : "npm";
  const commandArgs = useCli ? [execPath, ...args] : [...args];
  const result = spawnSync(command, commandArgs, {
    cwd,
    encoding: "utf8",
    shell: !useCli && process.platform === "win32",
  });
  if (result.error) throw result.error;
  if (result.status !== 0) {
    throw new Error(`npm ${args.join(" ")} failed (${result.status}):\n${result.stderr}`);
  }
  return { stdout: result.stdout };
}

/**
 * Copy the host's archives into a staging tree, write the manifest and run
 * `npm pack`. Returns the absolute path of the produced `.tgz`.
 */
export function packagePlatform(root: string, slug: string = platformSlug()): string {
  const pkg = JSON.parse(readFileSync(join(root, "package.json"), "utf8")) as {
    version: string;
    repository?: PackageRepository;
  };
  const libDir = join(root, "runtime", "lib", slug);
  const missing = requiredArchives(slug).filter((name) => !existsSync(join(libDir, name)));
  if (missing.length > 0) {
    throw new Error(`missing ${missing.join(", ")} in ${libDir}; run \`npm run runtime\` first`);
  }

  const stageRoot = join(root, "build", "npm-platform-stage");
  const stage = join(stageRoot, "package");
  rmSync(stageRoot, { recursive: true, force: true });
  mkdirSync(stage, { recursive: true });
  cpSync(libDir, join(stage, "runtime", "lib", slug), { recursive: true });

  const manifest = platformPackageManifest({
    version: pkg.version,
    slug,
    ...(pkg.repository ? { repository: pkg.repository } : {}),
  });
  writeFileSync(join(stage, "package.json"), `${JSON.stringify(manifest, null, 2)}\n`);
  writeFileSync(
    join(stage, "README.md"),
    `# ${manifest.name}\n\n` +
      `Prebuilt xbintsc runtime archives for \`${slug}\`.\n\n` +
      `This package is installed automatically as an optional dependency of\n` +
      `[\`xbintsc\`](https://www.npmjs.com/package/xbintsc) and is not meant to be\n` +
      `used on its own. The compiler links \`runtime/lib/${slug}/\` from this\n` +
      `package instead of compiling its C runtime on the first build.\n`,
  );
  const license = join(root, "LICENSE");
  if (existsSync(license)) cpSync(license, join(stage, "LICENSE"));

  const outDir = join(root, "dist", "npm-platform");
  rmSync(outDir, { recursive: true, force: true });
  mkdirSync(outDir, { recursive: true });
  const packed = JSON.parse(runNpm(["pack", "--json", "--pack-destination", outDir], stage).stdout) as Array<{
    filename?: string;
  }>;
  const filename = packed[0]?.filename;
  if (!filename) throw new Error("npm pack did not report a tarball name");
  return join(outDir, filename);
}

const invokedDirectly =
  process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href;

if (invokedDirectly) {
  try {
    const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
    console.log(`xbintsc: wrote ${packagePlatform(root)}`);
  } catch (error) {
    console.error(`xbintsc: ${error instanceof Error ? error.message : String(error)}`);
    process.exit(1);
  }
}
