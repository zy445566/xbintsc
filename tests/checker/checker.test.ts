import { describe, expect, it } from "vitest";
import { DiagnosticBag, DiagnosticCode, type Diagnostic } from "../../src/diagnostics/diagnostic.js";
import { bind } from "../../src/binder/binder.js";
import { checkProgram, type CheckOptions } from "../../src/checker/checker.js";
import { isDependencyNode } from "../../src/checker/source.js";
import type { Identifier, SourceFileNode } from "../../src/ast/nodes.js";
import { compileToIr, parse } from "../helpers.js";

/** Parse, bind and check one program; assert it was syntactically valid. */
function check(source: string, options: CheckOptions = {}): readonly Diagnostic[] {
  const { file, diagnostics } = parse(source);
  expect(diagnostics, source).toEqual([]);
  const bag = new DiagnosticBag();
  checkProgram(file, bind(file), bag, options);
  return bag.diagnostics;
}

function codes(diagnostics: readonly Diagnostic[], code: DiagnosticCode): readonly Diagnostic[] {
  return diagnostics.filter((diagnostic) => diagnostic.code === code);
}

describe("checker: unresolved names", () => {
  it("reports a name that is neither declared nor a runtime global", () => {
    const diagnostics = codes(check("console.log(missing);"), DiagnosticCode.CannotFindName);
    expect(diagnostics).toHaveLength(1);
    expect(diagnostics[0]!.message).toContain("missing");
  });

  it("accepts runtime globals, namespaces and extension builtins", () => {
    const source = [
      "console.log(Math.PI, JSON.stringify({}), parseInt('1'), new Map(), new Uint8Array(1));",
      "setTimeout(() => {}, 1);",
      "Buffer.from('x');",
      "import.meta.url;",
      "print('x');",
    ].join("\n");
    expect(codes(check(source, { builtins: { print: {} } }), DiagnosticCode.CannotFindName)).toEqual([]);
  });

  it("does not treat erased type positions as value references", () => {
    const source = [
      "const value = 1 as SomeType;",
      "function identity<T>(input: T): T { return input as T; }",
      "identity<SomethingElse>(value satisfies AnotherType);",
    ].join("\n");
    expect(codes(check(source), DiagnosticCode.CannotFindName)).toEqual([]);
  });

  it("is reported exactly once even though codegen also notices the name", () => {
    const { diagnostics } = compileToIr("missing;");
    expect(diagnostics.filter((d) => d.code === DiagnosticCode.CannotFindName)).toHaveLength(1);
  });
});

describe("checker: redeclarations", () => {
  const conflicts = (source: string): readonly Diagnostic[] =>
    codes(check(source), DiagnosticCode.DuplicateIdentifier);

  it("rejects duplicate let/const bindings in one scope", () => {
    expect(conflicts("let value = 1; let value = 2;")).toHaveLength(1);
    expect(conflicts("const value = 1; const value = 2;")).toHaveLength(1);
    expect(conflicts("let value = 1; const value = 2;")).toHaveLength(1);
  });

  it("rejects a block-scoped name that collides with an earlier declaration", () => {
    expect(conflicts("var value = 1; let value = 2;")).toHaveLength(1);
    expect(conflicts("function value() {} const value = 1;")).toHaveLength(1);
    expect(conflicts("function f(a) { let a = 1; }")).toHaveLength(1);
  });

  it("allows the merges JavaScript and TypeScript define", () => {
    expect(conflicts("var a = 1; var a = 2;")).toHaveLength(0);
    expect(conflicts("function f() {} function f() {}")).toHaveLength(0);
    expect(conflicts("function f(a) { var a = 1; }")).toHaveLength(0);
    expect(conflicts("interface I { a: number } interface I { b: number }")).toHaveLength(0);
    expect(conflicts("class C {} interface C { extra: number }")).toHaveLength(0);
    expect(conflicts("type T = number; const T = 1;")).toHaveLength(0);
  });

  it("keeps the old permissive behaviour when the checker is off", () => {
    const { diagnostics } = compileToIr("let value = 1; let value = 2; console.log(value);", undefined, undefined, {
      check: false,
    });
    expect(codes(diagnostics, DiagnosticCode.DuplicateIdentifier)).toEqual([]);
  });
});

describe("checker: bundled dependencies", () => {
  const node = { kind: 0, text: "x", start: 0, end: 1 } as unknown as Identifier;
  const sourceWith = (origin: string): SourceFileNode =>
    ({ origins: new Map<object, string>([[node, origin]]) }) as unknown as SourceFileNode;

  it("marks nodes from node_modules and leaves the entry alone", () => {
    expect(isDependencyNode(sourceWith("/app/node_modules/pkg/index.js"), node)).toBe(true);
    expect(isDependencyNode(sourceWith("C:\\app\\node_modules\\pkg\\index.js"), node)).toBe(true);
    expect(isDependencyNode(sourceWith("/app/src/main.ts"), node)).toBe(false);
    expect(isDependencyNode({} as SourceFileNode, node)).toBe(false);
  });
});

describe("checker: call arity", () => {
  const arity = (source: string): readonly Diagnostic[] =>
    codes(check(source), DiagnosticCode.ArgumentCountMismatch);

  it("reports too few and too many arguments for a declared function", () => {
    expect(arity("function f(a, b) { return a + b; } f(1);")).toHaveLength(1);
    expect(arity("function f(a) { return a; } f(1, 2);")).toHaveLength(1);
  });

  it("checks const-initialised function expressions and arrows", () => {
    expect(arity("const f = (a: number) => a; f();")).toHaveLength(1);
    expect(arity("const f = function (a: number) { return a; }; f(1, 2);")).toHaveLength(1);
  });

  it("honours defaults, optional and rest parameters", () => {
    expect(arity("function f(a, b = 1) {} f(1);")).toHaveLength(0);
    expect(arity("function f(a?: number) {} f();")).toHaveLength(0);
    expect(arity("function f(a, ...rest: number[]) {} f(1, 2, 3);")).toHaveLength(0);
  });

  it("skips calls it cannot resolve without inference", () => {
    const source = [
      "function f(a) { return a; }",
      "f(...[1, 2]);",
      "const object = { m(a: number) { return a; } }; object.m();",
      "function reassigned(a) {} reassigned = (a, b) => 1; reassigned(1, 2);",
      "function variadic() { return arguments.length; } variadic(1, 2, 3);",
      "const unknown = (globalThis as unknown) as (a: number) => number; unknown();",
    ].join("\n");
    expect(arity(source)).toHaveLength(0);
  });
});
