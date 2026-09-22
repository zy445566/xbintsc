import { describe, expect, it } from "vitest";
import { compileToIr } from "../helpers.js";
import { DiagnosticCode } from "../../src/diagnostics/diagnostic.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { nodeExtension } from "../../src/extensions/node/index.js";

function errorsOf(source: string) {
  return compileToIr(source).diagnostics.filter((d) => d.category === "error");
}

function irOf(source: string): string {
  const { ir, diagnostics } = compileToIr(source);
  expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
  return ir;
}

function irWithNodeExtension(source: string): string {
  const registry = createDefaultRegistry();
  registry.register(nodeExtension);
  const { ir, diagnostics } = compileToIr(source, registry);
  expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
  return ir;
}

describe("codegen optional chaining", () => {
  it("short-circuits optional property access", () => {
    const ir = irOf("const o: any = {};\nconst v = o?.a;");
    expect(ir).toContain("@xt_is_nullish");
    expect(ir).toContain("optchain.end");
  });

  it("short-circuits optional element access", () => {
    const ir = irOf('const o: any = {};\nconst k = "a";\nconst v = o?.[k];');
    expect(ir).toContain("optchain.end");
    expect(ir).toContain("@xt_is_nullish");
  });

  it("short-circuits a longer optional chain", () => {
    const ir = irOf("const o: any = {};\nconst v = o?.a.b;");
    expect(ir).toContain("optchain.end");
  });

  it("guards an optional call to a plain function value", () => {
    const ir = irOf("const f: any = () => 1;\nf?.();");
    expect(ir).toContain("optchain.end");
    expect(ir).toContain("@xt_closure_call");
  });

  it("binds `this` for an optional method call", () => {
    const ir = irOf("const o: any = {};\no.m?.();");
    expect(ir).toContain("optchain.end");
    expect(ir).toContain("@xt_call_with_this");
  });

  it("guards a receiver before calling a method", () => {
    const ir = irOf("const o: any = {};\no?.m();");
    expect(ir).toContain("optchain.end");
    expect(ir).toContain("@xt_call_method");
  });

  it("dispatches calls through an element access", () => {
    const ir = irOf('const o: any = {};\no["m"](1);');
    expect(ir).toContain("@xt_call_method");
  });

  it("binds `this` for an optional element method call", () => {
    const ir = irOf('const a: any = {};\nconst k = "m";\na[k]?.();');
    expect(ir).toContain("@xt_call_with_this");
    expect(ir).toContain("optchain.end");
  });

  it("guards a receiver for an optional element call", () => {
    const ir = irOf('const a: any = {};\nconst k = "m";\na?.[k]();');
    expect(ir).toContain("@xt_call_method");
    expect(ir).toContain("optchain.end");
  });

  it("invokes the result of an optional chain as a closure", () => {
    const ir = irOf("const a: any = {};\na?.b()();");
    expect(ir).toContain("@xt_closure_call");
    expect(ir).toContain("optchain.end");
  });

  it("reads a `super` property through the prototype chain", () => {
    const ir = irOf("class A { x = 1; } class B extends A { m() { return super.x; } }");
    expect(ir).toContain("@xt_object_get_prototype");
  });

  it("routes global namespace properties through their runtime getter", () => {
    const ir = irOf("const p = process.pid;");
    expect(ir).toContain("@xt_process_get");
  });

  it("routes imported namespace properties through their runtime getter", () => {
    const ir = irWithNodeExtension('import * as proc from "process";\nconst p = proc.pid;');
    expect(ir).toContain("@xt_process_get");
  });
});

describe("codegen delete", () => {
  it("lowers delete of a property and an element", () => {
    const ir = irOf('const o: any = { a: 1 };\ndelete o.a;\ndelete o["b"];');
    expect(ir).toContain("@xt_delete");
  });

  it("treats delete of a bare identifier as a no-op", () => {
    const ir = irOf("let y = 1;\ndelete y;");
    // No runtime delete call is emitted for a plain binding.
    expect(ir).toContain("define i32 @main");
  });
});

describe("codegen assignment", () => {
  it("lowers compound assignment through the binary runtime", () => {
    const ir = irOf("let a = 1;\na += 2;");
    expect(ir).toContain("@xt_add");
  });

  it("lowers &&=, ||= and ??= with explicit blocks", () => {
    const ir = irOf("let a: any = 1;\na &&= 2;\na ||= 3;\na ??= 4;");
    expect(ir).toContain("assign.right");
    expect(ir).toContain("assign.end");
    expect(ir).toContain("@xt_is_nullish");
  });

  it("destructures an array assignment through runtime reads", () => {
    const ir = irOf("let a: any;\nlet b: any;\n[a, b] = [1, 2];");
    expect(ir).toContain("@xt_get");
  });

  it("supports default values and rest in array assignments", () => {
    const ir = irOf("let a: any;\nlet rest: any;\n[a = 5, ...rest] = [1, 2, 3];");
    expect(ir).toContain("@xt_get");
    expect(ir).toContain("@xt_call_method");
  });

  it("destructures object shorthand, renamed and computed targets", () => {
    const sources = [
      "let a: any;\n({ a } = { a: 1 });",
      "let o: any = {};\n({ x: o.y } = { x: 1 });",
      'let k = "x";\nlet o: any = {};\n({ [k]: o.y } = { x: 1 });',
    ];
    for (const source of sources) {
      const { ir, diagnostics } = compileToIr(source);
      expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
      expect(ir).toMatch(/@xt_(get|set)/);
    }
  });

  it("supports defaults in object shorthand assignment", () => {
    const ir = irOf("let a: any;\n({ a = 1 } = {});");
    expect(ir).toContain("@xt_get");
  });

  it("reports object rest destructuring as unsupported", () => {
    const diagnostics = errorsOf("let o: any;\n({ ...o } = { a: 1 });");
    expect(diagnostics.some((d) => d.code === DiagnosticCode.UnsupportedFeature)).toBe(true);
  });
});

describe("codegen unary operators", () => {
  it("lowers logical, bitwise and arithmetic unary operators", () => {
    const ir = irOf("let x = 1;\nconst a = !x;\nconst b = ~x;\nconst c = +x;\nconst d = -x;");
    expect(ir).toContain("@xt_not");
    expect(ir).toContain("@xt_bit_not");
    expect(ir).toContain("@xt_pos");
    expect(ir).toContain("@xt_neg");
  });

  it("lowers prefix and postfix increments", () => {
    const ir = irOf("let x = 1;\nx++;\n++x;\nx--;\n--x;");
    expect(ir).toContain("@xt_add");
    expect(ir).toContain("@xt_sub");
  });
});
