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
