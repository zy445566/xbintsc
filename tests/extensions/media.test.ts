import { afterEach, describe, expect, it, vi } from "vitest";

const mocks = vi.hoisted(() => ({ findRuntimeLibrary: vi.fn() }));

/* Keep the archive lookup deterministic so both the success and the
 * "not built yet" branches of `nativeObjects` can be exercised. */
vi.mock("../../src/driver/runtime-lib.js", () => ({
  findRuntimeLibrary: mocks.findRuntimeLibrary,
}));

import { bundledExtensions } from "../../src/extensions/catalog.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { guiExtension } from "../../src/extensions/gui/index.js";
import { mediaExtension } from "../../src/extensions/media/index.js";

afterEach(() => {
  vi.restoreAllMocks();
  mocks.findRuntimeLibrary.mockReset();
});

/** Pretend to be a POSIX host so the Windows-only guard does not fire. */
function posixHost(): void {
  vi.spyOn(process, "platform", "get").mockReturnValue("darwin");
}

describe("media extension object", () => {
  it("is a separate extension that depends on gui", () => {
    expect(mediaExtension.name).toBe("media");
    expect(mediaExtension.description).toMatch(/FFmpeg/);
    expect(mediaExtension.requires).toEqual(["gui"]);
    // Media is advertised as a bundled extension, but is not enabled by
    // enabling `gui`: that separation is the point of the split.
    expect(bundledExtensions().map((extension) => extension.name)).toContain("media");
  });

  it("links the bridge object and the prebuilt archive", () => {
    posixHost();
    mocks.findRuntimeLibrary.mockReturnValue("/tmp/media.a");
    const sources = mediaExtension.runtimeSources?.() ?? [];
    expect(sources).toHaveLength(1);
    expect(sources[0]).toMatch(/runtime[\\/]ext_media[\\/]media_bridge\.c$/);
    expect(mediaExtension.nativeObjects?.()).toEqual(["/tmp/media.a"]);
    expect(mocks.findRuntimeLibrary).toHaveBeenCalledWith("media");
  });

  it("fails with an actionable message when the archive is missing", () => {
    posixHost();
    mocks.findRuntimeLibrary.mockReturnValue(undefined);
    expect(() => mediaExtension.nativeObjects?.()).toThrow(/media native library not found/);
    expect(() => mediaExtension.nativeObjects?.()).toThrow(/npm run media/);
  });

  it("reports that Windows has no media archive yet", () => {
    vi.spyOn(process, "platform", "get").mockReturnValue("win32");
    expect(() => mediaExtension.nativeObjects?.()).toThrow(/not available on Windows/);
  });

  it("rejects a build that enables media without gui", () => {
    const registry = createDefaultRegistry().register(mediaExtension);
    expect(() => registry.validate()).toThrow(/requires the 'gui' extension/);
    expect(() => registry.validate()).toThrow(/--ext gui,media/);
  });

  it("accepts gui + media together", () => {
    const registry = createDefaultRegistry().register(guiExtension).register(mediaExtension);
    expect(() => registry.validate()).not.toThrow();
  });
});
