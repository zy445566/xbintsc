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
  type TaggedTemplateExpression,
  type TemplateLiteral,
  type YieldExpression,
  type ArrayLiteralExpression,
} from "../../../ast/nodes.js";
import { SymbolKind, type SymbolInfo } from "../../../binder/binder.js";
import { DiagnosticCode } from "../../../diagnostics/diagnostic.js";
import { i64, numberLiteral, XT_FALSE, XT_NULL, XT_TRUE, XT_UNDEFINED } from "../../values.js";
import { BUILTIN_FUNCTION_VALUES, CTOR_FUNCTIONS, ERROR_CONSTRUCTORS } from "../tables.js";
import type { Generator } from "../generator.js";

export interface PrimaryExpressionMethods {
  emitExpression(this: Generator, node: Expression): string;
  emitIdentifier(this: Generator, identifier: Identifier): string;
  emitFunctionValue(this: Generator, symbol: SymbolInfo): string;
  emitTemplate(this: Generator, node: TemplateLiteral): string;
  emitTaggedTemplate(this: Generator, node: TaggedTemplateExpression): string;
  emitThis(this: Generator): string;
  emitNew(this: Generator, node: NewExpression): string;
  emitAwait(this: Generator, node: AwaitExpression): string;
  emitYield(this: Generator, node: YieldExpression): string;
}

export const primaryExpressionMethods: PrimaryExpressionMethods = {
  emitExpression(node: Expression): string {
    /* An optional chain must be lowered as a unit so `?.` short-circuits the
       entire chain (`a?.b.c()`), not just the guarded member. */
    if (this.isOptionalChain(node)) return this.emitOptionalChain(node);
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
      case SyntaxKind.TaggedTemplateExpression:
        return this.emitTaggedTemplate(node as TaggedTemplateExpression);
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
      case SyntaxKind.YieldExpression:
        return this.emitYield(node as YieldExpression);
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
        // The providing module was reported as missing (with a `pass --ext`
        // hint) while resolving imports; don't pile on a second error.
        if (this.missingModuleSymbols.has(symbol.id)) return i64(XT_UNDEFINED);
        // Some module bindings are plain values rather than functions
        // (`isMainThread`, `workerData`, ...). They resolve through a nullary
        // runtime getter instead of being called or namespaced.
        const exported = this.importExports.get(symbol.id);
        if (exported?.valueSymbol) {
          this.extraDeclarations.add(`declare i64 @${exported.valueSymbol}(i32, i64*)`);
          const result = this.reg();
          this.emit(`  ${result} = call i64 @${exported.valueSymbol}(i32 0, i64* null)`);
          return result;
        }
        const defaultExport = this.importDefaults.get(symbol.id);
        if (defaultExport?.valueSymbol) {
          this.extraDeclarations.add(`declare i64 @${defaultExport.valueSymbol}(i32, i64*)`);
          const result = this.reg();
          this.emit(`  ${result} = call i64 @${defaultExport.valueSymbol}(i32 0, i64* null)`);
          return result;
        }
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
        const errorCtor = ERROR_CONSTRUCTORS[identifier.text];
        if (errorCtor) {
          // A first-class Error-family constructor (`instanceof TypeError`,
          // `typeof RangeError`, `class X extends Error`, ...).
          const nameValue = this.stringValue(identifier.text);
          return this.runtimeCall("xt_error_constructor", [nameValue]);
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
    this.emitFunctionMetadata(closure, fn);
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

  emitYield(node: YieldExpression): string {
    if (node.delegate) {
      const delegate = this.emitExpression(node.expression as Expression);
      const result = this.reg();
      this.emit(`  ${result} = call i64 @xt_yield_star(i64 ${delegate})`);
      return result;
    }
    const value = node.expression ? this.emitExpression(node.expression) : i64(XT_UNDEFINED);
    const result = this.reg();
    this.emit(`  ${result} = call i64 @xt_yield(i64 ${value})`);
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

  /**
   * Lower a tagged template: build the (cooked) strings array with a `.raw`
   * sibling and call the tag with it followed by the interpolation values.
   * `String.raw` is the one namespace tag special-cased by the runtime.
   */
  emitTaggedTemplate(node: TaggedTemplateExpression): string {
    const template = node.template;
    let cooked: string[];
    let raw: string[];
    let expressions: readonly Expression[];
    if (template.kind === SyntaxKind.NoSubstitutionTemplateLiteral) {
      cooked = [template.value];
      raw = [template.raw ?? template.value];
      expressions = [];
    } else {
      const literal = template as TemplateLiteral;
      cooked = [literal.head, ...literal.spans.map((span) => span.literal)];
      raw = [literal.raw ?? literal.head, ...literal.spans.map((span) => span.raw ?? span.literal)];
      expressions = literal.spans.map((span) => span.expression);
    }

    const makeArray = (items: string[]): string => {
      const array = this.reg();
      this.emit(`  ${array} = call i64 @xt_array_new(i32 0, i64* null)`);
      for (const item of items) {
        const value = this.stringValue(item);
        this.emit(`  call i64 @xt_array_push(i64 ${array}, i64 ${value})`);
      }
      return array;
    };
    const strings = makeArray(cooked);
    const rawArray = makeArray(raw);
    const rawKey = this.stringValue("raw");
    this.emit(`  call i64 @xt_set(i64 ${strings}, i64 ${rawKey}, i64 ${rawArray})`);

    const argc = String(expressions.length + 1);
    const ptr = `%args${this.current.allocas.length}`;
    this.current.allocas.push(`${ptr} = alloca i64, i32 ${expressions.length + 1}`);
    const first = this.reg();
    this.emit(`  ${first} = getelementptr i64, i64* ${ptr}, i32 0`);
    this.emit(`  store i64 ${strings}, i64* ${first}`);
    for (let index = 0; index < expressions.length; index++) {
      const value = this.emitExpression(expressions[index]!);
      const slot = this.reg();
      this.emit(`  ${slot} = getelementptr i64, i64* ${ptr}, i32 ${index + 1}`);
      this.emit(`  store i64 ${value}, i64* ${slot}`);
    }

    const tag = node.tag;
    if (tag.kind === SyntaxKind.PropertyAccessExpression) {
      const access = tag as PropertyAccessExpression;
      const owner = access.expression;
      if (
        owner.kind === SyntaxKind.Identifier &&
        (owner as Identifier).text === "String" &&
        access.name.text === "raw" &&
        !this.binding.symbolOfIdentifier.get(owner as Identifier)
      ) {
        const key = this.stringValue("raw");
        const result = this.reg();
        this.emit(`  ${result} = call i64 @xt_string_static(i64 ${key}, i32 ${argc}, i64* ${ptr})`);
        return result;
      }
      const object = this.emitExpression(owner);
      const name = this.stringValue(access.name.text);
      const result = this.reg();
      this.emit(`  ${result} = call i64 @xt_call_method(i64 ${object}, i64 ${name}, i32 ${argc}, i64* ${ptr})`);
      return result;
    }
    const tagValue = this.emitExpression(tag);
    const result = this.reg();
    this.emit(`  ${result} = call i64 @xt_closure_call(i64 ${tagValue}, i32 ${argc}, i64* ${ptr})`);
    return result;
  },
};
