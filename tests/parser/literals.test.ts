/* eslint-disable @typescript-eslint/no-explicit-any */
import { describe, expect, it } from "vitest";
import { parse } from "../helpers.js";
import { NodeFlags, SyntaxKind } from "../../src/ast/nodes.js";
import { DiagnosticCode } from "../../src/diagnostics/diagnostic.js";

function initializerOf(source: string): { init: any; diagnostics: readonly any[] } {
  const { file, diagnostics } = parse(source);
  const declaration = (file.statements[0] as any).declarationList.declarations[0];
  return { init: declaration.initializer, diagnostics };
}

describe("literal expressions", () => {
  it("represents array holes as undefined literals", () => {
    const { init, diagnostics } = initializerOf("const a = [1, , 2];");
    expect(diagnostics).toHaveLength(0);
    expect(init.kind).toBe(SyntaxKind.ArrayLiteralExpression);
    expect(init.elements).toHaveLength(3);
    expect(init.elements[1].kind).toBe(SyntaxKind.UndefinedKeyword);
  });

  it("parses object-literal generator methods with return types", () => {
    const { init, diagnostics } = initializerOf("const o = { *gen(): number { yield 1; } };");
    expect(diagnostics).toHaveLength(0);
    const fn = init.properties[0].initializer;
    expect(fn.flags & NodeFlags.Generator).toBe(NodeFlags.Generator);
    expect(fn.returnType.kind).toBe(SyntaxKind.NumberKeywordType);
  });

  it("reports invalid object literal members", () => {
    const { diagnostics } = parse("const o = { 1 2 };");
    expect(diagnostics.some((d) => d.code === DiagnosticCode.UnexpectedToken)).toBe(true);
  });

  it("parses generator function expressions", () => {
    const { init } = initializerOf("const f = function* () { yield 1; };");
    expect(init.kind).toBe(SyntaxKind.FunctionExpression);
    expect(init.flags & NodeFlags.Generator).toBe(NodeFlags.Generator);
  });

  it("parses anonymous and named class expressions", () => {
    const { init: anonymous } = initializerOf("const C = class {};");
    expect(anonymous.kind).toBe(SyntaxKind.ClassExpression);
    expect(anonymous.name).toBeUndefined();

    const { init: named, diagnostics } = initializerOf("const C = class Named<T> extends Object {};");
    expect(diagnostics).toHaveLength(0);
    expect(named.name.text).toBe("Named");
    expect(named.typeParameters).toHaveLength(1);
  });

  it("keeps BigInt literal values exact as bigint", () => {
    const { init, diagnostics } = initializerOf("const a = 123456789012345678901234567890n;");
    expect(diagnostics).toHaveLength(0);
    expect(init.kind).toBe(SyntaxKind.BigIntLiteral);
    expect(typeof init.value).toBe("bigint");
    expect(init.value).toBe(123456789012345678901234567890n);
  });

  it("parses regular expression literals with flags", () => {
    const { init } = initializerOf("const r = /ab+c/gi;");
    expect(init.kind).toBe(SyntaxKind.RegularExpressionLiteral);
    expect(init.pattern).toBe("ab+c");
    expect(init.flags).toBe("gi");
  });

  it("reports an unterminated regular expression", () => {
    const { diagnostics } = parse("const r = /abc;");
    expect(diagnostics.some((d) => d.code === DiagnosticCode.InvalidCharacter)).toBe(true);
  });

  it("parses template literals with substitutions", () => {
    const { init, diagnostics } = initializerOf("const s = `a${1 + 1}b`;");
    expect(diagnostics).toHaveLength(0);
    expect(init.kind).toBe(SyntaxKind.TemplateLiteral);
    expect(init.spans).toHaveLength(1);
    expect(init.spans[0].isTail).toBe(true);
  });

  it("reports unterminated template substitutions", () => {
    const { diagnostics } = parse("const s = `${1;");
    expect(diagnostics.some((d) => d.code === DiagnosticCode.UnterminatedTemplate)).toBe(true);
  });

  it("reports an unterminated template literal", () => {
    const { diagnostics } = parse("const s = `${1 `;");
    expect(diagnostics.some((d) => d.code === DiagnosticCode.UnterminatedTemplate)).toBe(true);
  });
});
