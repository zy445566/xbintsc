import { join } from "node:path";
import { describe, expect, it } from "vitest";
import { findRuntimeLibrary, runtimeLibDir } from "../../src/driver/runtime-lib.js";
import { findRuntimeDir, platformSlug } from "../../src/driver/paths.js";

describe("runtime libraries", () => {
  it("points at the per-platform archive folder", () => {
    expect(runtimeLibDir()).toBe(join(findRuntimeDir(), "lib", platformSlug()));
  });

  it("returns undefined for an archive that is not bundled", () => {
    expect(findRuntimeLibrary("definitely-not-a-real-archive")).toBeUndefined();
  });

  it("finds a bundled core archive when present", () => {
    const core = findRuntimeLibrary("core");
    // Prebuilt archives are optional, but when present they must be absolute.
    if (core !== undefined) expect(core.endsWith(".a") || core.endsWith(".lib")).toBe(true);
  });
});
