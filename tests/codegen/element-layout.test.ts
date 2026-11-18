/**
 * The inline element fast path reads fields straight out of the runtime's
 * container structs (`xt_array.items`, `xt_object.view_bytes`, …), so the
 * compiler's byte offsets in `src/codegen/generator/layout.ts` must match
 * `runtime/rt_object.h`.
 *
 * This case compiles a tiny C probe against the runtime header with the same
 * clang the compiler uses and compares the real `offsetof` values with the
 * constants. A layout change without updating the compiler therefore fails here
 * instead of silently reading the wrong field of every array in every compiled
 * program.
 *
 * The probe must be built with the same flags the driver uses, macOS SDK
 * included: `resolveToolchain().clang` is an absolute path in CI, and an Apple
 * clang invoked that way has no sysroot of its own. Without `-isysroot` the
 * first runtime header — `setjmp.h` — is not found on macOS, even though every
 * other case here passes because the driver hands clang that flag.
 */

import { spawnSync } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterAll, describe, expect, it } from "vitest";
import { XT_LAYOUT } from "../../src/codegen/generator/layout.js";
import { resolveToolchain } from "../../src/driver/toolchain-provider.js";
import { sysrootFlags } from "../../src/driver/toolchain.js";
import { findRuntimeDir } from "../../src/driver/paths.js";
import { hasClang } from "../helpers.js";

const describeWithClang = hasClang() ? describe : describe.skip;
const workdir = mkdtempSync(join(tmpdir(), "xbintsc-layout-"));
const probe = join(workdir, "layout.c");
const binary = join(workdir, process.platform === "win32" ? "layout.exe" : "layout");

const SOURCE = `
#include "rt_internal.h"
#include <stddef.h>
#include <stdio.h>

#define REPORT(label, value) printf("%s %zu\\n", label, (size_t)(value))

int main(void) {
  REPORT("headerSize", sizeof(xt_header));
  REPORT("arrayLength", offsetof(xt_array, length));
  REPORT("arrayItems", offsetof(xt_array, items));
  REPORT("objectTypedKind", offsetof(xt_object, reserved2));
  REPORT("objectViewBytes", offsetof(xt_object, view_bytes));
  REPORT("objectViewLength", offsetof(xt_object, view_length));
  return 0;
}
`;

afterAll(() => rmSync(workdir, { recursive: true, force: true }));

describeWithClang("inline element layout", () => {
  it("matches the offsets the compiler hardcodes", () => {
    writeFileSync(probe, SOURCE);
    const toolchain = resolveToolchain();
    /* Same flags as the driver's C compile: `-isysroot` is mandatory on macOS,
     * where `runtime/rt_internal.h` pulls `setjmp.h` out of the SDK. */
    const compiled = spawnSync(
      toolchain.clang,
      [...sysrootFlags(toolchain.sdkRoot), "-I", findRuntimeDir(), probe, "-o", binary],
      { encoding: "utf8" },
    );
    expect(compiled.status, compiled.stderr).toBe(0);
    const ran = spawnSync(binary, [], { encoding: "utf8" });
    expect(ran.status).toBe(0);
    const reported = new Map<string, number>();
    for (const line of ran.stdout.trim().split("\n")) {
      const [label, value] = line.trim().split(/\s+/);
      if (label && value !== undefined) reported.set(label, Number(value));
    }
    const expected: Record<string, number> = {
      headerSize: XT_LAYOUT.headerSize,
      arrayLength: XT_LAYOUT.arrayLength,
      arrayItems: XT_LAYOUT.arrayItems,
      objectTypedKind: XT_LAYOUT.objectTypedKind,
      objectViewBytes: XT_LAYOUT.objectViewBytes,
      objectViewLength: XT_LAYOUT.objectViewLength,
    };
    for (const [label, value] of Object.entries(expected)) {
      expect(reported.get(label), `${label}: rt_object.h vs layout.ts`).toBe(value);
    }
  });
});


