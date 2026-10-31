import { join } from "node:path";
import { afterEach, describe, expect, it, vi } from "vitest";

const mocks = vi.hoisted(() => ({ findRuntimeLibrary: vi.fn() }));

/* Keep the archive lookup deterministic so both the success and the
 * "not built yet" branches of `nativeObjects` can be exercised. */
vi.mock("../../src/driver/runtime-lib.js", () => ({
  findRuntimeLibrary: mocks.findRuntimeLibrary,
  runtimeLibDir: () => "/tmp/runtime/lib/darwin-x64",
}));
vi.mock("../../src/driver/paths.js", () => ({
  findRuntimeDir: () => "/tmp/runtime",
  platformSlug: () => "darwin-x64",
}));

import { guiExtension } from "../../src/extensions/gui/index.js";
import {
  guiLinkFeatures,
  hasGpuUsage,
  hasImageUsage,
  hasMediaUsage,
  hasShapingUsage,
  hasWebglUsage,
} from "../../src/extensions/gui/features.js";
import type { ProgramUsage } from "../../src/extensions/link-feature.js";

afterEach(() => {
  vi.restoreAllMocks();
  mocks.findRuntimeLibrary.mockReset();
});

/** Build a usage record with the fields a matcher reads. */
function usage(overrides: Partial<ProgramUsage> = {}): ProgramUsage {
  return {
    strings: [],
    identifiers: new Set<string>(),
    getContext: [],
    dynamicMarkup: false,
    ...overrides,
  };
}

/** The features in declaration order. */
const media = guiLinkFeatures.find((feature) => feature.name === "media")!;
const gpu = guiLinkFeatures.find((feature) => feature.name === "gpu")!;
const images = guiLinkFeatures.find((feature) => feature.name === "images")!;
const shaping = guiLinkFeatures.find((feature) => feature.name === "shaping")!;
const webgl = guiLinkFeatures.find((feature) => feature.name === "webgl")!;

describe("gui link features", () => {
  it("declares media, gpu, images, shaping and webgl as optional halves of gui", () => {
    expect(guiExtension.linkFeatures?.()).toBe(guiLinkFeatures);
    expect(guiLinkFeatures.map((feature) => feature.name)).toEqual([
      "media",
      "gpu",
      "images",
      "shaping",
      "webgl",
    ]);
    expect(media.description).toMatch(/FFmpeg/);
    expect(gpu.description).toMatch(/glslang|GLSL/);
    expect(images.description).toMatch(/stb_image|NanoSVG/);
    expect(shaping.description).toMatch(/HarfBuzz|FreeType/);
    expect(webgl.requires).toEqual(["gpu"]);
  });

  it("links the media bridge and the prebuilt archive", () => {
    mocks.findRuntimeLibrary.mockReturnValue("/tmp/media.a");
    expect(media.runtimeSources?.()).toEqual([join("/tmp/runtime", "ext_media", "media_bridge.c")]);
    expect(media.nativeObjects?.()).toEqual(["/tmp/media.a"]);
    expect(mocks.findRuntimeLibrary).toHaveBeenCalledWith("media");
  });

  it("links the gpu bridge without an archive (the code is in gui.a)", () => {
    expect(gpu.runtimeSources?.()).toEqual([join("/tmp/runtime", "ext_gui", "gpu_bridge.c")]);
    expect(gpu.nativeObjects?.()).toBeUndefined();
  });

  it("links the images bridge without an archive", () => {
    expect(images.runtimeSources?.()).toEqual([join("/tmp/runtime", "ext_gui", "image_bridge.c")]);
    expect(images.nativeObjects?.()).toBeUndefined();
  });

  it("links the shaping bridge without an archive", () => {
    expect(shaping.runtimeSources?.()).toEqual([join("/tmp/runtime", "ext_gui", "text_bridge.c")]);
    expect(shaping.nativeObjects?.()).toBeUndefined();
  });

  it("links the webgl bridge without an archive", () => {
    expect(webgl.runtimeSources?.()).toEqual([join("/tmp/runtime", "ext_gui", "webgl_bridge.c")]);
    expect(webgl.nativeObjects?.()).toBeUndefined();
  });

  it("fails with an actionable message when the media archive is missing", () => {
    mocks.findRuntimeLibrary.mockReturnValue(undefined);
    expect(() => media.nativeObjects?.()).toThrow(/media native library not found/);
    expect(() => media.nativeObjects?.()).toThrow(/npm run media/);
    expect(() => media.nativeObjects?.()).toThrow(/darwin-x64/);
  });

  it("adds FFmpeg's Win32 libraries on Windows only", () => {
    vi.spyOn(process, "platform", "get").mockReturnValue("win32");
    expect(media.linkerFlags?.()).toEqual(["-lbcrypt", "-lole32", "-luser32"]);
    vi.restoreAllMocks();
    vi.spyOn(process, "platform", "get").mockReturnValue("darwin");
    expect(media.linkerFlags?.()).toEqual(["-lm"]);
  });
});

describe("media usage detection", () => {
  it("recognises <video>/<audio> tags in HTML strings", () => {
    expect(hasMediaUsage(usage({ strings: ['<html><video src="x.mp4"></video>'] }))).toBe(true);
    expect(hasMediaUsage(usage({ strings: ["<audio controls></audio>"] }))).toBe(true);
  });

  it("recognises createElement tag names and the Audio constructor", () => {
    expect(hasMediaUsage(usage({ strings: ["video"] }))).toBe(true);
    expect(hasMediaUsage(usage({ identifiers: new Set(["Audio"]) }))).toBe(true);
    expect(hasMediaUsage(usage({ identifiers: new Set(["HTMLVideoElement"]) }))).toBe(true);
    expect(hasMediaUsage(usage({ identifiers: new Set(["decodeAudioData"]) }))).toBe(true);
  });

  it("stays off for a DOM-only page and turns on for dynamic markup", () => {
    expect(hasMediaUsage(usage({ strings: ["<div>hi</div>"], identifiers: new Set(["document"]) }))).toBe(false);
    expect(hasMediaUsage(usage({ dynamicMarkup: true }))).toBe(true);
  });
});

describe("gpu usage detection", () => {
  it("turns on for a WebGL context", () => {
    expect(hasGpuUsage(usage({ getContext: ["webgl2"] }))).toBe(true);
    expect(hasGpuUsage(usage({ getContext: ["experimental-webgl"] }))).toBe(true);
  });

  it("turns on for a context kind decided at run time", () => {
    expect(hasGpuUsage(usage({ getContext: [undefined] }))).toBe(true);
  });

  it("turns on for mix-blend-mode/backdrop-filter in HTML/CSS or the style property", () => {
    expect(hasGpuUsage(usage({ strings: ["style=\"mix-blend-mode: multiply\""] }))).toBe(true);
    expect(hasGpuUsage(usage({ identifiers: new Set(["mixBlendMode"]) }))).toBe(true);
    expect(hasGpuUsage(usage({ strings: ["backdrop-filter: blur(4px)"] }))).toBe(true);
    expect(hasGpuUsage(usage({ identifiers: new Set(["backdropFilter"]) }))).toBe(true);
  });

  it("stays off for a 2D canvas and plain DOM", () => {
    expect(hasGpuUsage(usage({ getContext: ["2d"] }))).toBe(false);
    expect(hasGpuUsage(usage({ strings: ["<canvas id=c></canvas>"] }))).toBe(false);
  });

  it("turns on for dynamic markup", () => {
    expect(hasGpuUsage(usage({ dynamicMarkup: true }))).toBe(true);
  });
});

describe("webgl usage detection", () => {
  it("turns on only for a WebGL context or a webgl string", () => {
    expect(hasWebglUsage(usage({ getContext: ["webgl2"] }))).toBe(true);
    expect(hasWebglUsage(usage({ strings: ["webgl2"] }))).toBe(true);
    expect(hasWebglUsage(usage({ getContext: [undefined] }))).toBe(true);
  });

  it("stays off for a 2D canvas, blend-only CSS and plain DOM", () => {
    expect(hasWebglUsage(usage({ getContext: ["2d"] }))).toBe(false);
    expect(hasWebglUsage(usage({ strings: ["mix-blend-mode: multiply"] }))).toBe(false);
    expect(hasWebglUsage(usage({ strings: ["<canvas id=c></canvas>"] }))).toBe(false);
  });

  it("turns on for dynamic markup", () => {
    expect(hasWebglUsage(usage({ dynamicMarkup: true }))).toBe(true);
  });
});

describe("image usage detection", () => {
  it("turns on for <img>, CSS backgrounds, SVG and drawImage", () => {
    expect(hasImageUsage(usage({ strings: ["<img src=a.png>"] }))).toBe(true);
    expect(hasImageUsage(usage({ strings: ["background-image: url(a.png)"] }))).toBe(true);
    expect(hasImageUsage(usage({ strings: ["<svg viewBox='0 0 10 10'></svg>"] }))).toBe(true);
    expect(hasImageUsage(usage({ identifiers: new Set(["drawImage"]) }))).toBe(true);
  });

  it("stays off for text-only DOM", () => {
    expect(hasImageUsage(usage({ strings: ["<div>Hello</div>"] }))).toBe(false);
    expect(hasImageUsage(usage({ getContext: ["2d"] }))).toBe(false);
  });

  it("turns on for dynamic markup", () => {
    expect(hasImageUsage(usage({ dynamicMarkup: true }))).toBe(true);
  });
});

describe("shaping usage detection", () => {
  it("turns on for non-ASCII text and escaped/encoded codepoints", () => {
    expect(hasShapingUsage(usage({ strings: ["<p>中文测试</p>"] }))).toBe(true);
    expect(hasShapingUsage(usage({ strings: ["café"] }))).toBe(true);
    expect(hasShapingUsage(usage({ strings: ["\\u4e2d\\u6587"] }))).toBe(true);
    expect(hasShapingUsage(usage({ strings: ["&#20013;&#25991;"] }))).toBe(true);
  });

  it("turns on for author @font-face rules (custom fonts may be CFF/OTF)", () => {
    expect(
      hasShapingUsage(
        usage({
          strings: ['@font-face { font-family: "X"; src: url("./x.otf"); }'],
        }),
      ),
    ).toBe(true);
  });

  it("stays off for ASCII-only text", () => {
    expect(hasShapingUsage(usage({ strings: ["<div>Hello</div>"] }))).toBe(false);
  });

  it("turns on for dynamic markup", () => {
    expect(hasShapingUsage(usage({ dynamicMarkup: true }))).toBe(true);
  });
});
