import { describe, expect, it } from "vitest";
import { bindSource } from "../helpers.js";
import { SymbolKind } from "../../src/binder/binder.js";
import { SyntaxKind } from "../../src/ast/nodes.js";

describe("binder", () => {
  it("records the module function and static topology", () => {
    const { result } = bindSource("function f() {}\nconst x = 1;");
    expect(result.moduleFunction.isModule).toBe(true);
    expect(result.functions).toHaveLength(2);
    expect(result.functions.map((fn) => fn.name)).toEqual(["(module)", "f"]);
  });

  it("classifies symbol kinds", () => {
    const { result } = bindSource("var a = 1; let b = 2; const c = 3; function d() {}");
    const moduleScope = result.moduleFunction.scope;
    expect(moduleScope.symbols.get("a")?.kind).toBe(SymbolKind.Var);
    expect(moduleScope.symbols.get("b")?.kind).toBe(SymbolKind.Let);
    expect(moduleScope.symbols.get("c")?.kind).toBe(SymbolKind.Const);
    expect(moduleScope.symbols.get("d")?.kind).toBe(SymbolKind.Function);
  });

  it("marks const bindings immutable", () => {
    const { result } = bindSource("const a = 1; let b = 2;");
    const scope = result.moduleFunction.scope;
    expect(scope.symbols.get("a")?.mutable).toBe(false);
    expect(scope.symbols.get("b")?.mutable).toBe(true);
  });

  it("hoists var and function declarations to the function scope", () => {
    const { result } = bindSource("function outer() {\n  {\n    var hoisted = 1;\n    function inner() {}\n  }\n}");
    const outer = result.functions.find((fn) => fn.name === "outer")!;
    const names = outer.locals.map((symbol) => symbol.name);
    expect(names).toContain("hoisted");
    expect(names).toContain("inner");
  });

  it("keeps let/const inside their block scope", () => {
    const { result } = bindSource("function outer() { { let hidden = 1; } }");
    const outer = result.functions.find((fn) => fn.name === "outer")!;
    expect(outer.scope.symbols.has("hidden")).toBe(false);
    const declaresHidden = [...result.scopes.values()].some((scope) => scope.symbols.has("hidden"));
    expect(declaresHidden).toBe(true);
    expect(result.scopes.size).toBeGreaterThanOrEqual(3);
  });

  it("resolves identifiers to their declarations", () => {
    const { result } = bindSource("const value = 1; function get() { return value; }");
    const get = result.functions.find((fn) => fn.name === "get")!;
    const returnStatement = (get.node as { body: { statements: { expression: unknown }[] } }).body.statements[0] as {
      expression: unknown;
    };
    const reference = returnStatement.expression as Parameters<typeof result.symbolOfIdentifier.get>[0];
    expect(result.symbolOfIdentifier.get(reference)?.name).toBe("value");
  });

  it("does not capture module-level functions as values", () => {
    const { result } = bindSource("function fib(n) { return fib(n - 1); }");
    const fib = result.functions.find((fn) => fn.name === "fib")!;
    expect(fib.captures).toHaveLength(0);
  });

  it("marks captured variables and threads them through intermediate closures", () => {
    const { result } = bindSource(
      "function outer() { let count = 0; const middle = () => () => count; return middle; }",
    );
    const outer = result.functions.find((fn) => fn.name === "outer")!;
    const arrows = result.functions.filter((fn) => fn.isArrow);
    expect(arrows).toHaveLength(2);
    // The middle arrow captures `count` only because the inner arrow needs it.
    for (const arrow of arrows) {
      expect(arrow.captures.map((symbol) => symbol.name)).toContain("count");
    }
    const count = outer.locals.find((symbol) => symbol.name === "count")!;
    expect(count.captured).toBe(true);
    expect(count.boxed).toBe(true);
  });

  it("assigns stable capture indices", () => {
    const { result } = bindSource("function outer() { let a = 1; let b = 2; return () => a + b; }");
    const arrow = result.functions.find((fn) => fn.isArrow)!;
    expect(arrow.captures).toHaveLength(2);
    expect(arrow.captureIndex.get(arrow.captures[0]!.id)).toBe(0);
    expect(arrow.captureIndex.get(arrow.captures[1]!.id)).toBe(1);
  });

  it("maps function nodes back to FunctionInfo", () => {
    const { result } = bindSource("function declared() {}\nconst arrow = () => 1;");
    const moduleFunction = result.moduleFunction;
    const declaration = (moduleFunction.node as { statements: { kind: SyntaxKind }[] }).statements[0]!;
    expect(result.functionOfNode.get(declaration as never)?.name).toBe("declared");
  });

  it("registers parameters as local symbols", () => {
    const { result } = bindSource("function add(a, b) { return a + b; }");
    const add = result.functions.find((fn) => fn.name === "add")!;
    expect(add.params.map((symbol) => symbol.name)).toEqual(["a", "b"]);
    expect(add.locals.map((symbol) => symbol.name)).toEqual(["a", "b"]);
  });

  it("tracks unresolved identifier references", () => {
    const { result } = bindSource("missingName;");
    expect(result.unresolved.map((identifier) => identifier.text)).toContain("missingName");
  });
});
