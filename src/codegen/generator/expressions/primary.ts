/**
 * Primary expression lowering: the dispatcher, identifier/function values,
 * `this`, `new`, `await` and template literals.
 */

import {
  SyntaxKind,
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
  type TemplateLiteral,
  type ArrayLiteralExpression,
} from "../../../ast/nodes.js";
import { SymbolKind, type SymbolInfo } from "../../../binder/binder.js";
import { DiagnosticCode } from "../../../diagnostics/diagnostic.js";
import { i64, numberLiteral, XT_FALSE, XT_NULL, XT_TRUE, XT_UNDEFINED } from "../../values.js";
import { BUILTIN_FUNCTION_VALUES, CTOR_FUNCTIONS } from "../tables.js";
import type { Generator } from "../generator.js";

export interface PrimaryExpressionMethods {
  emitExpression(this: Generator, node: Expression): string;
  emitIdentifier(this: Generator, identifier: Identifier): string;
  emitFunctionValue(this: Generator, symbol: SymbolInfo): string;
  emitTemplate(this: Generator, node: TemplateLiteral): string;
  emitThis(this: Generator): string;
  emitNew(this: Generator, node: NewExpression): string;
  emitAwait(this: Generator, node: AwaitExpression): string;
}

export const primaryExpressionMethods: PrimaryExpressionMethods = {
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
      // An imported constructor (`import { EventEmitter } from "events"`) keeps
      // its import symbol, so resolve it through the module export table.
      if (symbol && symbol.kind === SymbolKind.Import) {
        const exported = this.importExports.get(symbol.id);
        if (exported?.symbol && exported.isConstructor) {
          this.extraDeclarations.add(`declare i64 @${exported.symbol}(i32, i64*)`);
          const args = this.emitArguments(node.arguments);
          const result = this.reg();
          this.emit(`  ${result} = call i64 @${exported.symbol}(i32 ${args.argc}, i64* ${args.ptr})`);
          return result;
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
};
