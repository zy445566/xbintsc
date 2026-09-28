import { afterEach, describe, expect, it, vi } from "vitest";

const mocks = vi.hoisted(() => ({ findRuntimeLibrary: vi.fn() }));

/* Keep the archive lookup deterministic so both the success and the
 * "not built yet" branches of `nativeObjects` can be exercised. */
vi.mock("../../src/driver/runtime-lib.js", () => ({
  findRuntimeLibrary: mocks.findRuntimeLibrary,
}));

import { guiExtension } from "../../src/extensions/gui/index.js";
import { loadHtmlAsset } from "../../src/extensions/gui/html.js";

afterEach(() => {
  vi.restoreAllMocks();
  mocks.findRuntimeLibrary.mockReset();
});

describe("gui extension object", () => {
  it("exposes its module, builtins and asset loader", () => {
    expect(guiExtension.name).toBe("gui");
    expect(guiExtension.description).toMatch(/GUI/);
    const modules = guiExtension.modules?.() ?? {};
    expect(modules.gui?.namespace).toBe("gui");
    expect(modules.gui?.exports?.createWindow?.symbol).toBe("xt_gui_create_window");
    expect(modules.gui?.exports?.run?.symbol).toBe("xt_gui_run");
    expect(modules.gui?.exports?.quit?.symbol).toBe("xt_gui_quit");
    expect(guiExtension.builtins?.().__registerScript?.symbol).toBe("xt_register_script");
    expect(guiExtension.assetLoaders?.()[".html"]).toBe(loadHtmlAsset);
  });

  it("resolves the native archive", () => {
    mocks.findRuntimeLibrary.mockReturnValue("/tmp/gui.a");
    expect(guiExtension.nativeObjects?.()).toEqual(["/tmp/gui.a"]);
    expect(mocks.findRuntimeLibrary).toHaveBeenCalledWith("gui");
  });

  it("fails with an actionable message when the archive is missing", () => {
    mocks.findRuntimeLibrary.mockReturnValue(undefined);
    expect(() => guiExtension.nativeObjects?.()).toThrow(/GUI native library not found/);
  });

  it("links the right frameworks on every supported platform", () => {
    const platform = (value: NodeJS.Platform): void => {
      vi.spyOn(process, "platform", "get").mockReturnValue(value);
    };

    platform("darwin");
    expect(guiExtension.linkerFlags?.()).toContain("Cocoa");
    vi.restoreAllMocks();

    platform("win32");
    expect(guiExtension.linkerFlags?.()).toContain("-ld3d12");
    vi.restoreAllMocks();

    platform("linux");
    expect(guiExtension.linkerFlags?.()).toContain("-lX11");
  });
});
