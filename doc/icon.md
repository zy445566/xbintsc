# xbintsc application icons (compile-time icon embedding)

> Language: **English** | [简体中文](./zh-CN/icon.md)

Status: **implemented** — milestones I0–I3 below have shipped. I4 (Linux
desktop integration) and I5 (per-window icon) remain optional follow-ups.

## Goal

Give a compiled program a real OS-level **application / window icon** supplied
at *compile time* and baked into the artifact, so the binary needs no side-car
icon file at runtime:

- **Windows** — the icon shows in Explorer, the taskbar and Alt-Tab (PE
  resource).
- **macOS** — the icon shows in Finder and the Dock (`.app` bundle with an
  `.icns`).
- **Linux** — the running window/taskbar icon is set from the embedded image;
  desktop integration (`.desktop` + themed PNG) is a follow-up.

The core compiler stays platform-agnostic: the lexer/parser/binder/codegen never
learn what an icon is. Icon handling is a *driver* concern (like the per-platform
linker flags that already live there) plus an optional *runtime* concern (the
`gui` extension).

## UX

```bash
# CLI — icon is a build option
xbintsc build app.ts --icon assets/app.png -o app
xbintsc run   app.ts --icon assets/app.png

# programmatic API
build("app.ts", { icon: "assets/app.png" });
```

Icon settings — and the app metadata that comes with them (name, bundle id) —
are normally declared once in a project build config (`xbintsc.config.json`):

```json
{
  "$schema": "https://raw.githubusercontent.com/zy445566/xbintsc/main/doc/xbintsc.config.schema.json",
  "entry": "src/app.ts",
  "outDir": "build",
  "extensions": ["gui"],
  "app": {
    "name": "Demo",
    "icon": "assets/app.png",
    "bundle": true,
    "bundleId": "com.example.demo"
  }
}
```

```bash
xbintsc build                   # reads xbintsc.config.json
xbintsc build --icon other.png  # a flag overrides the config
```

```ts
// GUI apps pick the embedded icon up automatically
import { createWindow, run } from "gui";
createWindow({ title: "Demo", width: 800, height: 600 });
run(); // window/taskbar/Dock icon = the one configured at build time
```

## Layer 0 — project build config (`xbintsc.config.json`)

Introduced by this feature (there was no project-level compile config before).
A single checked-in file makes a build reproducible (`xbintsc build` with no
arguments).

### Schema

Every field is optional:

```json
{
  "entry": "src/app.ts",
  "outDir": "build",
  "output": "build/Demo",
  "optimize": "2",
  "extensions": ["gui", "node"],
  "extNative": ["native/xbintsc.manifest.json"],
  "force": false,
  "app": {
    "name": "Demo",
    "icon": "assets/app.png",
    "bundle": true,
    "bundleId": "com.example.demo"
  }
}
```

- Strict JSON (`JSON.parse`); unknown keys are ignored.
- All relative paths resolve against the **config file's directory**, not the
  cwd.
- `app.bundle` is macOS-only and controls the `.app` bundle.

### Discovery & precedence

1. `--config <path>` selects a config explicitly; `--no-config` disables it.
2. Otherwise walk up from the positional entry's directory, then the cwd, until
   `xbintsc.config.json` is found.
3. Precedence for every option: **CLI flag > config value > built-in default**.
4. `xbintsc build` with no positional entry falls back to `entry` in the config;
   an error is reported if neither is present.

Implemented in `src/driver/config.ts` (`loadProjectConfig`, `parseProjectConfig`,
`findProjectConfig`, `resolveConfigPaths`, `ProjectConfigError`) and wired into
`src/cli/main.ts`.

## Layer 1 — embed & package

### `src/driver/icon.ts`

```ts
export type IconFormat = "png" | "ico" | "icns";
export interface IconInfo {
  readonly path: string;
  readonly format: IconFormat;
  readonly bytes: Uint8Array;
  readonly width: number;  // 0 when unknown (e.g. .icns)
  readonly height: number;
}
export function readIcon(path: string): IconInfo;                  // throws IconError
export function iconSource(icon: IconInfo): string;                // generated C
export function ensureIconObject(runner, clang, cacheDir, icon): string;
export function pngToIco(png: Uint8Array, width, height): Uint8Array;
export function toIcoBytes(icon: IconInfo): Uint8Array;
```

`readIcon` sniffs the magic bytes (PNG/ICO/ICNS), rejects missing/empty/
unsupported files with an `IconError`, and reads the intrinsic size where it is
cheap (PNG IHDR, ICO directory entry).

### Embedded symbols

When an icon is configured — and for every GUI build even without one — the
driver generates a tiny C file in the cache dir (never in the user's tree),
compiles it with the existing `compileC`, and appends the object to the `link()`
input list:

```c
/* generated, cached by icon content hash */
const unsigned char      xt_app_icon_data[];   /* raw icon bytes      */
const unsigned long long xt_app_icon_size;     /* 0 == no icon        */
const char               xt_app_icon_format[]; /* "png" | "ico" | "icns" */
const unsigned int       xt_app_icon_width;
const unsigned int       xt_app_icon_height;
```

The GUI extension references these symbols **unconditionally**, so the driver
links an empty icon object (`EMPTY_ICON`) into GUI programs even when no icon is
configured; `xt_app_icon_size == 0` tells the runtime to skip it. This avoids
fragile weak-symbol tricks (Mach-O `weak` does not resolve to null the way ELF
does).

### CLI & API wiring

- `src/cli/main.ts`: `--icon`, `--bundle`, `--app-name`, `--app-id`, `--config`,
  `--no-config`; merged with the config (`mergeAppConfig`).
- `src/driver/compiler.ts`: `BuildOptions.icon` and `BuildOptions.app`.
  - The icon **content hash** and the packaging options are part of the
    executable `cacheKey`, so changing the icon rebuilds.
  - The icon object and (on win32) the resource object are appended to the link
    inputs.
  - The macOS `.app` bundle is produced after linking; `BuildResult.bundlePath`
    reports it (the executable path is unchanged, so `run` still works).

### Per-platform packaging

#### Windows (PE resource) — `src/driver/win-icon.ts`

1. A PNG input is wrapped in a minimal ICO container (`ICONDIR` +
   `ICONDIRENTRY` pointing at the PNG payload); Vista+ accepts PNG-in-ICO.
2. A `.rc` (`1 ICON "icon-<hash>.ico"`) is written next to it in the cache.
3. `llvm-rc /fo app.res app.rc` (preferred, next to the resolved clang or on
   `PATH`), else MinGW `windres app.rc -O coff -o app_res.o`. Override with
   `xbintsc_RC`.
4. If no resource compiler exists, the build continues with the runtime icon only
   (a cosmetic feature never hard-fails a build).

#### macOS (`MyApp.app` bundle) — `src/driver/mac-bundle.ts`

```
MyApp.app/Contents/
  Info.plist            CFBundleName/Identifier/Executable/IconFile
  MacOS/MyApp           copy of the linked executable
  Resources/AppIcon.icns
```

- `.icns` inputs are copied; `.png` inputs are converted with `sips` +
  `iconutil`. If those tools are unavailable the PNG is shipped as
  `AppIcon.png` and the runtime Dock icon still applies.
- `bundleId` supplies `CFBundleIdentifier`; the default is
  `com.xbintsc.<binary>`.
- Bundling is opt-in (`app.bundle` / `--bundle`) because it changes the output
  layout.

#### Linux

ELF has no icon convention. The only in-binary use is the runtime window icon
(via the embedded bytes). A follow-up may synthesize a `<name>.desktop` +
`hicolor` PNG and set `SDL_SetAppMetadata`/app-id hints for Wayland.

### Format policy

- **PNG is canonical** and works everywhere.
- `.ico` (Windows) and `.icns` (macOS) are accepted; `.ico` is best-effort as a
  PE resource, `.icns` is copied into the mac bundle verbatim.

## Layer 2 — runtime window/Dock icon (`gui`)

`runtime/ext_gui/gui.cpp` defines `xt_gui_apply_icon(SDL_Window *)`, called from
`xt_gui_create_window` after the window is created:

- Reads `xt_app_icon_{data,size,format}`; skips when `size == 0`.
- Decodes PNG bytes with the existing `xtgui::xt_image_decode` (stb_image).
- Builds an `SDL_Surface` (`SDL_CreateSurfaceFrom(..., SDL_PIXELFORMAT_RGBA32,
  pitch)`) and calls `SDL_SetWindowIcon`.
- Any failure is silent: an icon is cosmetic and must never stop the program.

## Caching

- The executable cache key includes the icon bytes hash, format, app name,
  bundle flag and bundle id.
- The generated C object is cached on the icon content hash.
- The Windows `.res`/COFF object is cached on the icon+compiler hash.
- The `.app` bundle is part of the cached outputs, so a stale bundle is never
  reused.

## Testing

- `tests/driver/config.test.ts` — parse/validate, discovery, path resolution.
- `tests/driver/icon.test.ts` — format sniffing, generated C, ICO conversion,
  icon object caching, `llvm-rc`/`windres` command lines, mac bundle layout and
  fallbacks, and `build()` integration (icon objects linked, missing icon ⇒
  `DiagnosticCode.IOError`).
- `tests/e2e/icon.test.ts` — compiles a real GUI program with `app.icon`,
  asserts the PNG bytes are present in the executable, runs it headless, and
  (darwin) checks the `.app` bundle + `.icns`.
- The CLI suites cover `--config`/`--no-config`/`--icon`.

## Milestones

0. **I0 — project build config** ✅ (`src/driver/config.ts`, CLI wiring, tests).
1. **I1 — embedded icon + runtime window icon** ✅ (`--icon`,
   `src/driver/icon.ts`, `xt_app_icon_*`, `SDL_SetWindowIcon`, cache key).
2. **I2 — Windows PE resource** ✅ (`src/driver/win-icon.ts`).
3. **I3 — macOS `.app` bundle** ✅ (`src/driver/mac-bundle.ts`).
4. **I4 — Linux desktop integration** (optional).
5. **I5 — per-window / per-asset icon** (optional).

## Decisions

- **OQ-1 UX** — icon lives in the project build config (`app.icon`) with a
  `--icon` override. *Done.*
- **OQ-2 macOS bundling** — explicit `--bundle` / `app.bundle`. *Done.*
- **OQ-3 formats** — PNG required; ICO/ICNS best-effort. *Done.*
- **OQ-4 non-GUI apps** — supported everywhere (it is just a resource). *Done.*
- **OQ-5 default icon** — none; opt-in. *Done.*
- **OQ-6 config format/name** — `xbintsc.config.json`, strict JSON (`$schema`
  allowed but ignored). *Done.*
- **OQ-7 config scope** — single `entry` for now. *Done.*
