/* eslint-disable @typescript-eslint/no-explicit-any */
import { describe, expect, it } from "vitest";
import { parse } from "../helpers.js";
import { SyntaxKind, NodeFlags } from "../../src/ast/nodes.js";
import { DiagnosticCode } from "../../src/diagnostics/diagnostic.js";

function initializer(source: string): any {
  const { file, diagnostics } = parse(source);
  expect(diagnostics).toHaveLength(0);
  const stmt = file.statements[0] as any;
  return stmt.declarationList.declarations[0].initializer;
}

describe("arrow functions", () => {
  it("parses a single-parameter arrow without parentheses", () => {
    const expr = initializer("const f = x => x + 1;");
    expect(expr.kind).toBe(SyntaxKind.ArrowFunction);
    expect(expr.parameters).toHaveLength(1);
    expect(expr.parameters[0].name.text).toBe("x");
  });

  it("parses async arrows", () => {
    const expr = initializer("const f = async x => x;");
    expect(expr.kind).toBe(SyntaxKind.ArrowFunction);
    expect(expr.flags).toBe(NodeFlags.Async);
  });

  it("parses generic arrows", () => {
    const expr = initializer("const f = <T>(x: T) => x;");
    expect(expr.kind).toBe(SyntaxKind.ArrowFunction);
    expect(expr.typeParameters).toHaveLength(1);
    expect(expr.typeParameters[0].name.text).toBe("T");
  });
});

describe("type-only expressions", () => {
  it("parses satisfies expressions", () => {
    const expr = initializer("const x = 1 satisfies number;");
    expect(expr.kind).toBe(SyntaxKind.SatisfiesExpression);
  });

  it("parses as expressions", () => {
    const expr = initializer("const x = 1 as number;");
    expect(expr.kind).toBe(SyntaxKind.AsExpression);
  });
});

describe("assignment validation", () => {
  it("reports invalid assignment targets", () => {
    const { diagnostics } = parse("1 = 2;");
    expect(diagnostics.some((d) => d.code === DiagnosticCode.InvalidAssignmentTarget)).toBe(true);
  });
});

describe("unsupported syntax", () => {
  it("reports new.target as an unsupported feature, not a syntax error", () => {
    const { diagnostics } = parse("function F() { return new.target; }");
    expect(diagnostics.map((d) => d.code)).toContain(DiagnosticCode.UnsupportedFeature);
    expect(diagnostics.map((d) => d.code)).not.toContain(DiagnosticCode.UnexpectedToken);
  });
});

describe("sequence expressions", () => {
  it("parses a comma expression into one SequenceExpression", () => {
    const expr = initializer("const v = (a, b, c);");
    expect(expr.kind).toBe(SyntaxKind.ParenthesizedExpression);
    const sequence = expr.expression;
    expect(sequence.kind).toBe(SyntaxKind.SequenceExpression);
    expect(sequence.expressions.map((item: any) => item.text)).toEqual(["a", "b", "c"]);
  });

  it("parses comma-separated for-update clauses", () => {
    const { file, diagnostics } = parse("for (let i = 0, j = 1; i < 3; i++, j += 2) {}");
    expect(diagnostics).toHaveLength(0);
    const loop = file.statements[0] as any;
    expect(loop.incrementor.kind).toBe(SyntaxKind.SequenceExpression);
    expect(loop.incrementor.expressions).toHaveLength(2);
  });

  it("keeps declarator commas separate from the comma operator", () => {
    const { file, diagnostics } = parse("let a = 1, b = 2;");
    expect(diagnostics).toHaveLength(0);
    const statement = file.statements[0] as any;
    expect(statement.declarationList.declarations).toHaveLength(2);
  });
});

describe("async function expressions", () => {
  it("parses `async function` as an expression with the async flag", () => {
    const expr = initializer("const f = async function (v) { return v; };");
    expect(expr.kind).toBe(SyntaxKind.FunctionExpression);
    expect(expr.flags).toBe(NodeFlags.Async);
    expect(expr.name).toBeUndefined();
  });

  it("parses named async generator function expressions", () => {
    const expr = initializer("const f = async function* gen() { yield 1; };");
    expect(expr.kind).toBe(SyntaxKind.FunctionExpression);
    expect(expr.flags).toBe(NodeFlags.Async | NodeFlags.Generator);
    expect(expr.name.text).toBe("gen");
  });
});