import { describe, expect, it } from "vitest";
import {
  LLVM_MINGW_VERSION,
  LLVM_VERSION,
  toolchainDownload,
  toolchainSupportLibraries,
} from "../../src/driver/toolchain-download.js";

describe("toolchainDownload", () => {
  it("maps Linux x64 to the pinned LLVM release", () => {
    const target = toolchainDownload("linux", "x64");
    expect(target?.url).toContain(`llvmorg-${LLVM_VERSION}`);
    expect(target?.url).toContain("x86_64-linux-gnu");
    expect(target?.kind).toBe("tar.xz");
    expect(target?.stripComponents).toBe(1);
  });

  it("maps Linux arm64 to the pinned LLVM release", () => {
    const target = toolchainDownload("linux", "arm64");
    expect(target?.url).toContain("aarch64-linux-gnu");
    expect(target?.kind).toBe("tar.xz");
  });

  it("maps Windows x64 to the pinned llvm-mingw release", () => {
    const target = toolchainDownload("win32", "x64");
    expect(target?.url).toContain(`llvm-mingw-${LLVM_MINGW_VERSION}`);
    expect(target?.url).toContain("ucrt-x86_64");
    expect(target?.kind).toBe("zip");
    expect(target?.stripComponents).toBe(1);
  });

  it("does not bundle a toolchain on macOS", () => {
    expect(toolchainDownload("darwin", "arm64")).toBeUndefined();
    expect(toolchainDownload("darwin", "x64")).toBeUndefined();
  });

  it("bundles libtinfo.so.5 for Linux x64", () => {
    const libraries = toolchainSupportLibraries("linux", "x64");
    expect(libraries).toHaveLength(1);
    expect(libraries[0]?.dest).toBe("libtinfo.so.5");
    expect(libraries[0]?.urls.length).toBeGreaterThan(0);
  });

  it("requests no support libraries on other hosts", () => {
    expect(toolchainSupportLibraries("darwin", "arm64")).toEqual([]);
    expect(toolchainSupportLibraries("win32", "x64")).toEqual([]);
  });
});
