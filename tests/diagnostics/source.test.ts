import { describe, expect, it } from "vitest";
import { SourceFile, combineHashes, hashText } from "../../src/diagnostics/source.js";

describe("SourceFile", () => {
  it("normalizes a leading BOM and CRLF/CR line endings", () => {
    const source = new SourceFile("a.ts", "\uFEFFa\r\nb\rc\n");
    expect(source.text).toBe("a\nb\nc\n");
    expect(source.fileName).toBe("a.ts");
  });

  it("reports its length from the normalized text", () => {
    expect(new SourceFile("a.ts", "abc").length).toBe(3);
    expect(new SourceFile("a.ts", "\uFEFFabc").length).toBe(3);
  });

  it("maps offsets to 1-based line/column pairs", () => {
    const source = new SourceFile("a.ts", "a\nbb\nccc");
    expect(source.positionAt(0)).toEqual({ line: 1, column: 1 });
    expect(source.positionAt(1)).toEqual({ line: 1, column: 2 });
    expect(source.positionAt(2)).toEqual({ line: 2, column: 1 });
    expect(source.positionAt(3)).toEqual({ line: 2, column: 2 });
    expect(source.positionAt(5)).toEqual({ line: 3, column: 1 });
    expect(source.positionAt(7)).toEqual({ line: 3, column: 3 });
  });

  it("clamps out-of-range and negative offsets", () => {
    const source = new SourceFile("a.ts", "abc");
    expect(source.positionAt(-5)).toEqual({ line: 1, column: 1 });
    expect(source.positionAt(100)).toEqual({ line: 1, column: 4 });
  });

  it("slices the raw text of a range", () => {
    const source = new SourceFile("a.ts", "hello world");
    expect(source.slice({ start: 0, end: 5 })).toBe("hello");
    expect(source.slice({ start: 6, end: 11 })).toBe("world");
  });

  it("exposes a stable content hash on the instance", () => {
    expect(new SourceFile("a.ts", "same").hash).toBe(hashText("same"));
    expect(new SourceFile("a.ts", "same").hash).not.toBe(new SourceFile("a.ts", "other").hash);
  });
});

describe("hashText", () => {
  it("is deterministic and zero-padded to 16 hex chars", () => {
    const hash = hashText("hello");
    expect(hash).toBe(hashText("hello"));
    expect(hash).toMatch(/^[0-9a-f]{16}$/);
  });

  it("changes with the input and respects a seed", () => {
    expect(hashText("a")).not.toBe(hashText("b"));
    expect(hashText("a", 1n)).not.toBe(hashText("a"));
  });

  it("treats empty input as the seed", () => {
    expect(hashText("", 0n)).toBe("0000000000000000");
  });
});

describe("combineHashes", () => {
  it("combines parts without collisions between orderings", () => {
    expect(combineHashes(["a", "b"])).toBe(combineHashes(["a", "b"]));
    expect(combineHashes(["a", "b"])).not.toBe(combineHashes(["b", "a"]));
    expect(combineHashes([])).toBe(hashText(""));
  });
});
