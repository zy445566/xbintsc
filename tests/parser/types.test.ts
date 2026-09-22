import { describe, expect, it } from "vitest";
import { parse } from "../helpers.js";
import { SyntaxKind } from "../../src/ast/nodes.js";
import { TypeOperator } from "../../src/ast/operators.js";

/* eslint-disable @typescript-eslint/no-explicit-any */
function aliasType(source: string): any {
  const { file, diagnostics } = parse(source);
  expect(diagnostics.filter((d) => d.category === "error")).toHaveLength(0);
  return (file.statements[0] as any).type;
}

describe("parser type syntax", () => {
  it("parses intersection types", () => {
    const type = aliasType("type T = A & B & C;");
    expect(type.kind).toBe(SyntaxKind.IntersectionType);
    expect(type.types).toHaveLength(3);
    expect(type.types.map((t: any) => t.kind)).toEqual([
      SyntaxKind.TypeReference,
      SyntaxKind.TypeReference,
      SyntaxKind.TypeReference,
    ]);
  });

  it("tolerates leading union and intersection bars", () => {
    const union = aliasType("type T = | A | B;");
    expect(union.kind).toBe(SyntaxKind.UnionType);
    expect(union.types).toHaveLength(2);

    const intersection = aliasType("type T = & A & B;");
    expect(intersection.kind).toBe(SyntaxKind.IntersectionType);
    expect(intersection.types).toHaveLength(2);
  });

  it("parses keyof, unique and readonly type operators", () => {
    const keyof = aliasType("type T = keyof Foo;");
    expect(keyof.kind).toBe(SyntaxKind.TypeOperator);
    expect(keyof.operator).toBe(TypeOperator.KeyOf);

    const unique = aliasType("type T = unique symbol;");
    expect(unique.kind).toBe(SyntaxKind.TypeOperator);
    expect(unique.operator).toBe(TypeOperator.Unique);

    const readonly = aliasType("type T = readonly number[];");
    expect(readonly.kind).toBe(SyntaxKind.TypeOperator);
    expect(readonly.operator).toBe(TypeOperator.Readonly);
  });

  it("parses infer types with and without a constraint", () => {
    const plain = aliasType("type T = infer R;");
    expect(plain.kind).toBe(SyntaxKind.InferType);
    expect(plain.typeParameter.name.text).toBe("R");
    expect(plain.typeParameter.constraint).toBeUndefined();

    const constrained = aliasType("type T = infer R extends string;");
    expect(constrained.kind).toBe(SyntaxKind.InferType);
    expect(constrained.typeParameter.constraint.kind).toBe(SyntaxKind.StringKeywordType);
  });

  it("parses every keyword type", () => {
    const cases: [string, SyntaxKind][] = [
      ["bigint", SyntaxKind.BigIntKeywordType],
      ["boolean", SyntaxKind.BooleanKeywordType],
      ["symbol", SyntaxKind.SymbolKeywordType],
      ["undefined", SyntaxKind.UndefinedKeywordType],
      ["null", SyntaxKind.NullKeywordType],
      ["never", SyntaxKind.NeverKeywordType],
      ["object", SyntaxKind.ObjectKeywordType],
      ["this", SyntaxKind.ThisKeywordType],
    ];
    for (const [text, kind] of cases) {
      expect(aliasType(`type T = ${text};`).kind).toBe(kind);
    }
  });

  it("parses typeof queries with and without type arguments", () => {
    const plain = aliasType("type T = typeof foo.bar;");
    expect(plain.kind).toBe(SyntaxKind.TypeQuery);
    expect(plain.exprName.kind).toBe(SyntaxKind.QualifiedName);
    expect(plain.typeArguments).toEqual([]);

    const generic = aliasType("type T = typeof foo<number>;");
    expect(generic.kind).toBe(SyntaxKind.TypeQuery);
    expect(generic.typeArguments).toHaveLength(1);
  });

  it("parses parenthesized and function types", () => {
    expect(aliasType("type T = (number);").kind).toBe(SyntaxKind.ParenthesizedType);
    expect(aliasType("type T = (x: number) => string;").kind).toBe(SyntaxKind.FunctionType);
    expect(aliasType("type T = <U>(x: U) => U;").kind).toBe(SyntaxKind.FunctionType);
    expect(aliasType("type T = new (x: number) => Foo;").kind).toBe(SyntaxKind.FunctionType);
  });

  it("parses literal types including booleans and negatives", () => {
    const trueType = aliasType("type T = true;");
    expect(trueType.kind).toBe(SyntaxKind.LiteralType);
    expect(trueType.literal.kind).toBe(SyntaxKind.TrueKeyword);

    const falseType = aliasType("type T = false;");
    expect(falseType.literal.kind).toBe(SyntaxKind.FalseKeyword);

    const negative = aliasType("type T = -1;");
    expect(negative.kind).toBe(SyntaxKind.LiteralType);
    expect(negative.literal.text).toBe("-1");
    expect(negative.literal.value).toBe(-1);

    expect(aliasType('type T = "x";').literal.value).toBe("x");
    expect(aliasType("type T = 3;").literal.value).toBe(3);
  });

  it("parses import types, degrading them to a loose node", () => {
    const type = aliasType('type T = import("./m").Foo;');
    expect(type.kind).toBe(SyntaxKind.AnyKeywordType);
  });

  it("parses indexed access types", () => {
    const type = aliasType('type T = Foo["bar"];');
    expect(type.kind).toBe(SyntaxKind.IndexedAccessType);
    expect(type.objectType.kind).toBe(SyntaxKind.TypeReference);
    expect(type.indexType.kind).toBe(SyntaxKind.LiteralType);
  });

  it("parses tuple types with rest and optional members", () => {
    const type = aliasType("type T = [number, string?, ...boolean[]];");
    expect(type.kind).toBe(SyntaxKind.TupleType);
    expect(type.elements.map((e: any) => e.kind)).toEqual([
      SyntaxKind.NumberKeywordType,
      SyntaxKind.OptionalType,
      SyntaxKind.RestType,
    ]);
  });

  it("parses mapped types with question modifiers", () => {
    const optional = aliasType("type T = { [K in keyof U]?: U[K] };");
    expect(optional.kind).toBe(SyntaxKind.MappedType);
    expect(optional.questionToken).toBe(true);

    const added = aliasType("type T = { [K in keyof U]+?: U[K] };");
    expect(added.kind).toBe(SyntaxKind.MappedType);
    expect(added.questionToken).toBe("+");

    const removed = aliasType("type T = { [K in keyof U]-?: U[K] };");
    expect(removed.kind).toBe(SyntaxKind.MappedType);
    expect(removed.questionToken).toBe("-");
  });

  it("parses conditional types", () => {
    const type = aliasType("type T = A extends B ? C : D;");
    expect(type.kind).toBe(SyntaxKind.ConditionalType);
    expect(type.checkType.kind).toBe(SyntaxKind.TypeReference);
    expect(type.trueType.kind).toBe(SyntaxKind.TypeReference);
    expect(type.falseType.kind).toBe(SyntaxKind.TypeReference);
  });

  it("parses generic type references and qualified names", () => {
    const generic = aliasType("type T = Map<string, number>;");
    expect(generic.kind).toBe(SyntaxKind.TypeReference);
    expect(generic.typeArguments).toHaveLength(2);

    const qualified = aliasType("type T = A.B.C;");
    expect(qualified.kind).toBe(SyntaxKind.TypeReference);
    expect(qualified.typeName.kind).toBe(SyntaxKind.QualifiedName);
  });

  it("reports invalid type syntax", () => {
    const { diagnostics } = parse("type T = @;");
    expect(diagnostics.some((d) => d.category === "error")).toBe(true);
  });
});
