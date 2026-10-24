/**
 * The repository's cross-platform text conventions, and the helpers that make
 * them easy to follow.
 *
 * `.gitattributes` forces LF on checkout. This test catches the two ways that
 * guarantee can rot: the attribute being removed, and a working tree whose text
 * files are still CRLF (a stale checkout, or a Windows editor that rewrote
 * them). A CRLF file otherwise surfaces much later as a confusing mismatch in
 * an unrelated suite, after a Windows-only failure that cannot be reproduced on
 * the machine that added the test.
 */

import { spawnSync } from "node:child_process";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { normalizeEol, readTextFile, toPosixPath } from "./platform.js";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");

describe("platform helpers", () => {
  it("canonicalises CRLF and a lone CR to LF", () => {
    expect(normalizeEol("a\r\nb\rc\n")).toBe("a\nb\nc\n");
    expect(normalizeEol("already\nlf\n")).toBe("already\nlf\n");
  });

  it("renders a native path in the compiler's canonical form", () => {
    expect(toPosixPath("C:\\dir\\file.ts")).toBe("C:/dir/file.ts");
    expect(toPosixPath("/already/posix.ts")).toBe("/already/posix.ts");
  });
});

describe("repository text conventions", () => {
  it("declares LF for every text file", () => {
    expect(readTextFile(join(root, ".gitattributes"))).toMatch(/^\*\s+text=auto\s+eol=lf$/m);
  });

  it("has no CRLF in the working tree", () => {
    const listed = spawnSync("git", ["ls-files", "--eol"], { cwd: root, encoding: "utf8" });
    // A source archive (no .git) has nothing to compare; Git without --eol is
    // too old to matter for this repository.
    if (listed.status !== 0) return;

    // `git ls-files --eol` reports `i/<index> w/<worktree> attr/... \t path`.
    // `w/crlf` and `w/mixed` are text files (binary files show `w/-text`), so
    // this respects Git's own text detection instead of guessing extensions.
    const offenders = listed.stdout
      .split("\n")
      .filter((line) => /^i\/\S+\s+w\/(?:crlf|mixed)\s+/.test(line))
      .map((line) => line.slice(line.indexOf("\t") + 1))
      .filter((path) => path.length > 0);

    expect(
      offenders,
      "text files must use LF; on a clean checkout run " +
        "`git add --renormalize . && git checkout -- .` " +
        "(stash or commit local edits first), or fix the listed files",
    ).toEqual([]);
  });
});
