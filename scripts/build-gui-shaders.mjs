#!/usr/bin/env node
/**
 * Compile the GUI engine's shaders ahead of time and embed them in
 * `runtime/ext_gui/renderer_shaders_data.h`.
 *
 * SDL_GPU cannot compile GLSL or HLSL at runtime: each backend consumes its own
 * binary format. The engine therefore ships:
 *
 *   - SPIR-V, for Vulkan, compiled from `runtime/ext_gui/spirv/*.{vert,frag}`
 *     with `glslc` (from the Vulkan SDK); and
 *   - DXIL, for Direct3D 12, compiled from `runtime/ext_gui/shaders.hlsl` with
 *     `dxc` (inside the Windows SDK).
 *
 * Metal needs no blob: SDL compiles the MSL literals in `renderer_shaders.h` at
 * runtime.
 *
 * The generated header is committed so a machine without either compiler (Linux,
 * macOS CI) still builds the engine; only the corresponding backend is
 * unavailable there.
 *
 * Environment:
 *   xbintsc_GLSLC  explicit `glslc` path (default: newest `C:\VulkanSDK\*`)
 *   xbintsc_DXC    explicit `dxc` path (default: newest Windows SDK)
 *
 * Usage:
 *   node scripts/build-gui-shaders.mjs          # regenerate the header
 *   node scripts/build-gui-shaders.mjs --check  # fail if it is out of date
 */

import { existsSync, mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const engineDir = join(root, "runtime", "ext_gui");
const glslDir = join(engineDir, "spirv");
const hlsl = join(engineDir, "shaders.hlsl");
const header = join(engineDir, "renderer_shaders_data.h");
const scratch = join(root, "build", "gui-shaders");

/** SPIR-V programs: GLSL source file and the symbol to emit. */
const SPIRV_PROGRAMS = [
  { source: "fill.vert", symbol: "kFillSpirvVs" },
  { source: "fill.frag", symbol: "kFillSpirvFs" },
  { source: "quad.vert", symbol: "kTextSpirvVs" },
  { source: "text.frag", symbol: "kTextSpirvFs" },
  { source: "image.frag", symbol: "kImageSpirvFs" },
];

/** DXIL programs: HLSL entry point, stage and the symbol to emit. */
const DXIL_PROGRAMS = [
  { target: "vs_6_0", entry: "vs_main", symbol: "kFillDxilVs" },
  { target: "ps_6_0", entry: "fs_main", symbol: "kFillDxilFs" },
  { target: "vs_6_0", entry: "vs_text", symbol: "kTextDxilVs" },
  { target: "ps_6_0", entry: "fs_text", symbol: "kTextDxilFs" },
  { target: "ps_6_0", entry: "fs_image", symbol: "kImageDxilFs" },
];

function fail(message) {
  console.error(`xbintsc: ${message}`);
  process.exit(1);
}

/** Newest version-suffixed directory under `base`, or undefined. */
function newestVersionDir(base, isVersion) {
  if (!existsSync(base)) return undefined;
  const versions = readdirSync(base)
    .filter(isVersion)
    .sort((a, b) => {
      const pa = a.split(".").map(Number);
      const pb = b.split(".").map(Number);
      for (let i = 0; i < Math.max(pa.length, pb.length); i += 1) {
        const diff = (pa[i] ?? 0) - (pb[i] ?? 0);
        if (diff !== 0) return diff;
      }
      return 0;
    })
    .reverse();
  return { base, versions };
}

/** Locate `glslc`: the explicit override, then the newest installed Vulkan SDK. */
function findGlslc() {
  if (process.env.xbintsc_GLSLC) return process.env.xbintsc_GLSLC;
  const found = newestVersionDir("C:\\VulkanSDK", (name) => /^\d+\.\d+\.\d+\.\d+$/.test(name));
  if (!found) return undefined;
  for (const version of found.versions) {
    const bin = join(found.base, version, "Bin");
    for (const name of ["glslc.exe", "glslc"]) {
      if (existsSync(join(bin, name))) return join(bin, name);
    }
  }
  return undefined;
}

/** Locate `dxc`: the explicit override, then the newest Windows SDK. */
function findDxc() {
  if (process.env.xbintsc_DXC) return process.env.xbintsc_DXC;
  const found = newestVersionDir("C:\\Program Files (x86)\\Windows Kits\\10\\bin", (name) =>
    /^\d+\.\d+\.\d+\.\d+$/.test(name),
  );
  if (!found) return undefined;
  for (const version of found.versions) {
    for (const arch of ["x64", "arm64"]) {
      const candidate = join(found.base, version, arch, "dxc.exe");
      if (existsSync(candidate)) return candidate;
    }
  }
  return undefined;
}

/** Compile `args` with `tool`, failing with its output on a non-zero exit. */
function compile(tool, args, label) {
  const result = spawnSync(tool, args, { encoding: "utf8" });
  if (result.status !== 0) {
    fail(`${label} failed:\n${result.stderr ?? ""}${result.stdout ?? ""}`);
  }
}

/** Render one blob as a C++ array initializer, 16 bytes per line. */
function renderArray(symbol, bytes) {
  const lines = [];
  for (let i = 0; i < bytes.length; i += 16) {
    lines.push("  " + Array.from(bytes.subarray(i, i + 16)).join(", ") + ",");
  }
  return [
    `inline constexpr unsigned char ${symbol}[] = {`,
    ...lines,
    "};",
    `inline constexpr size_t ${symbol}Size = sizeof(${symbol});`,
  ].join("\n");
}

mkdirSync(scratch, { recursive: true });

const blobs = [];

const glslc = findGlslc();
if (glslc === undefined) {
  fail(
    "no glslc found; install the Vulkan SDK (https://vulkan.lunarg.com) or set " +
      `xbintsc_GLSLC.\nThe committed ${header} was not regenerated.`,
  );
}
for (const program of SPIRV_PROGRAMS) {
  const output = join(scratch, `${program.symbol}.spv`);
  compile(
    glslc,
    ["-c", join(glslDir, program.source), "-o", output],
    `glslc ${program.source}`,
  );
  blobs.push({ symbol: program.symbol, bytes: readFileSync(output) });
}

const dxc = findDxc();
if (dxc === undefined) {
  fail(
    "no dxc found; install the Windows SDK or set xbintsc_DXC.\n" +
      `The committed ${header} was not regenerated.`,
  );
}
for (const program of DXIL_PROGRAMS) {
  const output = join(scratch, `${program.symbol}.dxil`);
  compile(
    dxc,
    ["-T", program.target, "-E", program.entry, "-Fo", output, hlsl],
    `dxc ${program.target} ${program.entry}`,
  );
  blobs.push({ symbol: program.symbol, bytes: readFileSync(output) });
}

rmSync(scratch, { recursive: true, force: true });

const generated = [
  "/*",
  " * Generated by `scripts/build-gui-shaders.mjs` — do not edit.",
  " *",
  " * SPIR-V blobs (Vulkan) compiled from `runtime/ext_gui/spirv/` with glslc, and",
  " * DXIL blobs (Direct3D 12) compiled from `runtime/ext_gui/shaders.hlsl` with dxc.",
  " * The Metal path uses the MSL literals in `renderer_shaders.h` directly.",
  " */",
  "#ifndef XT_GUI_RENDERER_SHADERS_DATA_H",
  "#define XT_GUI_RENDERER_SHADERS_DATA_H",
  "",
  "#include <stddef.h>",
  "",
  ...blobs.map((blob) => renderArray(blob.symbol, blob.bytes)),
  "",
  "#endif /* XT_GUI_RENDERER_SHADERS_DATA_H */",
  "",
].join("\n");

if (process.argv.includes("--check")) {
  const current = existsSync(header) ? readFileSync(header, "utf8") : "";
  if (current !== generated) {
    fail(`${header} is out of date; run \`node scripts/build-gui-shaders.mjs\``);
  }
  console.log(`xbintsc: ${header} is up to date`);
} else {
  writeFileSync(header, generated);
  const total = blobs.reduce((sum, blob) => sum + blob.bytes.length, 0);
  console.log(`xbintsc: wrote ${header} (${blobs.length} blobs, ${total} bytes)`);
}
