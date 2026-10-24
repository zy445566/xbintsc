/**
 * Regression: `spawnSync` with a command path that contains spaces.
 *
 * MSVC's `_spawnvp` builds the child command line without quoting argv[0], so a
 * compiler installed under "C:\Program Files" reached the child as several
 * arguments ("Files\Microsoft", "Visual", …). It only showed up with a real
 * compile (an argument list long enough that clang did not short-circuit on
 * `--version`), which is exactly what a self-hosted `xbintsc` does on Windows.
 */

import { execFileSync } from "node:child_process";
import { expect, it } from "vitest";
import { resolveToolchain } from "../../src/driver/toolchain-provider.js";
import { toPosixPath } from "../platform.js";
import { describeE2E } from "./harness.js";

/** The resolved clang absolute path, or a `where clang` hit, when it has a space. */
function clangPathWithSpace(): string | undefined {
  if (process.platform !== "win32") return undefined;
  try {
    const resolved = resolveToolchain().clang;
    if (resolved.includes(" ")) return resolved;
  } catch {
    // Fall through to `where`.
  }
  try {
    const output = execFileSync("where", ["clang"], { encoding: "utf8" });
    return output
      .split(/\r?\n/)
      .map((line) => line.trim())
      .find((line) => line.length > 0 && line.includes(" "));
  } catch {
    return undefined;
  }
}

const spacedClang = clangPathWithSpace();

describeE2E("child_process.spawnSync", (harness) => {
  it.skipIf(spacedClang === undefined)("runs a command whose path contains spaces", () => {
    const directory = toPosixPath(harness.workdir);
    const clang = toPosixPath(spacedClang!);
    const source = `
import { existsSync, writeFileSync } from "node:fs";
import { spawnSync } from "node:child_process";

const directory = ${JSON.stringify(directory)};
writeFileSync(directory + "/probe.c", "int probe(void) { return 7; }\\n");
const result = spawnSync(
  ${JSON.stringify(clang)},
  ["-x", "c", "-c", directory + "/probe.c", "-o", directory + "/probe.o"],
  { encoding: "utf8" },
);
console.log(result.status, existsSync(directory + "/probe.o"));
`;
    const output = harness.runProgram(source, { extensions: true });
    expect(output).toBe("0 true");
  });
});
