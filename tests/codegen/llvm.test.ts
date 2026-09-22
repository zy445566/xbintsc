import { describe, expect, it } from "vitest";
import { compileToIr } from "../helpers.js";
import { numberLiteral } from "../../src/codegen/values.js";
import { requiresSetjmpex } from "../../src/codegen/generator/tables.js";
import { DiagnosticCode } from "../../src/diagnostics/diagnostic.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { nodeExtension } from "../../src/extensions/node/index.js";

function defineNames(ir: string): string[] {
  return [...ir.matchAll(/^define i64 @([A-Za-z0-9_.]+)/gm)].map((match) => match[1]!);
}

describe("codegen", () => {
  it("emits a module with a C entry point", () => {
    const { ir, diagnostics } = compileToIr("console.log(1);");
    expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
    expect(ir).toContain("define i32 @main");
    expect(ir).toMatch(/declare i64 @xt_add/);
  });

  it("defines the module function plus one LLVM function per user function", () => {
    const { ir } = compileToIr("function a() {}\nfunction b() {}\nconst c = () => 1;");
    const names = defineNames(ir);
    expect(names).toHaveLength(4);
    expect(names[0]).toBe("xt_module");
    expect(new Set(names).size).toBe(names.length);
  });

  it("emits numeric constants using the NaN-boxed bit pattern", () => {
    const { ir } = compileToIr("const x = 42;");
    expect(ir).toContain(numberLiteral(42));
  });

  it("emits string literals as private globals and allocates them at runtime", () => {
    const { ir } = compileToIr('console.log("hello");');
    expect(ir).toContain('@.str.0 = private unnamed_addr constant');
    expect(ir).toMatch(/call i64 @xt_string_new\(i8\* @\.str\.0, i64 5\)/);
  });

  it("escapes non-ASCII bytes in string constants", () => {
    const { ir } = compileToIr('console.log("héllo→");');
    expect(ir).toContain("\\C3\\A9");
    expect(ir).toContain("\\E2\\86\\92");
  });

  it("lowers arithmetic to runtime helpers", () => {
    const { ir } = compileToIr("const x = a + b * c;");
    expect(ir).toContain("@xt_mul");
    expect(ir).toContain("@xt_add");
  });

  it("lowers comparisons and logical operators", () => {
    const { ir } = compileToIr("const x = a < b && c;");
    expect(ir).toContain("@xt_lt");
    expect(ir).toContain("@xt_truthy");
  });

  it("uses explicit basic blocks for conditionals", () => {
    const { ir } = compileToIr("if (a) { b(); } else { c(); }");
    expect(ir).toMatch(/if\.then/);
    expect(ir).toMatch(/if\.else/);
    expect(ir).toMatch(/if\.end/);
  });

  it("lowers while loops with cond/body/end blocks", () => {
    const { ir } = compileToIr("while (a) { b(); }");
    expect(ir).toMatch(/while\.cond/);
    expect(ir).toMatch(/while\.body/);
    expect(ir).toMatch(/while\.end/);
  });

  it("only asks for `_setjmpex` on 64-bit Windows on ARM", () => {
    expect(requiresSetjmpex("win32", "arm64")).toBe(true);
    expect(requiresSetjmpex("win32", "x64")).toBe(false);
    expect(requiresSetjmpex("linux", "arm64")).toBe(false);
    expect(requiresSetjmpex("darwin", "x64")).toBe(false);
  });

  it("saves try frames with `_setjmp` on 32/64-bit non-ARM hosts", () => {
    const { ir } = compileToIr("function a() {}\nfunction b() {}\ntry { a(); } catch (e) { b(); }", undefined, {
      platform: "win32",
      arch: "x64",
    });
    expect(ir).toContain("call i32 @_setjmp(i8*");
    expect(ir).toContain("@llvm.frameaddress");
    expect(ir).not.toContain("@_setjmpex");
  });

  it("saves try frames with `_setjmpex`/`sponentry` on Windows ARM64 (which has no `_setjmp`)", () => {
    const { ir, diagnostics } = compileToIr(
      "function a() {}\nfunction b() {}\ntry { a(); } catch (e) { b(); }",
      undefined,
      { platform: "win32", arch: "arm64" },
    );
    expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
    expect(ir).toContain("declare i8* @llvm.sponentry()");
    expect(ir).toContain("declare i32 @_setjmpex(i8*, i8*) returns_twice");
    expect(ir).toMatch(/call i8\* @llvm\.sponentry\(\)/);
    expect(ir).toMatch(/call i32 @_setjmpex\(i8\* %\w+, i8\* %\w+\)/);
  });

  it("allocates locals with alloca and promotes reads/writes through slots", () => {
    const { ir } = compileToIr("let x = 1; x = 2; console.log(x);");
    expect(ir).toContain("alloca i64");
    expect(ir).toContain("store i64");
    expect(ir).toContain("load i64");
  });

  it("passes closures an environment and reads captures from it", () => {
    const { ir } = compileToIr("function outer() { let c = 1; return () => c; }");
    expect(ir).toContain("@xt_box_new");
    expect(ir).toContain("@xt_closure_new");
    expect(ir).toContain("@xt_closure_env");
  });

  it("routes console.log through the runtime", () => {
    const { ir } = compileToIr("console.log(1, 2);");
    expect(ir).toContain("@xt_console_log");
  });

  it("builds object and array literals through runtime constructors", () => {
    const { ir } = compileToIr("const o = { a: 1 };\nconst a = [1, 2];");
    expect(ir).toContain("@xt_object_new");
    expect(ir).toContain("@xt_object_set");
    expect(ir).toContain("@xt_array_new");
    expect(ir).toContain("@xt_array_push");
  });

  it("lowers enum declarations to an object with reverse mapping", () => {
    const { ir, diagnostics } = compileToIr("enum E { A, B = 5, C }\nconsole.log(E.A, E[5]);");
    expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
    expect(ir).toContain("@xt_object_new");
    expect(ir).toContain("@xt_set");
  });

  it("lowers destructuring bindings via runtime reads", () => {
    const { ir, diagnostics } = compileToIr("const p = [1, 2];\nconst [a, b] = p;\nconst o = { c: 3 };\nconst { c } = o;\nconsole.log(a, b, c);");
    expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
    expect(ir).toMatch(/call i64 @xt_get\(i64 %\w+, i64 %\w+\)/);
  });

  it("lowers typeof and void expressions", () => {
    const { ir, diagnostics } = compileToIr("const x = 1;\nconst t = typeof x;\nconst v = void 0;");
    expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
    expect(ir).toContain("@xt_typeof");
  });

  it("resolves imported extension builtins to their runtime symbols", () => {
    const registry = createDefaultRegistry().register(nodeExtension);
    const { ir } = compileToIr('import { readFileSync } from "fs";\nreadFileSync("f.txt");', registry);
    expect(ir).toContain("declare i64 @xt_node_read_text_file(i32, i64*)");
    expect(ir).toMatch(/call i64 @xt_node_read_text_file\(i32 \d+, i64\* %\w+\)/);
  });

  it("resolves namespace imports to the namespace dispatcher", () => {
    const registry = createDefaultRegistry().register(nodeExtension);
    const { ir } = compileToIr('import * as path from "path";\npath.join("a", "b");', registry);
    expect(ir).toMatch(/call i64 @xt_path_static\(i64 %\w+, i32 2, i64\* %\w+\)/);
  });

  it("resolves default and namespace imports of dispatcher-less node modules", () => {
    const registry = createDefaultRegistry().register(nodeExtension);
    const sources = [
      'import fs from "node:fs";\nfs.readFileSync("f.txt");',
      'import * as fs from "node:fs";\nfs.readFileSync("f.txt");',
      'import cp from "node:child_process";\ncp.spawnSync("ls");',
    ];
    for (const source of sources) {
      const { diagnostics } = compileToIr(source, registry);
      expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
    }
    const { ir } = compileToIr('import fs from "node:fs";\nfs.readFileSync("f.txt");', registry);
    expect(ir).toMatch(/call i64 @xt_node_read_text_file\(i32 \d+, i64\* %\w+\)/);
  });

  it("reports unsupported syntax instead of crashing", () => {
    const { diagnostics } = compileToIr("tag`x`;");
    expect(diagnostics.some((d) => d.category === "error")).toBe(true);
  });

  it("rejects assignment to const bindings", () => {
    const { diagnostics } = compileToIr("const x = 1;\nx = 2;");
    expect(diagnostics.some((d) => d.code === DiagnosticCode.CannotAssignToConst)).toBe(true);
  });

  it("rejects compound, update and destructuring assignment to const bindings", () => {
    const sources = [
      "const x = 1;\nx += 2;",
      "const x = 1;\nx++;",
      "const [a] = [1];\na = 2;",
      "const x = 1;\nfunction f() { x = 2; }",
    ];
    for (const source of sources) {
      const { diagnostics } = compileToIr(source);
      expect(diagnostics.some((d) => d.code === DiagnosticCode.CannotAssignToConst)).toBe(true);
    }
  });

  it("allows assigning to let bindings, object properties and for-of constants", () => {
    const sources = ["let x = 1;\nx = 2;", "const o = {};\no.a = 1;", "for (const v of [1, 2]) { v; }"];
    for (const source of sources) {
      const { diagnostics } = compileToIr(source);
      expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
    }
  });
});
