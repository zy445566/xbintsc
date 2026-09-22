import { describe, expect, it } from "vitest";
import { parse } from "../helpers.js";
import { SyntaxKind } from "../../src/ast/nodes.js";

/* eslint-disable @typescript-eslint/no-explicit-any */
function first(source: string): any {
  const { file, diagnostics } = parse(source);
  expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
  return file.statements[0];
}

function members(source: string): any[] {
  return first(source).members;
}

describe("parser type members", () => {
  it("parses call signatures", () => {
    const [member] = members("interface I { (x: number): string; }");
    expect(member.kind).toBe(SyntaxKind.MethodSignature);
    expect(member.name.text).toBe("__call");
    expect(member.parameters).toHaveLength(1);
    expect(member.returnType.kind).toBe(SyntaxKind.StringKeywordType);
  });

  it("parses generic call signatures with type parameters", () => {
    const [member] = members("interface I { <T>(x: T): T; }");
    expect(member.kind).toBe(SyntaxKind.MethodSignature);
    expect(member.typeParameters).toHaveLength(1);
    expect(member.returnType.kind).toBe(SyntaxKind.TypeReference);
  });

  it("parses index signatures", () => {
    const [member] = members("interface I { [key: string]: number; }");
    expect(member.kind).toBe(SyntaxKind.IndexSignature);
    expect(member.parameters[0].name.text).toBe("key");
    expect(member.parameters[0].type.kind).toBe(SyntaxKind.StringKeywordType);
    expect(member.type.kind).toBe(SyntaxKind.NumberKeywordType);
  });

  it("falls back to a property signature for an invalid index signature", () => {
    const [member] = members("interface I { [key]: number; }");
    expect(member.kind).toBe(SyntaxKind.PropertySignature);
  });

  it("parses method signatures", () => {
    const [member] = members("interface I { greet(name: string): void; }");
    expect(member.kind).toBe(SyntaxKind.MethodSignature);
    expect(member.name.text).toBe("greet");
    expect(member.questionToken).toBe(false);
    expect(member.returnType.kind).toBe(SyntaxKind.VoidKeywordType);
  });

  it("parses generic method signatures", () => {
    const [member] = members("interface I { map<T>(fn: (x: T) => T): T; }");
    expect(member.kind).toBe(SyntaxKind.MethodSignature);
    expect(member.typeParameters).toHaveLength(1);
  });

  it("parses optional, readonly and untyped property signatures", () => {
    const [readonly, plain, untyped] = members("interface I { readonly a?: number; b: string; c; }");
    expect(readonly.kind).toBe(SyntaxKind.PropertySignature);
    expect(readonly.readonlyToken).toBe(true);
    expect(readonly.questionToken).toBe(true);
    expect(readonly.type.kind).toBe(SyntaxKind.NumberKeywordType);

    expect(plain.readonlyToken).toBe(false);
    expect(plain.questionToken).toBe(false);

    expect(untyped.kind).toBe(SyntaxKind.PropertySignature);
    expect(untyped.type).toBeUndefined();
  });

  it("parses multiple members separated by commas", () => {
    const parsed = members("interface I { a: number, b: string, }");
    expect(parsed).toHaveLength(2);
  });

  it("parses module declarations with a block body", () => {
    const declaration = first("module Foo { const x = 1; }");
    expect(declaration.kind).toBe(SyntaxKind.ModuleDeclaration);
    expect(declaration.name.kind).toBe(SyntaxKind.Identifier);
    expect(declaration.name.text).toBe("Foo");
    expect(declaration.body.kind).toBe(SyntaxKind.Block);
  });

  it("parses dotted namespace declarations without a body", () => {
    const declaration = first("namespace A.B.C;");
    expect(declaration.kind).toBe(SyntaxKind.ModuleDeclaration);
    expect(declaration.name.text).toBe("A");
    expect(declaration.body).toBeUndefined();
  });

  it("parses string-named ambient modules", () => {
    const declaration = first('declare module "foo" { }');
    expect(declaration.kind).toBe(SyntaxKind.ModuleDeclaration);
    expect(declaration.name.kind).toBe(SyntaxKind.StringLiteral);
    expect(declaration.name.value).toBe("foo");
    expect(declaration.body.kind).toBe(SyntaxKind.Block);
  });
});
