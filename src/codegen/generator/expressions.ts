/**
 * Expression lowering: operators, assignments, conditionals, templates and
 * identifier resolution.
 */

import {
  SyntaxKind,
  type ArrayLiteralExpression,
  type ArrowFunction,
  type AwaitExpression,
  type BinaryExpression,
  type CallExpression,
  type ClassExpression,
  type ConditionalExpression,
  type DeleteExpression,
  type ElementAccessExpression,
  type Expression,
  type FunctionExpression,
  type Identifier,
  type NewExpression,
  type ObjectLiteralExpression,
  type PostfixUnaryExpression,
  type PrefixUnaryExpression,
  type PropertyAccessExpression,
  type PropertyAssignment,
  type ShorthandPropertyAssignment,
  type SpreadElement,
  type TemplateLiteral,
} from "../../ast/nodes.js";
import {
  AssignmentOperator,
  BinaryOperator,
  PrefixUnaryOperator,
  PostfixUnaryOperator,
} from "../../ast/nodes.js";
import { SymbolKind, type SymbolInfo } from "../../binder/binder.js";
import { DiagnosticCode } from "../../diagnostics/diagnostic.js";
import { i64, numberLiteral, XT_FALSE, XT_NULL, XT_TRUE, XT_UNDEFINED } from "../values.js";
import { BINARY_RUNTIME, compoundToBinary, isAssignmentOperator, BUILTIN_FUNCTION_VALUES, CTOR_FUNCTIONS, propertyNameText } from "./tables.js";
import type { Generator } from "./generator.js";

export interface ExpressionMethods {
  emitExpression(this: Generator, node: Expression): string;
  emitIdentifier(this: Generator, identifier: Identifier): string;
  emitFunctionValue(this: Generator, symbol: SymbolInfo): string;
  emitTemplate(this: Generator, node: TemplateLiteral): string;
  emitBinaryOrAssignment(this: Generator, node: BinaryExpression): string;
  emitShortCircuit(this: Generator, node: BinaryExpression, kind: "and" | "or" | "nullish"): string;
  emitConditional(this: Generator, node: ConditionalExpression): string;
  emitPrefix(this: Generator, node: PrefixUnaryExpression): string;
  emitPostfix(this: Generator, node: PostfixUnaryExpression): string;
  emitAssignment(this: Generator, node: BinaryExpression): string;
  emitLogicalAssignment(this: Generator, node: BinaryExpression): string;
  emitAssignmentTarget(this: Generator, target: Expression, value: string): void;
  emitArrayAssignmentTarget(this: Generator, pattern: ArrayLiteralExpression, value: string): void;
  emitObjectAssignmentTarget(this: Generator, pattern: ObjectLiteralExpression, value: string): void;
  emitThis(this: Generator): string;
  emitNew(this: Generator, node: NewExpression): string;
  emitAwait(this: Generator, node: AwaitExpression): string;
}

/** True for `target = default` (only the plain assignment operator). */
function isPlainAssignment(node: Expression): boolean {
  if (node.kind !== SyntaxKind.BinaryExpression) return false;
  return ((node as BinaryExpression).operator as unknown as AssignmentOperator) === AssignmentOperator.Assign;
}

export const expressionMethods: ExpressionMethods = {
  emitExpression(node: Expression): string {
    switch (node.kind) {
      case SyntaxKind.Identifier:
        return this.emitIdentifier(node as Identifier);
      case SyntaxKind.NumericLiteral: {
        const value = (node as { value: number }).value;
        return numberLiteral(value);
      }
      case SyntaxKind.BigIntLiteral: {
        const entry = this.internString((node as { text: string }).text);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @xt_bigint_from_string(i8* ${entry.label}, i64 ${entry.length})`);
        return result;
      }
      case SyntaxKind.StringLiteral:
        return this.stringValue((node as { value: string }).value);
      case SyntaxKind.NoSubstitutionTemplateLiteral:
        return this.stringValue((node as { value: string }).value);
      case SyntaxKind.RegularExpressionLiteral: {
        const regex = node as { pattern: string; flags: string };
        const pattern = this.stringValue(regex.pattern);
        const flags = this.stringValue(regex.flags);
        const ptr = `%regex${this.current.allocas.length}`;
        this.current.allocas.push(`${ptr} = alloca i64, i32 2`);
        const slot0 = this.reg();
        this.emit(`  ${slot0} = getelementptr i64, i64* ${ptr}, i32 0`);
        this.emit(`  store i64 ${pattern}, i64* ${slot0}`);
        const slot1 = this.reg();
        this.emit(`  ${slot1} = getelementptr i64, i64* ${ptr}, i32 1`);
        this.emit(`  store i64 ${flags}, i64* ${slot1}`);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @xt_regexp_ctor(i32 2, i64* ${ptr})`);
        return result;
      }
      case SyntaxKind.TemplateLiteral:
        return this.emitTemplate(node as TemplateLiteral);
      case SyntaxKind.TrueKeyword:
        return i64(XT_TRUE);
      case SyntaxKind.FalseKeyword:
        return i64(XT_FALSE);
      case SyntaxKind.NullKeyword:
        return i64(XT_NULL);
      case SyntaxKind.UndefinedKeyword:
        return i64(XT_UNDEFINED);
      case SyntaxKind.ParenthesizedExpression:
        return this.emitExpression((node as { expression: Expression }).expression);
      case SyntaxKind.AsExpression:
      case SyntaxKind.SatisfiesExpression:
      case SyntaxKind.NonNullExpression:
        return this.emitExpression((node as { expression: Expression }).expression);
      case SyntaxKind.DeleteExpression:
        return this.emitDelete(node as DeleteExpression);
      case SyntaxKind.TypeOfExpression: {
        const operand = this.emitExpression((node as { expression: Expression }).expression);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @xt_typeof(i64 ${operand})`);
        return result;
      }
      case SyntaxKind.VoidExpression:
        this.emitExpression((node as { expression: Expression }).expression);
        return i64(XT_UNDEFINED);
      case SyntaxKind.ThisKeyword:
        return this.emitThis();
      case SyntaxKind.NewExpression:
        return this.emitNew(node as NewExpression);
      case SyntaxKind.AwaitExpression:
        return this.emitAwait(node as AwaitExpression);
      case SyntaxKind.ClassExpression: {
        const info = this.binding.classOfNode.get(node);
        if (!info) {
          this.unsupported(node, "class expression");
          return i64(XT_UNDEFINED);
        }
        this.emitClassSetup(info);
        return this.emitClassReference(info);
      }
      case SyntaxKind.BinaryExpression:
        return this.emitBinaryOrAssignment(node as BinaryExpression);
      case SyntaxKind.PrefixUnaryExpression:
        return this.emitPrefix(node as PrefixUnaryExpression);
      case SyntaxKind.PostfixUnaryExpression:
        return this.emitPostfix(node as PostfixUnaryExpression);
      case SyntaxKind.ConditionalExpression:
        return this.emitConditional(node as ConditionalExpression);
      case SyntaxKind.CallExpression:
        return this.emitCall(node as CallExpression);
      case SyntaxKind.PropertyAccessExpression:
        return this.emitPropertyAccess(node as PropertyAccessExpression);
      case SyntaxKind.ElementAccessExpression:
        return this.emitElementAccess(node as ElementAccessExpression);
      case SyntaxKind.ArrayLiteralExpression:
        return this.emitArrayLiteral(node as ArrayLiteralExpression);
      case SyntaxKind.ObjectLiteralExpression:
        return this.emitObjectLiteral(node as ObjectLiteralExpression);
      case SyntaxKind.ArrowFunction:
      case SyntaxKind.FunctionExpression:
        return this.emitClosure(node as ArrowFunction | FunctionExpression);
      default:
        this.unsupported(node, "expression");
        return i64(XT_UNDEFINED);
    }
  },

  emitIdentifier(identifier: Identifier): string {
    const symbol = this.binding.symbolOfIdentifier.get(identifier);
    if (symbol) {
      if (symbol.kind === SymbolKind.Class) {
        const declaration = symbol.declarations[0];
        const info = declaration ? this.binding.classOfNode.get(declaration) : undefined;
        if (info) return this.emitClassReference(info);
      }
      if (symbol.kind === SymbolKind.Function && !this.current.slots.has(symbol.id)) {
        return this.emitFunctionValue(symbol);
      }
      if (symbol.kind === SymbolKind.Import) {
        // Imported runtime bindings have no first-class value: they are meant
        // to be called (`readFileSync(...)`) or used as a namespace
        // (`path.join(...)`), both handled before identifier lowering.
        this.diagnostics.error(
          DiagnosticCode.CodegenError,
          `Imported binding '${identifier.text}' cannot be used as a value`,
          identifier,
          this.sourceFile.fileName,
        );
        return i64(XT_UNDEFINED);
      }
      return this.readSlot(symbol);
    }
    switch (identifier.text) {
      case "undefined":
        return i64(XT_UNDEFINED);
      case "NaN":
        return numberLiteral(NaN);
      case "Infinity":
        return numberLiteral(Infinity);
      case "console":
        return i64(XT_UNDEFINED);
      case "arguments": {
        const argc = this.reg();
        this.emit(`  ${argc} = load i32, i32* %saved.argc`);
        const argv = this.reg();
        this.emit(`  ${argv} = load i64*, i64** %saved.argv`);
        const rest = this.reg();
        this.emit(`  ${rest} = call i64 @xt_rest_args(i32 ${argc}, i64* ${argv}, i32 0)`);
        return rest;
      }
      default: {
        const builtinValue = BUILTIN_FUNCTION_VALUES[identifier.text];
        if (builtinValue) {
          // A global like `Boolean` used as a value (`arr.filter(Boolean)`):
          // wrap its runtime trampoline in a closure object.
          const cast = this.reg();
          this.emit(`  ${cast} = bitcast i64 (i64, i64, i32, i64*)* @${builtinValue} to i8*`);
          const closure = this.reg();
          this.emit(`  ${closure} = call i64 @xt_closure_new(i8* ${cast}, i32 0, i64* null)`);
          return closure;
        }
        this.diagnostics.error(
          DiagnosticCode.CannotFindName,
          `Cannot find name '${identifier.text}'`,
          identifier,
          this.sourceFile.fileName,
        );
        return i64(XT_UNDEFINED);
      }
    }
  },

  emitFunctionValue(symbol: SymbolInfo): string {
    const declaration = symbol.declarations[0];
    const fn = declaration ? this.binding.functionOfNode.get(declaration) : undefined;
    if (!fn) return i64(XT_UNDEFINED);
    const cast = this.reg();
    this.emit(`  ${cast} = bitcast i64 (i64, i64, i32, i64*)* @${this.functionName(fn)} to i8*`);
    const closure = this.reg();
    this.emit(`  ${closure} = call i64 @xt_closure_new(i8* ${cast}, i32 0, i64* null)`);
    return closure;
  },

  emitThis(): string {
    if (!this.current.thisPtr) return i64(XT_UNDEFINED);
    const value = this.reg();
    this.emit(`  ${value} = load i64, i64* ${this.current.thisPtr}`);
    return value;
  },

  emitAwait(node: AwaitExpression): string {
    const value = this.emitExpression(node.expression);
    const result = this.reg();
    this.emit(`  ${result} = call i64 @xt_await(i64 ${value})`);
    return result;
  },

  emitNew(node: NewExpression): string {
    const callee = node.expression;
    if (callee.kind === SyntaxKind.Identifier) {
      const identifier = callee as Identifier;
      const symbol = this.binding.symbolOfIdentifier.get(identifier);
      if (symbol && symbol.kind === SymbolKind.Class) {
        const declaration = symbol.declarations[0];
        const info = declaration ? this.binding.classOfNode.get(declaration) : undefined;
        if (info) {
          const ctor = this.emitClassReference(info);
          const args = this.emitArguments(node.arguments);
          const result = this.reg();
          this.emit(`  ${result} = call i64 @xt_new(i64 ${ctor}, i32 ${args.argc}, i64* ${args.ptr})`);
          return result;
        }
      }
      if (!symbol) {
        const ctor = CTOR_FUNCTIONS[identifier.text];
        if (ctor) {
          const args = this.emitArguments(node.arguments);
          const result = this.reg();
          this.emit(`  ${result} = call i64 @${ctor}(i32 ${args.argc}, i64* ${args.ptr})`);
          return result;
        }
        if (identifier.text === "Object") {
          if (node.arguments.length > 0) return this.emitExpression(node.arguments[0]!);
          return this.runtimeCall("xt_object_new", []);
        }
      }
    }
    const ctorValue = this.emitExpression(callee);
    const args = this.emitArguments(node.arguments);
    const result = this.reg();
    this.emit(`  ${result} = call i64 @xt_new(i64 ${ctorValue}, i32 ${args.argc}, i64* ${args.ptr})`);
    return result;
  },

  emitTemplate(node: TemplateLiteral): string {
    let accumulator = this.stringValue(node.head);
    for (const span of node.spans) {
      const expression = this.emitExpression(span.expression);
      const text = this.reg();
      this.emit(`  ${text} = call i64 @xt_to_string(i64 ${expression})`);
      const joined = this.reg();
      this.emit(`  ${joined} = call i64 @xt_add(i64 ${accumulator}, i64 ${text})`);
      accumulator = joined;
      if (span.literal.length > 0) {
        const literal = this.stringValue(span.literal);
        const withLiteral = this.reg();
        this.emit(`  ${withLiteral} = call i64 @xt_add(i64 ${accumulator}, i64 ${literal})`);
        accumulator = withLiteral;
      }
    }
    return accumulator;
  },

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
    const left = this.emitExpression(node.left);
    const right = this.emitExpression(node.right);
    const runtime = BINARY_RUNTIME[operator];
    if (!runtime) {
      this.unsupported(node, `operator '${operator}'`);
      return i64(XT_UNDEFINED);
    }
    const result = this.reg();
    this.emit(`  ${result} = call i64 @${runtime}(i64 ${left}, i64 ${right})`);
    return result;
  },

  emitShortCircuit(node: BinaryExpression, kind: "and" | "or" | "nullish"): string {
    const result = this.alloca();
    const left = this.emitExpression(node.left);
    this.emit(`  store i64 ${left}, i64* ${result}`);
    const rightLabel = this.label("sc.right");
    const endLabel = this.label("sc.end");
    let condition: string;
    if (kind === "and") {
      const truthy = this.reg();
      this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${left})`);
      condition = this.reg();
      this.emit(`  ${condition} = icmp ne i32 ${truthy}, 0`);
    } else if (kind === "or") {
      const truthy = this.reg();
      this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${left})`);
      const isTruthy = this.reg();
      this.emit(`  ${isTruthy} = icmp ne i32 ${truthy}, 0`);
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
    const truthy = this.reg();
    this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${condition})`);
    const nonzero = this.reg();
    this.emit(`  ${nonzero} = icmp ne i32 ${truthy}, 0`);
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
        const next = this.reg();
        this.emit(`  ${next} = call i64 @${fn}(i64 ${current}, i64 ${one})`);
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
    const next = this.reg();
    this.emit(`  ${next} = call i64 @${fn}(i64 ${current}, i64 ${one})`);
    this.emitAssignmentTarget(node.operand, next);
    return current;
  },

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
