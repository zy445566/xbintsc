import { describe, expect, it } from "vitest";
import {
  PLATFORM_SLUGS,
  platformPackageFileName,
  platformPackageManifest,
  platformPackageName,
  requiredArchives,
} from "../../scripts/package-platform.js";

describe("npm platform package", () => {
  it("names the package after the host slug", () => {
    expect(platformPackageName("linux-x64")).toBe("@xbintsc/linux-x64");
    expect(platformPackageName("win32-arm64")).toBe("@xbintsc/win32-arm64");
  });

  it("constrains the manifest to the host os and cpu", () => {
    const manifest = platformPackageManifest({
      version: "1.2.3",
      slug: "darwin-arm64",
      repository: { type: "git", url: "git+https://example.invalid/repo.git" },
    });
    expect(manifest).toMatchObject({
      name: "@xbintsc/darwin-arm64",
      version: "1.2.3",
      os: ["darwin"],
      cpu: ["arm64"],
      files: ["runtime"],
      publishConfig: { access: "public" },
      repository: { type: "git", url: "git+https://example.invalid/repo.git" },
    });
  });

  it("uses COFF libraries on Windows and archives elsewhere", () => {
    expect(requiredArchives("win32-x64")).toEqual(["core.lib", "ext_node.lib"]);
    expect(requiredArchives("linux-arm64")).toEqual(["core.a", "ext_node.a"]);
    expect(requiredArchives("darwin-x64")).toEqual(["core.a", "ext_node.a"]);
  });

  it("names the tarball the way npm pack does", () => {
    expect(platformPackageFileName("linux-x64", "1.2.3")).toBe("xbintsc-linux-x64-1.2.3.tgz");
    expect(platformPackageFileName("win32-arm64", "0.5.35")).toBe("xbintsc-win32-arm64-0.5.35.tgz");
  });

  it("covers exactly the hosts a release publishes", () => {
    expect(PLATFORM_SLUGS).toEqual([
      "linux-x64",
      "linux-arm64",
      "darwin-x64",
      "darwin-arm64",
      "win32-x64",
      "win32-arm64",
    ]);
    for (const slug of PLATFORM_SLUGS) expect(requiredArchives(slug)).toHaveLength(2);
  });

  it("rejects a slug without an architecture", () => {
    expect(() => platformPackageManifest({ version: "1.0.0", slug: "linux" })).toThrow(/invalid platform slug/);
  });
});
