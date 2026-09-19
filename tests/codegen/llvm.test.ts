import { describe, expect, it } from "vitest";
import { compileToIr } from "../helpers.js";
import { numberLiteral } from "../../src/codegen/values.js";
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

  it("reports unsupported syntax instead of crashing", () => {
    const { diagnostics } = compileToIr("tag`x`;");
    expect(diagnostics.some((d) => d.category === "error")).toBe(true);
  });
});
