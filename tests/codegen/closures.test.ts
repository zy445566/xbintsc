import { describe, expect, it } from "vitest";
import { compileToIr } from "../helpers.js";

function irOf(source: string): string {
  const { ir, diagnostics } = compileToIr(source);
  expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
  return ir;
}

describe("closure lowering", () => {
  it("captures free variables into the closure environment", () => {
    const ir = irOf("function outer(a: number) { const b = a + 1; return () => b; }");
    expect(ir).toContain("@xt_closure_new");
    expect(ir).toContain("@xt_closure_env");
  });

  it("captures `this` from an enclosing method", () => {
    const ir = irOf("class C { v = 1; m() { return () => this.v; } }");
    expect(ir).toContain("@xt_closure_new");
    expect(ir).toMatch(/alloca i64, i32 1/);
  });

  it("records arity before defaults and rest parameters", () => {
    const ir = irOf("const f = (a: number, b = 1, ...rest: number[]) => a;");
    expect(ir).toMatch(/@xt_function_set_metadata\(i64 %r\d+, i64 %r\d+, i32 1\)/);
  });

  it("marks generator function expressions", () => {
    const ir = irOf("const g = function* () { yield 1; };");
    expect(ir).toContain("@xt_function_set_generator");
  });

  it("names named function expressions", () => {
    const ir = irOf("const h = function inner() { return 1; };");
    expect(ir).toContain("inner");
  });
});
