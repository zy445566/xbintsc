import { mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { gzipSync } from "node:zlib";
import { describe, expect, it } from "vitest";
import {
  tarballEntries,
  verifyPlatformEntries,
  verifyPlatformPackage,
} from "../scripts/verify-platform-package.js";

const BLOCK = 512;

/** Build one ustar header block for a regular file. */
function tarHeader(path: string, size: number): Buffer {
  const header = Buffer.alloc(BLOCK);
  header.write(path, 0, 100, "utf8");
  header.write("0000644\0", 100, 8, "utf8");
  header.write("0000000\0", 108, 8, "utf8");
  header.write("0000000\0", 116, 8, "utf8");
  header.write(`${size.toString(8).padStart(11, "0")}\0`, 124, 12, "utf8");
  header.write("00000000000\0", 136, 12, "utf8");
  header.write("        ", 148, 8, "utf8"); // checksum field, summed as spaces
  header.write("0", 156, 1, "utf8");
  header.write("ustar\0", 257, 6, "utf8");
  header.write("00", 263, 2, "utf8");
  let sum = 0;
  for (const byte of header) sum += byte;
  header.write(`${sum.toString(8).padStart(6, "0")}\0 `, 148, 8, "utf8");
  return header;
}

/** Build a gzip-compressed ustar archive holding `files` as regular files. */
function makeTarball(files: Record<string, string>): Buffer {
  const blocks: Buffer[] = [];
  for (const [path, content] of Object.entries(files)) {
    const body = Buffer.from(content, "utf8");
    blocks.push(tarHeader(path, body.length), body, Buffer.alloc((BLOCK - (body.length % BLOCK)) % BLOCK));
  }
  blocks.push(Buffer.alloc(BLOCK * 2)); // end-of-archive marker
  return gzipSync(Buffer.concat(blocks));
}

/** A platform tarball shaped like the one `scripts/package-platform.ts` packs. */
function platformTarball(
  slug: string,
  version: string,
  archives: readonly string[] = ["core", "ext_node"],
): Buffer {
  const suffix = slug.startsWith("win32-") ? ".lib" : ".a";
  const files: Record<string, string> = {
    "package/package.json": `${JSON.stringify({ name: `@xbintsc/${slug}`, version })}\n`,
    "package/README.md": `# @xbintsc/${slug}\n`,
    "package/LICENSE": "MIT\n",
  };
  for (const archive of archives) {
    files[`package/runtime/lib/${slug}/${archive}${suffix}`] = `${archive} payload`;
  }
  return makeTarball(files);
}

function entriesOf(tarball: Buffer) {
  return tarballEntries(tarball, "fixture.tgz");
}

describe("platform package tarball", () => {
  it("lists nested members with their contents", () => {
    const entries = entriesOf(platformTarball("linux-x64", "1.2.3"));
    expect(entries.map((entry) => entry.path)).toEqual([
      "package/package.json",
      "package/README.md",
      "package/LICENSE",
      "package/runtime/lib/linux-x64/core.a",
      "package/runtime/lib/linux-x64/ext_node.a",
    ]);
    expect(entries[3]!.data.toString("utf8")).toBe("core payload");
  });

  it("accepts the tarball a host packs for itself", () => {
    for (const slug of ["linux-x64", "linux-arm64", "darwin-x64", "darwin-arm64", "win32-x64"]) {
      expect(() => verifyPlatformEntries(entriesOf(platformTarball(slug, "1.2.3")), slug, "1.2.3")).not.toThrow();
    }
  });

  it("rejects a tarball without the core archive", () => {
    const tarball = platformTarball("linux-x64", "1.2.3", ["ext_node"]);
    expect(() => verifyPlatformEntries(entriesOf(tarball), "linux-x64", "1.2.3")).toThrow(
      /has no core runtime archive \(expected package\/runtime\/lib\/linux-x64\/core\.a\)/,
    );
  });

  it("rejects a tarball without the node extension archive", () => {
    const tarball = platformTarball("darwin-arm64", "1.2.3", ["core"]);
    expect(() => verifyPlatformEntries(entriesOf(tarball), "darwin-arm64", "1.2.3")).toThrow(
      /has no ext_node runtime archive/,
    );
  });

  it("expects COFF libraries on Windows", () => {
    expect(() => verifyPlatformEntries(entriesOf(platformTarball("win32-x64", "1.2.3")), "win32-x64", "1.2.3")).not.toThrow();
    // A package carrying `.a` archives cannot be the win32-x64 one npm links.
    const elfArchives = makeTarball({
      "package/package.json": `${JSON.stringify({ name: "@xbintsc/win32-x64", version: "1.2.3" })}\n`,
      "package/runtime/lib/win32-x64/core.a": "core payload",
      "package/runtime/lib/win32-x64/ext_node.a": "node payload",
    });
    expect(() => verifyPlatformEntries(entriesOf(elfArchives), "win32-x64", "1.2.3")).toThrow(
      /has no core runtime archive/,
    );
  });

  it("rejects a tarball built for another version", () => {
    const tarball = platformTarball("linux-x64", "1.2.2");
    expect(() => verifyPlatformEntries(entriesOf(tarball), "linux-x64", "1.2.3")).toThrow(
      /contains @xbintsc\/linux-x64@1\.2\.2, expected @xbintsc\/linux-x64@1\.2\.3/,
    );
  });

  it("rejects a tarball built for another host", () => {
    const tarball = platformTarball("linux-arm64", "1.2.3");
    expect(() => verifyPlatformEntries(entriesOf(tarball), "linux-x64", "1.2.3")).toThrow(
      /contains @xbintsc\/linux-arm64@1\.2\.3/,
    );
  });

  it("rejects a tarball without a manifest", () => {
    const tarball = makeTarball({ "package/README.md": "no manifest" });
    expect(() => verifyPlatformEntries(entriesOf(tarball), "linux-x64", "1.2.3")).toThrow(
      /has no package\/package\.json/,
    );
  });

  it("rejects a payload that is not a gzipped tar archive", () => {
    expect(() => tarballEntries(Buffer.from("not a tarball"), "fixture.tgz")).toThrow(
      /fixture\.tgz is not a gzip-compressed tar archive/,
    );
  });

  it("verifies a tarball read from disk", () => {
    const dir = mkdtempSync(join(tmpdir(), "xbintsc-platform-"));
    const file = join(dir, "xbintsc-linux-x64-1.2.3.tgz");
    writeFileSync(file, platformTarball("linux-x64", "1.2.3"));
    const entries = verifyPlatformPackage(file, "linux-x64", "1.2.3");
    expect(entries.length).toBe(5);
  });
});
