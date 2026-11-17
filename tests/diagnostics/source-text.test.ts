import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, describe, expect, it } from "vitest";
import {
  decodeSourceBytes,
  readSourceText,
  sourceTextBytes,
  sourceTextFromBytes,
} from "../../src/diagnostics/source-text.js";

const directories: string[] = [];

afterEach(() => {
  while (directories.length > 0) rmSync(directories.pop()!, { recursive: true, force: true });
});

/** The code units of `text`, as a compact array for assertions. */
function units(text: string): number[] {
  const result: number[] = [];
  for (let index = 0; index < text.length; index++) result.push(text.charCodeAt(index));
  return result;
}

describe("source text bytes", () => {
  it("encodes host text that is entirely in the Latin-1 range", () => {
    // The bug this guards: `é` is one UTF-16 code unit (0xE9), but its UTF-8
    // bytes are C3 A9. A raw code unit must never reach codegen.
    expect(units(sourceTextBytes("café"))).toEqual([0x63, 0x61, 0x66, 0xc3, 0xa9]);
    expect(units(sourceTextBytes("Grüße"))).toEqual([
      0x47, 0x72, 0xc3, 0xbc, 0xc3, 0x9f, 0x65,
    ]);
  });

  it("keeps file bytes as they are instead of encoding them again", () => {
    const fileBytes = Buffer.from("café 😀", "utf8");
    expect(sourceTextFromBytes(fileBytes)).toBe(sourceTextBytes("café 😀"));
    expect(units(sourceTextFromBytes(fileBytes))).toEqual(units(sourceTextBytes("café 😀")));
  });

  it("drops a UTF-8 BOM from a file read", () => {
    const bytes = Buffer.concat([Buffer.from([0xef, 0xbb, 0xbf]), Buffer.from("const x = 1;", "utf8")]);
    expect(sourceTextFromBytes(bytes)).toBe("const x = 1;");
    expect(sourceTextBytes("\uFEFFconst x = 1;")).toBe("const x = 1;");
  });

  it("decodes internal bytes back to host text", () => {
    expect(decodeSourceBytes(sourceTextBytes("中文 😀"))).toBe("中文 😀");
    // An incomplete trailing sequence becomes U+FFFD rather than throwing.
    expect(decodeSourceBytes("\xe4\xb8")).toBe("\uFFFD\uFFFD");
  });

  it("reads a source file through the byte path", () => {
    const directory = mkdtempSync(join(tmpdir(), "xbintsc-source-text-"));
    directories.push(directory);
    const path = join(directory, "only-accent.ts");
    writeFileSync(path, 'console.log("café");\n');
    expect(units(readSourceText(path))).toEqual(units(sourceTextBytes('console.log("café");\n')));
  });
});
