/* eslint-disable @typescript-eslint/no-explicit-any */
import { describe, expect, it } from "vitest";
import { parse } from "../helpers.js";
import { SyntaxKind } from "../../src/ast/nodes.js";

function statements(source: string): { stmts: any[]; diagnostics: readonly any[] } {
  const { file, diagnostics } = parse(source);
  return { stmts: file.statements as any[], diagnostics };
}

describe("statement parsing", () => {
  it("parses empty and debugger statements", () => {
    const { stmts, diagnostics } = statements(";\ndebugger;");
    expect(diagnostics).toHaveLength(0);
    expect(stmts[0].kind).toBe(SyntaxKind.EmptyStatement);
    expect(stmts[1].kind).toBe(SyntaxKind.DebuggerStatement);
  });

  it("parses const enums", () => {
    const { stmts, diagnostics } = statements("const enum E { A, B }");
    expect(diagnostics).toHaveLength(0);
    expect(stmts[0].kind).toBe(SyntaxKind.EnumDeclaration);
  });

  it("parses labeled statements", () => {
    const { stmts, diagnostics } = statements("outer: for (;;) { break outer; }");
    expect(diagnostics).toHaveLength(0);
    expect(stmts[0].kind).toBe(SyntaxKind.LabeledStatement);
  });

  it("parses interface, type alias, enum and namespace declarations", () => {
    const cases = [
      "interface I { x: number; }",
      "type T = number | string;",
      "enum E { A, B }",
      "namespace N { export const x = 1; }",
      'module "m" { export const y = 2; }',
    ];
    for (const source of cases) {
      const { diagnostics } = statements(source);
      expect(diagnostics, source).toHaveLength(0);
    }
  });

  it("parses declare modifiers on every declaration kind", () => {
    const cases = [
      "declare const x: number;",
      "declare let y: number;",
      "declare const enum E { A }",
      "declare function f(): void;",
      "declare class C {}",
      "declare interface I {}",
      "declare type T = number;",
      "declare enum E { A }",
      "declare namespace N {}",
      'declare module "m" {}',
    ];
    for (const source of cases) {
      const { diagnostics } = statements(source);
      expect(diagnostics, source).toHaveLength(0);
    }
  });

  it("parses export default function and class declarations", () => {
    expect(statements("export default function f() {}").diagnostics).toHaveLength(0);
    expect(statements("export default class C {}").diagnostics).toHaveLength(0);
  });
});

describe("class member parsing", () => {
  it("skips stray semicolons", () => {
    const { stmts, diagnostics } = statements("class C { ; x = 1; }");
    expect(diagnostics).toHaveLength(0);
    expect(stmts[0].members).toHaveLength(1);
  });

  it("parses member modifiers", () => {
    const source =
      "class C { public x = 1; private y = 2; protected z = 3; readonly w = 4; static s = 5; abstract m(): void; }";
    const { diagnostics } = statements(source);
    expect(diagnostics).toHaveLength(0);
  });

  it("parses optional and definite-assignment properties", () => {
    const { stmts, diagnostics } = statements("class C { a?: number; b!: number; }");
    expect(diagnostics).toHaveLength(0);
    expect(stmts[0].members[0].questionToken).toBe(true);
    expect(stmts[0].members[1].exclamationToken).toBe(true);
  });

  it("parses bodyless accessor, method and constructor signatures", () => {
    const source = "class C { get x(): number; set x(v: number); constructor(); m(): void; }";
    const { diagnostics } = statements(source);
    expect(diagnostics).toHaveLength(0);
  });
});
