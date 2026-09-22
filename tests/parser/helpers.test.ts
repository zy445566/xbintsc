import { describe, expect, it } from "vitest";
import {
  expressionToPropertyName,
  isAssignmentTarget,
  propertyNameText,
} from "../../src/parser/helpers.js";
import { SyntaxKind, type Expression, type PropertyName } from "../../src/ast/nodes.js";

/* eslint-disable @typescript-eslint/no-explicit-any */
const expr = (kind: SyntaxKind, extra: Record<string, unknown> = {}): Expression =>
  ({ kind, ...extra }) as unknown as Expression;
const name = (kind: SyntaxKind, extra: Record<string, unknown> = {}): PropertyName =>
  ({ kind, ...extra }) as unknown as PropertyName;

describe("parser helpers", () => {
  describe("expressionToPropertyName", () => {
    it("accepts string, numeric and identifier expressions", () => {
      const string = expr(SyntaxKind.StringLiteral, { value: "a" });
      const numeric = expr(SyntaxKind.NumericLiteral, { text: "1" });
      const identifier = expr(SyntaxKind.Identifier, { text: "x" });
      expect(expressionToPropertyName(string)).toBe(string);
      expect(expressionToPropertyName(numeric)).toBe(numeric);
      expect(expressionToPropertyName(identifier)).toBe(identifier);
    });

    it("unwraps a property access to its name", () => {
      const inner = name(SyntaxKind.Identifier, { text: "b" });
      const access = expr(SyntaxKind.PropertyAccessExpression, { name: inner });
      expect(expressionToPropertyName(access)).toBe(inner);
    });

    it("returns undefined for anything that is not a property name", () => {
      expect(expressionToPropertyName(expr(SyntaxKind.CallExpression))).toBeUndefined();
      expect(expressionToPropertyName(expr(SyntaxKind.BinaryExpression))).toBeUndefined();
      expect(expressionToPropertyName(expr(SyntaxKind.ObjectLiteralExpression))).toBeUndefined();
    });
  });

  describe("propertyNameText", () => {
    it("reads identifiers and private identifiers", () => {
      expect(propertyNameText(name(SyntaxKind.Identifier, { text: "value" }))).toBe("value");
      expect(propertyNameText(name(SyntaxKind.PrivateIdentifier, { text: "#secret" }))).toBe("#secret");
    });

    it("reads string literal values and numeric literal text", () => {
      expect(propertyNameText(name(SyntaxKind.StringLiteral, { value: "key" }))).toBe("key");
      expect(propertyNameText(name(SyntaxKind.NumericLiteral, { text: "0x10" }))).toBe("0x10");
    });

    it("falls back to an empty string for computed names", () => {
      expect(propertyNameText(name(SyntaxKind.ComputedPropertyName))).toBe("");
    });
  });

  describe("isAssignmentTarget", () => {
    it("accepts identifiers and access expressions", () => {
      expect(isAssignmentTarget(expr(SyntaxKind.Identifier))).toBe(true);
      expect(isAssignmentTarget(expr(SyntaxKind.PropertyAccessExpression))).toBe(true);
      expect(isAssignmentTarget(expr(SyntaxKind.ElementAccessExpression))).toBe(true);
    });

    it("accepts parenthesized expressions and destructuring patterns", () => {
      expect(isAssignmentTarget(expr(SyntaxKind.ParenthesizedExpression))).toBe(true);
      expect(isAssignmentTarget(expr(SyntaxKind.ArrayLiteralExpression))).toBe(true);
      expect(isAssignmentTarget(expr(SyntaxKind.ObjectLiteralExpression))).toBe(true);
    });

    it("rejects values that cannot be assigned to", () => {
      expect(isAssignmentTarget(expr(SyntaxKind.CallExpression))).toBe(false);
      expect(isAssignmentTarget(expr(SyntaxKind.NumericLiteral))).toBe(false);
      expect(isAssignmentTarget(expr(SyntaxKind.BinaryExpression))).toBe(false);
    });
  });
});
