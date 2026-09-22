/* eslint-disable @typescript-eslint/no-explicit-any */
import { describe, expect, it } from "vitest";
import { parse } from "../helpers.js";
import { SyntaxKind } from "../../src/ast/nodes.js";
import { DiagnosticCode } from "../../src/diagnostics/diagnostic.js";

function firstDeclaration(source: string): { decl: any; diagnostics: readonly any[] } {
  const { file, diagnostics } = parse(source);
  return { decl: file.statements[0], diagnostics };
}

describe("function declarations", () => {
  it("parses an overload signature without a body", () => {
    const { decl, diagnostics } = firstDeclaration("function f(a: number): void;");
    expect(diagnostics).toHaveLength(0);
    expect(decl.body).toBeUndefined();
  });

  it("parses type parameter constraints and defaults", () => {
    const { decl, diagnostics } = firstDeclaration('function f<T extends string = "a">() {}');
    expect(diagnostics).toHaveLength(0);
    const parameter = decl.typeParameters[0];
    expect(parameter.name.text).toBe("T");
    expect(parameter.constraint.kind).toBe(SyntaxKind.StringKeywordType);
    expect(parameter.default.kind).toBe(SyntaxKind.LiteralType);
  });

  it("parses optional parameters", () => {
    const { decl } = firstDeclaration("function f(a?: number) {}");
    expect(decl.parameters[0].questionToken).toBe(true);
  });

  it("parses array binding patterns with elisions", () => {
    const { decl, diagnostics } = firstDeclaration("function f([a, , b]: number[]) {}");
    expect(diagnostics).toHaveLength(0);
    const pattern = decl.parameters[0].name;
    expect(pattern.kind).toBe(SyntaxKind.ArrayBindingPattern);
    expect(pattern.elements).toHaveLength(3);
    expect(pattern.elements[1]).toBeUndefined();
    expect(pattern.elements[0].name.text).toBe("a");
    expect(pattern.elements[2].name.text).toBe("b");
  });

  it("parses computed keys in object binding patterns", () => {
    const { decl } = firstDeclaration("function f({ [k]: v }: any) {}");
    const element = decl.parameters[0].name.elements[0];
    expect(decl.parameters[0].name.kind).toBe(SyntaxKind.ObjectBindingPattern);
    expect(element.propertyName.text).toBe("k");
    expect(element.name.text).toBe("v");
  });

  it("falls back to a placeholder for computed keys that are not names", () => {
    const { decl } = firstDeclaration("function f({ [k + 1]: v }: any) {}");
    expect(decl.parameters[0].name.elements[0].propertyName.text).toBe("<computed>");
  });

  it("drops a type-only `this` parameter from the runtime parameter list", () => {
    const { decl, diagnostics } = firstDeclaration("function f(this: Foo, a: number) {}");
    expect(diagnostics).toHaveLength(0);
    expect(decl.parameters).toHaveLength(1);
    expect(decl.parameters[0].name.text).toBe("a");
  });

  it("parses an `x is T` type predicate return type", () => {
    const { decl, diagnostics } = firstDeclaration("function f(x: any): x is string { return true; }");
    expect(diagnostics).toHaveLength(0);
    expect(decl.returnType.kind).toBe(SyntaxKind.TypePredicate);
    expect(decl.returnType.parameterName.text).toBe("x");
    expect(decl.returnType.type.kind).toBe(SyntaxKind.StringKeywordType);
  });

  it("parses `asserts x` and `asserts x is T` return types", () => {
    const { decl: bare } = firstDeclaration("function f(x: any): asserts x {}");
    expect(bare.returnType.kind).toBe(SyntaxKind.TypePredicate);
    expect(bare.returnType.parameterName.text).toBe("x");
    expect(bare.returnType.type).toBeUndefined();

    const { decl: typed } = firstDeclaration("function f(x: any): asserts x is string {}");
    expect(typed.returnType.type.kind).toBe(SyntaxKind.StringKeywordType);
  });
});
