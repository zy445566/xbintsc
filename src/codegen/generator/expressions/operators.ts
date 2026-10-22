/**
 * Operator lowering: binary/compound expressions, short-circuiting,
 * conditionals and unary operators.
 */

import {
  BinaryOperator,
  PostfixUnaryOperator,
  PrefixUnaryOperator,
  SyntaxKind,
  type BinaryExpression,
  type ConditionalExpression,
  type Expression,
  type Identifier,
  type PostfixUnaryExpression,
  type PrefixUnaryExpression,
} from "../../../ast/nodes.js";
import { i64, numberLiteral, XT_UNDEFINED } from "../../values.js";
import { BINARY_RUNTIME, BUILTIN_INSTANCEOF, isAssignmentOperator } from "../tables.js";
import type { Generator } from "../generator.js";

/** True for an expression whose value is known to be a string. */
function isStringLike(node: Expression): boolean {
  return node.kind === SyntaxKind.StringLiteral || node.kind === SyntaxKind.NoSubstitutionTemplateLiteral;
}

/** True for a numeric literal (optionally signed), whose tag check can be skipped. */
function isNumericLiteral(node: Expression): boolean {
  if (node.kind === SyntaxKind.NumericLiteral) return true;
  if (node.kind !== SyntaxKind.PrefixUnaryExpression) return false;
  const prefix = node as PrefixUnaryExpression;
  if (prefix.operator !== PrefixUnaryOperator.Minus && prefix.operator !== PrefixUnaryOperator.Plus) return false;
  return prefix.operand.kind === SyntaxKind.NumericLiteral;
}

/**
 * The built-in constructor a right-hand `instanceof` operand names, if any.
 * A user binding with the same name shadows it.
 */
function builtinInstanceofId(generator: Generator, expression: Expression): number | undefined {
  if (expression.kind !== SyntaxKind.Identifier) return undefined;
  const identifier = expression as Identifier;
  if (generator.binding.symbolOfIdentifier.get(identifier)) return undefined;
  return BUILTIN_INSTANCEOF[identifier.text];
}

export interface OperatorExpressionMethods {
  emitBinaryOrAssignment(this: Generator, node: BinaryExpression): string;
  emitShortCircuit(this: Generator, node: BinaryExpression, kind: "and" | "or" | "nullish"): string;
  emitConditional(this: Generator, node: ConditionalExpression): string;
  emitPrefix(this: Generator, node: PrefixUnaryExpression): string;
  emitPostfix(this: Generator, node: PostfixUnaryExpression): string;
}

export const operatorExpressionMethods: OperatorExpressionMethods = {
  emitBinaryOrAssignment(node: BinaryExpression): string {
    const operator = node.operator as string;
    if (isAssignmentOperator(operator)) {
      return this.emitAssignment(node);
    }
    switch (operator) {
      case BinaryOperator.AmpersandAmpersand:
        return this.emitShortCircuit(node, "and");
      case BinaryOperator.BarBar:
        return this.emitShortCircuit(node, "or");
      case BinaryOperator.QuestionQuestion:
        return this.emitShortCircuit(node, "nullish");
      case BinaryOperator.Comma: {
        this.emitExpression(node.left);
        return this.emitExpression(node.right);
      }
      default:
        break;
    }
    // `x instanceof Array` and friends: the built-in constructor is not a
    // first-class value, so compare by the value's runtime kind instead.
    if (operator === BinaryOperator.InstanceOf) {
      const builtin = builtinInstanceofId(this, node.right);
      if (builtin !== undefined) {
        const leftValue = this.emitExpression(node.left);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @xt_instance_of_builtin(i64 ${leftValue}, i32 ${builtin})`);
        return result;
      }
    }
    const left = this.emitExpression(node.left);
    const right = this.emitExpression(node.right);
    const runtime = BINARY_RUNTIME[operator];
    if (!runtime) {
      this.unsupported(node, `operator '${operator}'`);
      return i64(XT_UNDEFINED);
    }
    // `+` concatenates when either side is known to be a string; skip the
    // numeric tag checks for that shape and call the runtime directly.
    const knownStringConcat = operator === "+" && (isStringLike(node.left) || isStringLike(node.right));
    return this.emitBinaryOperation(
      left,
      right,
      operator,
      runtime,
      !knownStringConcat,
      isNumericLiteral(node.left),
      isNumericLiteral(node.right),
    );
  },

  emitShortCircuit(node: BinaryExpression, kind: "and" | "or" | "nullish"): string {
    const result = this.alloca();
    const left = this.emitExpression(node.left);
    this.emit(`  store i64 ${left}, i64* ${result}`);
    const rightLabel = this.label("sc.right");
    const endLabel = this.label("sc.end");
    let condition: string;
    if (kind === "and") {
      condition = this.emitTruthiness(left, node.left);
    } else if (kind === "or") {
      const isTruthy = this.emitTruthiness(left, node.left);
      condition = this.reg();
      this.emit(`  ${condition} = xor i1 ${isTruthy}, true`);
    } else {
      const nullish = this.reg();
      this.emit(`  ${nullish} = call i64 @xt_is_nullish(i64 ${left})`);
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
    return value;
  },

  emitConditional(node: ConditionalExpression): string {
    const result = this.alloca();
    const condition = this.emitExpression(node.condition);
    const nonzero = this.emitTruthiness(condition, node.condition);
    const trueLabel = this.label("cond.true");
    const falseLabel = this.label("cond.false");
    const endLabel = this.label("cond.end");
    this.terminate(`br i1 ${nonzero}, label %${trueLabel}, label %${falseLabel}`);
    this.startBlock(trueLabel);
    const whenTrue = this.emitExpression(node.whenTrue);
    this.emit(`  store i64 ${whenTrue}, i64* ${result}`);
    this.terminate(`br label %${endLabel}`);
    this.startBlock(falseLabel);
    const whenFalse = this.emitExpression(node.whenFalse);
    this.emit(`  store i64 ${whenFalse}, i64* ${result}`);
    this.terminate(`br label %${endLabel}`);
    this.startBlock(endLabel);
    const value = this.reg();
    this.emit(`  ${value} = load i64, i64* ${result}`);
    return value;
  },

  emitPrefix(node: PrefixUnaryExpression): string {
    switch (node.operator) {
      case PrefixUnaryOperator.Minus: {
        const operand = this.emitExpression(node.operand);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @xt_neg(i64 ${operand})`);
        return result;
      }
      case PrefixUnaryOperator.Plus: {
        const operand = this.emitExpression(node.operand);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @xt_pos(i64 ${operand})`);
        return result;
      }
      case PrefixUnaryOperator.Exclamation: {
        const operand = this.emitExpression(node.operand);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @xt_not(i64 ${operand})`);
        return result;
      }
      case PrefixUnaryOperator.Tilde: {
        const operand = this.emitExpression(node.operand);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @xt_bit_not(i64 ${operand})`);
        return result;
      }
      case PrefixUnaryOperator.TypeOf: {
        const operand = this.emitExpression(node.operand);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @xt_typeof(i64 ${operand})`);
        return result;
      }
      case PrefixUnaryOperator.Void:
        if (node.operand) this.emitExpression(node.operand);
        return i64(XT_UNDEFINED);
      case PrefixUnaryOperator.PlusPlus:
      case PrefixUnaryOperator.MinusMinus: {
        const one = numberLiteral(1);
        const current = this.emitExpression(node.operand);
        const fn = node.operator === PrefixUnaryOperator.PlusPlus ? "xt_add" : "xt_sub";
        const next = this.emitBinaryOperation(
          current,
          one,
          node.operator === PrefixUnaryOperator.PlusPlus ? "+" : "-",
          fn,
          true,
          false,
          true,
        );
        this.emitAssignmentTarget(node.operand, next);
        return next;
      }
      default:
        this.unsupported(node, `unary operator '${node.operator}'`);
        return i64(XT_UNDEFINED);
    }
  },

  emitPostfix(node: PostfixUnaryExpression): string {
    const one = numberLiteral(1);
    const current = this.emitExpression(node.operand);
    const fn = node.operator === PostfixUnaryOperator.PlusPlus ? "xt_add" : "xt_sub";
    const next = this.emitBinaryOperation(
      current,
      one,
      node.operator === PostfixUnaryOperator.PlusPlus ? "+" : "-",
      fn,
      true,
      false,
      true,
    );
    this.emitAssignmentTarget(node.operand, next);
    return current;
  },
};
