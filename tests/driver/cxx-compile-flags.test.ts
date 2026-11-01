import { describe, expect, it } from "vitest";
import { cxxCompileFlags } from "../../scripts/gui-build-options.js";

describe("archive C++ compile flags", () => {
  it("passes the resolved macOS SDK to clang++ as a sysroot", () => {
    const sdk = "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";
    const flags = cxxCompileFlags(sdk, ["-I", "/runtime"]);

    expect(flags).toEqual([
      "-std=c++17",
      "-O2",
      "-Wall",
      "-Wextra",
      "-ffunction-sections",
      "-fdata-sections",
      "-isysroot",
      sdk,
      "-I",
      "/runtime",
    ]);
  });

  it("adds no sysroot off macOS", () => {
    expect(cxxCompileFlags(undefined, [])).not.toContain("-isysroot");
  });
});
