import { describe, expect, it } from "vitest";
import { isIdentifier, node, range, span } from "../../src/ast/factory.js";
import { SyntaxKind, type Node } from "../../src/ast/nodes.js";


const asNode = (value: Record<string, unknown>): Node => value as unknown as Node;

describe("ast factory", () => {
  it("returns the node it is given unchanged", () => {
    const identifier = asNode({ kind: SyntaxKind.Identifier, text: "x", start: 0, end: 1 });
    expect(node(identifier)).toBe(identifier);
  });

  it("builds a plain range", () => {
    expect(range(2, 5)).toEqual({ start: 2, end: 5 });
  });

  it("concatenates node ranges into a single span", () => {
    const first = asNode({ kind: SyntaxKind.Identifier, text: "a", start: 5, end: 6 });
    const second = asNode({ kind: SyntaxKind.Identifier, text: "b", start: 20, end: 25 });
    expect(span(first, second)).toEqual({ start: 5, end: 25 });
    expect(span(second, first)).toEqual({ start: 5, end: 25 });
  });

  it("accepts ranges and raw numbers and ignores undefined", () => {
    const child = asNode({ kind: SyntaxKind.Identifier, text: "a", start: 5, end: 6 });
    expect(span(3, { start: 1, end: 2 })).toEqual({ start: 1, end: 3 });
    expect(span(undefined, child)).toEqual({ start: 5, end: 6 });
    expect(span(child, undefined, 100)).toEqual({ start: 5, end: 100 });
  });

  it("returns an empty range when nothing measurable is supplied", () => {
    expect(span()).toEqual({ start: 0, end: 0 });
    expect(span(undefined)).toEqual({ start: 0, end: 0 });
  });

  it("detects identifier nodes", () => {
    expect(isIdentifier(asNode({ kind: SyntaxKind.Identifier }))).toBe(true);
    expect(isIdentifier(asNode({ kind: SyntaxKind.NumericLiteral }))).toBe(false);
    expect(isIdentifier(asNode({ kind: SyntaxKind.PropertyAccessExpression }))).toBe(false);
  });
});
