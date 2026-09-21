/**
 * Assignment lowering: compound/logical assignment and destructuring targets.
 */

import {
  SyntaxKind,
  AssignmentOperator,
  type ArrayLiteralExpression,
  type BinaryExpression,
  type ElementAccessExpression,
  type Expression,
  type Identifier,
  type ObjectLiteralExpression,
  type PropertyAccessExpression,
  type PropertyAssignment,
  type ShorthandPropertyAssignment,
  type SpreadElement,
} from "../../../ast/nodes.js";
import { SymbolKind } from "../../../binder/binder.js";
import { DiagnosticCode } from "../../../diagnostics/diagnostic.js";
import { i64, numberLiteral, XT_UNDEFINED } from "../../values.js";
import { BINARY_RUNTIME, compoundToBinary, propertyNameText } from "../tables.js";
import type { Generator } from "../generator.js";

export interface AssignmentExpressionMethods {
  emitAssignment(this: Generator, node: BinaryExpression): string;
  emitLogicalAssignment(this: Generator, node: BinaryExpression): string;
  emitAssignmentTarget(this: Generator, target: Expression, value: string): void;
  emitArrayAssignmentTarget(this: Generator, pattern: ArrayLiteralExpression, value: string): void;
  emitObjectAssignmentTarget(this: Generator, pattern: ObjectLiteralExpression, value: string): void;
}

/** True for `target = default` (only the plain assignment operator). */
function isPlainAssignment(node: Expression): boolean {
  if (node.kind !== SyntaxKind.BinaryExpression) return false;
  return ((node as BinaryExpression).operator as unknown as AssignmentOperator) === AssignmentOperator.Assign;
}

export const assignmentExpressionMethods: AssignmentExpressionMethods = {
  emitAssignment(node: BinaryExpression): string {
    const operator = node.operator as unknown as AssignmentOperator;
    const target = node.left;
    let value: string;
    if (operator === AssignmentOperator.Assign) {
      value = this.emitExpression(node.right);
    } else if (
      operator === AssignmentOperator.AmpersandAmpersandAssign ||
      operator === AssignmentOperator.BarBarAssign ||
      operator === AssignmentOperator.QuestionQuestionAssign
    ) {
      value = this.emitLogicalAssignment(node);
    } else {
      const current = this.emitExpression(target);
      const right = this.emitExpression(node.right);
      const binaryOperator = compoundToBinary(operator);
      const runtime = BINARY_RUNTIME[binaryOperator];
      if (!runtime) {
        this.unsupported(node, `assignment operator '${operator}'`);
        return i64(XT_UNDEFINED);
      }
      const result = this.reg();
      this.emit(`  ${result} = call i64 @${runtime}(i64 ${current}, i64 ${right})`);
      value = result;
    }
    this.emitAssignmentTarget(target, value);
    return value;
  },

  emitLogicalAssignment(node: BinaryExpression): string {
    const operator = node.operator as unknown as AssignmentOperator;
    const target = node.left;
    const current = this.emitExpression(target);
    const result = this.alloca();
    this.emit(`  store i64 ${current}, i64* ${result}`);
    const rightLabel = this.label("assign.right");
    const endLabel = this.label("assign.end");
    let condition: string;
    if (operator === AssignmentOperator.AmpersandAmpersandAssign) {
      const truthy = this.reg();
      this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${current})`);
      condition = this.reg();
      this.emit(`  ${condition} = icmp ne i32 ${truthy}, 0`);
    } else if (operator === AssignmentOperator.BarBarAssign) {
      const truthy = this.reg();
      this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${current})`);
      const isTruthy = this.reg();
      this.emit(`  ${isTruthy} = icmp ne i32 ${truthy}, 0`);
      condition = this.reg();
      this.emit(`  ${condition} = xor i1 ${isTruthy}, true`);
    } else {
      const nullish = this.reg();
      this.emit(`  ${nullish} = call i64 @xt_is_nullish(i64 ${current})`);
      const truthy = this.reg();
      this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${nullish})`);
      condition = this.reg();
      this.emit(`  ${condition} = icmp ne i32 ${truthy}, 0`);
    }
    this.terminate(`br i1 ${condition}, label %${rightLabel}, label %${endLabel}`);
    this.startBlock(rightLabel);
    const right = this.emitExpression(node.right);
    this.emit(`  store i64 ${right}, i64* ${result}`);
    this.terminate(`br label %${endLabel}`);
    this.startBlock(endLabel);
    const value = this.reg();
    this.emit(`  ${value} = load i64, i64* ${result}`);
    this.emitAssignmentTarget(target, value);
    return value;
  },

  emitAssignmentTarget(target: Expression, value: string): void {
    switch (target.kind) {
      case SyntaxKind.Identifier: {
        const identifier = target as Identifier;
        const symbol = this.binding.symbolOfIdentifier.get(identifier);
        if (!symbol) return;
        if (symbol.kind === SymbolKind.Const || symbol.kind === SymbolKind.Import) {
          this.diagnostics.error(
            DiagnosticCode.CannotAssignToConst,
            symbol.kind === SymbolKind.Import
              ? `Cannot assign to '${symbol.name}' because it is an import.`
              : `Cannot assign to '${symbol.name}' because it is a constant.`,
            identifier,
            this.sourceFile.fileName,
          );
          return;
        }
        this.writeSlot(symbol, value);
        return;
      }
      case SyntaxKind.PropertyAccessExpression: {
        const access = target as PropertyAccessExpression;
        const object = this.emitExpression(access.expression);
        const key = this.stringValue(access.name.text);
        this.emit(`  call i64 @xt_set(i64 ${object}, i64 ${key}, i64 ${value})`);
        return;
      }
      case SyntaxKind.ElementAccessExpression: {
        const access = target as ElementAccessExpression;
        const object = this.emitExpression(access.expression);
        const key = this.emitExpression(access.argumentExpression);
        this.emit(`  call i64 @xt_set(i64 ${object}, i64 ${key}, i64 ${value})`);
        return;
      }
      case SyntaxKind.ParenthesizedExpression:
        this.emitAssignmentTarget((target as { expression: Expression }).expression, value);
        return;
      case SyntaxKind.ArrayLiteralExpression:
        this.emitArrayAssignmentTarget(target as ArrayLiteralExpression, value);
        return;
      case SyntaxKind.ObjectLiteralExpression:
        this.emitObjectAssignmentTarget(target as ObjectLiteralExpression, value);
        return;
      default:
        this.unsupported(target, "assignment target");
    }
  },

  /** `[a, b] = value` / `for ([a, b] of xs)`: assign each element of `value`. */
  emitArrayAssignmentTarget(pattern: ArrayLiteralExpression, value: string): void {
    for (let index = 0; index < pattern.elements.length; index++) {
      const element = pattern.elements[index];
      if (!element) continue;
      if (element.kind === SyntaxKind.UndefinedKeyword) continue; // elision hole
      if (element.kind === SyntaxKind.SpreadElement) {
        const spread = element as SpreadElement;
        const startPtr = this.alloca();
        this.emit(`  store i64 ${numberLiteral(index)}, i64* ${startPtr}`);
        const sliceName = this.stringValue("slice");
        const rest = this.reg();
        this.emit(`  ${rest} = call i64 @xt_call_method(i64 ${value}, i64 ${sliceName}, i32 1, i64* ${startPtr})`);
        this.emitAssignmentTarget(spread.expression, rest);
        continue;
      }
      let target: Expression = element;
      let fallback: Expression | undefined;
      if (isPlainAssignment(element)) {
        const binary = element as BinaryExpression;
        target = binary.left;
        fallback = binary.right;
      }
      let elementValue = this.reg();
      this.emit(`  ${elementValue} = call i64 @xt_get(i64 ${value}, i64 ${numberLiteral(index)})`);
      if (fallback) elementValue = this.emitBindingDefault(elementValue, fallback);
      this.emitAssignmentTarget(target, elementValue);
    }
  },

  /** `{ a, b } = value` / `for ({ a } of xs)`: assign each property of `value`. */
  emitObjectAssignmentTarget(pattern: ObjectLiteralExpression, value: string): void {
    for (const property of pattern.properties) {
      if (property.kind === SyntaxKind.SpreadElement) {
        this.unsupported(property, "object rest destructuring");
        continue;
      }
      if (property.kind === SyntaxKind.PropertyAssignment) {
        const assignment = property as PropertyAssignment;
        let key: string;
        if (assignment.name.kind === SyntaxKind.ComputedPropertyName) {
          const keyValue = this.emitExpression((assignment.name as { expression: Expression }).expression);
          key = this.reg();
          this.emit(`  ${key} = call i64 @xt_to_string(i64 ${keyValue})`);
        } else {
          key = this.stringValue(propertyNameText(assignment.name));
        }
        let target: Expression = assignment.initializer;
        let fallback: Expression | undefined;
        if (isPlainAssignment(target)) {
          const binary = target as BinaryExpression;
          target = binary.left;
          fallback = binary.right;
        }
        let elementValue = this.reg();
        this.emit(`  ${elementValue} = call i64 @xt_get(i64 ${value}, i64 ${key})`);
        if (fallback) elementValue = this.emitBindingDefault(elementValue, fallback);
        this.emitAssignmentTarget(target, elementValue);
        continue;
      }
      const shorthand = property as ShorthandPropertyAssignment;
      const key = this.stringValue(shorthand.name.text);
      let elementValue = this.reg();
      this.emit(`  ${elementValue} = call i64 @xt_get(i64 ${value}, i64 ${key})`);
      if (shorthand.initializer) elementValue = this.emitBindingDefault(elementValue, shorthand.initializer);
      this.emitAssignmentTarget(shorthand.name, elementValue);
    }
  },
};
