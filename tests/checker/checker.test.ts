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

describe("checker: types", () => {
  const mismatches = (source: string): readonly Diagnostic[] =>
    codes(check(source), DiagnosticCode.TypeMismatch);

  it("checks initializers, assignments, returns and arguments", () => {
    expect(mismatches('const value: number = "hello";')).toHaveLength(1);
    expect(mismatches('let value: number = 1; value = "hello";')).toHaveLength(1);
    expect(mismatches('function f(): number { return "hello"; }')).toHaveLength(1);
    expect(mismatches('function f(value: number) {} f("hello");')).toHaveLength(1);
  });

  it("checks interface and class shapes structurally", () => {
    const good = [
      "interface Point { x: number; y: number }",
      "const point: Point = { x: 1, y: 2 };",
      "class Box { value = 1; }",
      "const box: Box = new Box();",
      "console.log(point.x, box.value);",
    ].join("\n");
    expect(mismatches(good)).toHaveLength(0);
    expect(mismatches("interface Point { x: number } const point: Point = { y: 1 };")).toHaveLength(1);
    expect(mismatches("interface Point { x: number } const point: Point = { x: 's' };")).toHaveLength(1);
  });

  it("infers a simple generic return type", () => {
    const source = [
      "function identity<T>(value: T): T { return value; }",
      "const good: number = identity(1);",
      "const bad: string = identity(1);",
      "console.log(good, bad);",
    ].join("\n");
    expect(mismatches(source)).toHaveLength(1);
  });

  it("narrows nullish unions inside an if block", () => {
    const source = [
      "interface Box { value: number }",
      "const box: Box | null = null;",
      "if (box !== null) { console.log(box.value); }",
    ].join("\n");
    expect(codes(check(source), DiagnosticCode.PropertyNotFound)).toHaveLength(0);
    const missing = [
      "interface Box { value: number }",
      "const box: Box | null = null;",
      "if (box !== null) { console.log(box.missing); }",
    ].join("\n");
    expect(codes(check(missing), DiagnosticCode.PropertyNotFound)).toHaveLength(1);
  });

  it("reports calls to a definitely non-callable value", () => {
    const diagnostics = codes(check("const value: number = 1; value();"), DiagnosticCode.NotCallable);
    expect(diagnostics).toHaveLength(1);
  });

  it("widens let literals and treats any as compatible with everything", () => {
    expect(mismatches("let value = 1; value = 2;")).toHaveLength(0);
    expect(mismatches('const anything: any = "s"; const n: number = anything;')).toHaveLength(0);
    expect(codes(check("const anything: any = {}; anything.missing.deep;"), DiagnosticCode.PropertyNotFound)).toHaveLength(0);
    expect(codes(check("const anything: any = {}; anything();"), DiagnosticCode.NotCallable)).toHaveLength(0);
  });
});

describe("checker: property existence", () => {
  const missing = (source: string): readonly Diagnostic[] =>
    codes(check(source), DiagnosticCode.PropertyNotFound);

  it("reports a missing property on a const object literal", () => {
    expect(missing("const object = { a: 1 }; console.log(object.b);")).toHaveLength(1);
    expect(missing("const object = { a: 1 }; console.log(object!.b);")).toHaveLength(1);
  });

  it("allows declared keys and Object.prototype members", () => {
    const source = "const object = { a: 1 }; console.log(object.a, object.toString(), object.hasOwnProperty('a'));";
    expect(missing(source)).toHaveLength(0);
  });

  it("stops checking once the shape can change", () => {
    expect(missing("const object = { a: 1 }; object.b = 2; console.log(object.b);")).toHaveLength(0);
    expect(missing("const object = { a: 1 }; Object.assign(object, { b: 2 }); console.log(object.b);")).toHaveLength(0);
    expect(missing("const object = { a: 1 }; mutate(object); console.log(object.b);")).toHaveLength(0);
    expect(missing("const object = { a: 1 }; const alias = object; alias.b = 2; console.log(object.b);")).toHaveLength(0);
  });

  it("skips spread and computed keys", () => {
    expect(missing("const key = 'b'; const object = { a: 1, [key]: 2 }; console.log(object.c);")).toHaveLength(0);
    expect(missing("const base = { b: 2 }; const object = { a: 1, ...base }; console.log(object.c);")).toHaveLength(0);
  });

  it("checks class instances, inherited members and parameter properties", () => {
    const source = [
      "class Base { a = 1; m() { return this.a; } }",
      "class Child extends Base { b = 2; constructor(public c: number) { super(); } }",
      "const instance = new Child(3);",
      "console.log(instance.a, instance.b, instance.c, instance.m());",
    ].join("\n");
    expect(missing(source)).toHaveLength(0);
    expect(missing("class A { a = 1; } const instance = new A(); console.log(instance.nope);")).toHaveLength(1);
  });

  it("does not treat static members as instance members", () => {
    expect(missing("class A { static kind = 'a'; } const instance = new A(); console.log(instance.kind);")).toHaveLength(1);
  });

  it("tracks this.x assignments but gives up on a dynamic shape", () => {
    expect(missing("class A { constructor() { this.x = 1; } } const a = new A(); console.log(a.x);")).toHaveLength(0);
    expect(missing("class A { constructor(opts: any) { Object.assign(this, opts); } } const a = new A({ y: 1 }); console.log(a.y);")).toHaveLength(0);
    expect(missing("class A { fill(key: string) { (this as any)[key] = 1; } } const a = new A(); console.log(a.y);")).toHaveLength(0);
  });

  it("gives up when the prototype is extended outside the class body", () => {
    expect(missing("class A {} Object.assign(A.prototype, { m() { return 1; } }); const a = new A(); console.log(a.m());")).toHaveLength(0);
    expect(missing("class A {} A.prototype.m = function () { return 1; }; const a = new A(); console.log(a.m());")).toHaveLength(0);
  });

  it("skips classes with an unknown base", () => {
    expect(missing("class A extends Error {} const a = new A(); console.log(a.anything);")).toHaveLength(0);
  });

  it("only trusts const bindings", () => {
    expect(missing("let object: any = { a: 1 }; console.log(object.b);")).toHaveLength(0);
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
