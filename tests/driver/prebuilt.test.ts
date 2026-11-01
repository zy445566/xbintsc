import { join } from "node:path";
import { describe, expect, it } from "vitest";
import {
  firstFailureLine,
  isPrebuiltArchivePath,
  looksLikePrebuiltLinkFailure,
} from "../../src/driver/prebuilt.js";

describe("prebuilt archive detection", () => {
  it("recognises archives that sit in a runtime lib folder", () => {
    const dir = join("tmp", "runtime", "lib", "darwin-arm64");
    expect(isPrebuiltArchivePath(join(dir, "core.a"), [dir])).toBe(true);
    expect(isPrebuiltArchivePath(join(dir, "gui.lib"), [dir])).toBe(true);
    // A native extension's own object/archive is not a runtime archive.
    expect(isPrebuiltArchivePath(join(dir, "media.o"), [dir])).toBe(false);
    expect(isPrebuiltArchivePath(join(dir, "notes.txt"), [dir])).toBe(false);
    // Same file name, different folder.
    expect(isPrebuiltArchivePath(join("elsewhere", "core.a"), [dir])).toBe(false);
  });

  it("matches the linker's toolchain-mismatch wording", () => {
    const messages = [
      "gui.lib : fatal error LNK2038: mismatch detected for 'RuntimeLibrary': value 'MT_StaticRelease' ...",
      "main.obj : error LNK2019: unresolved external symbol __std_init_once_begin_initialize referenced ...",
      "ld: Undefined symbols:\n  __ZNSt3__16format...",
      "/usr/bin/ld: foo.o: undefined reference to `__cxa_throw_bad_array_new_length'",
      "ld: symbol(s) not found for architecture arm64",
      "ld: warning: object file (core.a(xt_alloc.o)) was built for newer macOS version (15.0) than being linked (13.0)",
    ];
    for (const message of messages) expect(looksLikePrebuiltLinkFailure(message)).toBe(true);
  });

  it("does not treat ordinary toolchain errors as a version mismatch", () => {
    for (const message of [
      "clang: error: no such file or directory: 'main.o'",
      "ld: library not found for -lSDL3",
      "fatal error: 'rt.h' file not found",
      "permission denied",
      "",
    ]) {
      expect(looksLikePrebuiltLinkFailure(message)).toBe(false);
    }
  });

  it("summarises the first non-empty stderr line", () => {
    expect(firstFailureLine("\n  \nld: Undefined symbols:\n  _foo")).toBe("ld: Undefined symbols:");
    expect(firstFailureLine("   ")).toBe("");
    expect(firstFailureLine("x".repeat(400))).toHaveLength(160);
  });
});
