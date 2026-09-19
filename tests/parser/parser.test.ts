import { describe, expect, it } from "vitest";
import { parse } from "../helpers.js";
import { SyntaxKind, ModifierKind, type Node } from "../../src/ast/nodes.js";

/* eslint-disable @typescript-eslint/no-explicit-any */
function first(text: string): any {
  const { file, diagnostics } = parse(text);
  expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
  return file.statements[0];
}

function errors(text: string): readonly { message: string }[] {
  return parse(text).diagnostics.filter((d) => d.category === "error");
}

function kindsOf(text: string): SyntaxKind[] {
  return parse(text).file.statements.map((statement: Node) => statement.kind);
}

describe("parser", () => {
  it("parses variable declarations with all modifier kinds", () => {
    const statement = first("const answer = 42;");
    expect(statement.kind).toBe(SyntaxKind.VariableStatement);
    expect(statement.declarationList.declarations[0].name.text).toBe("answer");
    expect(statement.declarationList.declarations[0].initializer.kind).toBe(SyntaxKind.NumericLiteral);
  });

  it("parses function declarations with types and defaults", () => {
    const fn = first("function add(a: number, b: number = 2): number { return a + b; }");
    expect(fn.kind).toBe(SyntaxKind.FunctionDeclaration);
    expect(fn.name.text).toBe("add");
    expect(fn.parameters).toHaveLength(2);
    expect(fn.parameters[0].name.text).toBe("a");
    expect(fn.parameters[1].initializer.kind).toBe(SyntaxKind.NumericLiteral);
    expect(fn.returnType.kind).toBe(SyntaxKind.NumberKeywordType);
    expect(fn.body.kind).toBe(SyntaxKind.Block);
    expect(fn.body.statements[0].kind).toBe(SyntaxKind.ReturnStatement);
  });

  it("parses arrow functions with expression and block bodies", () => {
    const withExpr = first("const f = (x) => x + 1;");
    const arrow = withExpr.declarationList.declarations[0].initializer;
    expect(arrow.kind).toBe(SyntaxKind.ArrowFunction);
    expect(arrow.body.kind).toBe(SyntaxKind.BinaryExpression);

    const withBlock = first("const g = () => { return 1; };");
    expect(withBlock.declarationList.declarations[0].initializer.body.kind).toBe(SyntaxKind.Block);
  });

  it("respects operator precedence and associativity", () => {
    const statement = first("const v = 1 + 2 * 3;");
    const binary = statement.declarationList.declarations[0].initializer;
    expect(binary.kind).toBe(SyntaxKind.BinaryExpression);
    expect(binary.operator).toBe("+");
    expect(binary.right.kind).toBe(SyntaxKind.BinaryExpression);
    expect(binary.right.operator).toBe("*");
  });

  it("parses control flow statements", () => {
    expect(kindsOf("if (a) b(); else c();")).toEqual([SyntaxKind.IfStatement]);
    expect(kindsOf("while (a) b();")).toEqual([SyntaxKind.WhileStatement]);
    expect(kindsOf("do b(); while (a);")).toEqual([SyntaxKind.DoStatement]);
    expect(kindsOf("for (;;) break;")).toEqual([SyntaxKind.ForStatement]);
    expect(kindsOf("for (const x of xs) x;")).toEqual([SyntaxKind.ForOfStatement]);
    expect(kindsOf("for (const k in obj) k;")).toEqual([SyntaxKind.ForInStatement]);
    expect(kindsOf("switch (a) { case 1: break; default: break; }")).toEqual([SyntaxKind.SwitchStatement]);
  });

  it("parses for loops whose condition contains `<` without confusion", () => {
    const statement = first("for (let i = 0; i < 5; i++) { }");
    expect(statement.kind).toBe(SyntaxKind.ForStatement);
    expect(statement.condition.kind).toBe(SyntaxKind.BinaryExpression);
    expect(statement.condition.operator).toBe("<");
  });

  it("parses object and array literals", () => {
    const object = first("const o = { a: 1, b: 2, c };").declarationList.declarations[0].initializer;
    expect(object.kind).toBe(SyntaxKind.ObjectLiteralExpression);
    expect(object.properties.map((p: any) => p.kind)).toEqual([
      SyntaxKind.PropertyAssignment,
      SyntaxKind.PropertyAssignment,
      SyntaxKind.ShorthandPropertyAssignment,
    ]);

    const array = first("const a = [1, 2, ...rest];").declarationList.declarations[0].initializer;
    expect(array.kind).toBe(SyntaxKind.ArrayLiteralExpression);
    expect(array.elements.map((e: any) => e.kind)).toEqual([
      SyntaxKind.NumericLiteral,
      SyntaxKind.NumericLiteral,
      SyntaxKind.SpreadElement,
    ]);
  });

  it("parses member access, calls and new expressions", () => {
    const call = first("console.log(a.b[0]);").expression;
    expect(call.kind).toBe(SyntaxKind.CallExpression);
    expect(call.expression.kind).toBe(SyntaxKind.PropertyAccessExpression);
    expect(call.arguments[0].kind).toBe(SyntaxKind.ElementAccessExpression);

    const constructed = first("new Foo(1);").expression;
    expect(constructed.kind).toBe(SyntaxKind.NewExpression);
    expect(constructed.arguments).toHaveLength(1);
  });

  it("parses template literals with substitutions", () => {
    const statement = first("const s = `a${x}b${y}c`;");
    const template = statement.declarationList.declarations[0].initializer;
    expect(template.kind).toBe(SyntaxKind.TemplateLiteral);
    expect(template.head).toBe("a");
    expect(template.spans).toHaveLength(2);
    expect(template.spans[0].expression.kind).toBe(SyntaxKind.Identifier);
    expect(template.spans[0].literal).toBe("b");
    expect(template.spans[1].literal).toBe("c");
    expect(template.spans[1].isTail).toBe(true);
  });

  it("parses type-only declarations", () => {
    expect(kindsOf("interface Point { x: number; y: number; }")).toEqual([SyntaxKind.InterfaceDeclaration]);
    expect(kindsOf("type Id = string | number;")).toEqual([SyntaxKind.TypeAliasDeclaration]);
    expect(kindsOf("enum Color { Red, Green }")).toEqual([SyntaxKind.EnumDeclaration]);
    expect(kindsOf("class Box { value = 1; get() { return this.value; } }")).toEqual([
      SyntaxKind.ClassDeclaration,
    ]);
  });

  it("parses imports and exports", () => {
    expect(kindsOf("import { a, b } from './m';")).toEqual([SyntaxKind.ImportDeclaration]);
    expect(kindsOf("export function f() {}")).toEqual([SyntaxKind.FunctionDeclaration]);
    expect(kindsOf("export default 1;")).toEqual([SyntaxKind.ExportAssignment]);
  });

  it("parses generic calls without consuming comparison operators", () => {
    const statement = first("const r = identity<number>(1);");
    const call = statement.declarationList.declarations[0].initializer;
    expect(call.kind).toBe(SyntaxKind.CallExpression);
    expect(call.typeArguments).toHaveLength(1);
  });

  it("parses nested generic types", () => {
    const fn = first("function f(m: Map<string, Array<number>>): void {}");
    expect(fn.parameters[0].type.kind).toBe(SyntaxKind.TypeReference);
  });

  it("parses conditional, mapped and function types", () => {
    expect(kindsOf("type A = T extends string ? 1 : 2;")).toEqual([SyntaxKind.TypeAliasDeclaration]);
    expect(kindsOf("type B = { [K in keyof T]: T[K] };")).toEqual([SyntaxKind.TypeAliasDeclaration]);
    expect(kindsOf("type C = (a: number) => string;")).toEqual([SyntaxKind.TypeAliasDeclaration]);
  });

  it("applies automatic semicolon insertion", () => {
    const statements = kindsOf("const a = 1\nconst b = 2\n");
    expect(statements).toEqual([SyntaxKind.VariableStatement, SyntaxKind.VariableStatement]);
  });

  it("recovers from a stray token with a diagnostic", () => {
    expect(errors("const = ;").length).toBeGreaterThan(0);
  });

  it("does not emit phantom diagnostics for valid arrow with as-expression", () => {
    const { diagnostics } = parse("const f = (x: unknown) => (x as number) + 1;");
    expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
  });

  it("parses exported variable declarations", () => {
    expect(kindsOf("export const a = 1;")).toEqual([SyntaxKind.VariableStatement]);
    expect(kindsOf("export let b = 2;")).toEqual([SyntaxKind.VariableStatement]);
    expect(kindsOf("export var c = 3;")).toEqual([SyntaxKind.VariableStatement]);
    const statement = first("export const a = 1;");
    expect(statement.modifiers).toHaveLength(1);
    expect(statement.modifiers[0].modifierKind).toBe(ModifierKind.Export);
  });
});
