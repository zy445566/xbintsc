import { describe, expect, it } from "vitest";
import {
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

  it("rejects a slug without an architecture", () => {
    expect(() => platformPackageManifest({ version: "1.0.0", slug: "linux" })).toThrow(/invalid platform slug/);
  });
});
