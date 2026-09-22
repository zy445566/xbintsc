import { describe, expect, it } from "vitest";
import { forEachChild, getChildren, nodeAtPosition, walk } from "../../src/ast/visitor.js";
import { parse } from "../helpers.js";
import { SyntaxKind, type Node } from "../../src/ast/nodes.js";

/* eslint-disable @typescript-eslint/no-explicit-any */
const asNode = (value: Record<string, unknown>): Node => value as unknown as Node;

describe("ast visitor", () => {
  it("collects direct children while ignoring scalar token fields", () => {
    const leaf = asNode({ kind: SyntaxKind.Identifier, text: "x", value: 1, start: 0, end: 1 });
    const binary = asNode({ kind: SyntaxKind.BinaryExpression, left: leaf, right: undefined, start: 0, end: 1 });
    const block = asNode({ kind: SyntaxKind.Block, statements: [leaf, binary], start: 0, end: 2 });

    expect(getChildren(block)).toEqual([leaf, binary]);
    // Literal scalars (`text`, `value`) are not children.
    expect(getChildren(leaf)).toEqual([]);
  });

  it("skips non-node entries inside child arrays", () => {
    const leaf = asNode({ kind: SyntaxKind.Identifier, text: "x", start: 0, end: 1 });
    const block = asNode({
      kind: SyntaxKind.Block,
      statements: [leaf, null, 3, "text", { notANode: true }],
      start: 0,
      end: 1,
    });
    expect(getChildren(block)).toEqual([leaf]);
  });

  it("visits each direct child once via forEachChild", () => {
    const left = asNode({ kind: SyntaxKind.Identifier, text: "a", start: 0, end: 1 });
    const right = asNode({ kind: SyntaxKind.Identifier, text: "b", start: 4, end: 5 });
    const binary = asNode({ kind: SyntaxKind.BinaryExpression, left, right, start: 0, end: 5 });
    const seen: Node[] = [];
    forEachChild(binary, (child) => seen.push(child));
    expect(seen).toEqual([left, right]);
  });

  it("walks the tree depth-first in pre-order", () => {
    const leaf = asNode({ kind: SyntaxKind.Identifier, text: "x", start: 0, end: 1 });
    const binary = asNode({ kind: SyntaxKind.BinaryExpression, left: leaf, right: leaf, start: 0, end: 1 });
    const block = asNode({ kind: SyntaxKind.Block, statements: [binary], start: 0, end: 1 });
    const kinds: SyntaxKind[] = [];
    walk(block, (node) => kinds.push(node.kind));
    expect(kinds).toEqual([SyntaxKind.Block, SyntaxKind.BinaryExpression, SyntaxKind.Identifier, SyntaxKind.Identifier]);
  });

  it("finds the innermost node containing an offset", () => {
    const { file } = parse("const x = 42;");
    const literal = nodeAtPosition(file, "const x = ".length);
    expect(literal?.kind).toBe(SyntaxKind.NumericLiteral);
    expect((literal as any).value).toBe(42);
  });

  it("returns undefined when no child contains the offset", () => {
    const { file } = parse("const x = 1;");
    expect(nodeAtPosition(file, 10_000)).toBeUndefined();
  });
});
