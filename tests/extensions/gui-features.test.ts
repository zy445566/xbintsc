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
import { guiLinkFeatures, hasGpuUsage, hasMediaUsage } from "../../src/extensions/gui/features.js";
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

describe("gui link features", () => {
  it("declares media and gpu as optional halves of gui", () => {
    expect(guiExtension.linkFeatures?.()).toBe(guiLinkFeatures);
    expect(guiLinkFeatures.map((feature) => feature.name)).toEqual(["media", "gpu"]);
    expect(media.description).toMatch(/FFmpeg/);
    expect(gpu.description).toMatch(/glslang|GLSL/);
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

  it("turns on for mix-blend-mode in HTML/CSS or the style property", () => {
    expect(hasGpuUsage(usage({ strings: ["style=\"mix-blend-mode: multiply\""] }))).toBe(true);
    expect(hasGpuUsage(usage({ identifiers: new Set(["mixBlendMode"]) }))).toBe(true);
  });

  it("stays off for a 2D canvas and plain DOM", () => {
    expect(hasGpuUsage(usage({ getContext: ["2d"] }))).toBe(false);
    expect(hasGpuUsage(usage({ strings: ["<canvas id=c></canvas>"] }))).toBe(false);
  });

  it("turns on for dynamic markup", () => {
    expect(hasGpuUsage(usage({ dynamicMarkup: true }))).toBe(true);
  });
});
