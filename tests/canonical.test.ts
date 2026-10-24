import { describe, expect, it } from "vitest";
import {
  ARTIFACT_LINE_ENDING,
  ARTIFACT_PATH_SEPARATOR,
  artifactPath,
} from "../src/canonical.js";

/**
 * These are the values every byte-compared compiler artifact uses. Pinning them
 * here makes an accidental change (for example `"\r\n"` for a Windows host)
 * fail in a unit test instead of as a self-hosting or cross-platform diff.
 */
describe("canonical artifact encoding", () => {
  it("uses `/` as the path separator", () => {
    expect(ARTIFACT_PATH_SEPARATOR).toBe("/");
    expect(artifactPath("C:\\dir\\file.ts")).toBe("C:/dir/file.ts");
    expect(artifactPath("/already/posix.ts")).toBe("/already/posix.ts");
  });

  it("uses LF between lines", () => {
    expect(ARTIFACT_LINE_ENDING).toBe("\n");
    expect(["a", "b", ""].join(ARTIFACT_LINE_ENDING)).toBe("a\nb\n");
  });
});
