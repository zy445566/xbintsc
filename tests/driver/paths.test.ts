import { describe, expect, it } from "vitest";
import {
  platformSlug,
  releaseArchiveBase,
  releaseArchiveExtension,
} from "../../src/driver/paths.js";

describe("platformSlug", () => {
  it("joins the platform and architecture", () => {
    expect(platformSlug()).toBe(`${process.platform}-${process.arch}`);
  });
});

describe("releaseArchiveBase", () => {
  it("names the archive after the platform and architecture", () => {
    expect(releaseArchiveBase("linux", "x64")).toBe("xbintsc-linux-x64");
    expect(releaseArchiveBase("darwin", "arm64")).toBe("xbintsc-darwin-arm64");
    expect(releaseArchiveBase("win32", "x64")).toBe("xbintsc-win32-x64");
  });

  it("defaults to the host platform", () => {
    expect(releaseArchiveBase()).toBe(`xbintsc-${process.platform}-${process.arch}`);
  });
});

describe("releaseArchiveExtension", () => {
  it("uses .tar.zst when zstd is available", () => {
    expect(releaseArchiveExtension(true)).toBe(".tar.zst");
  });

  it("falls back to .tar.gz", () => {
    expect(releaseArchiveExtension(false)).toBe(".tar.gz");
  });
});
