/**
 * Calls, member access, optional chaining, object/array literals and closures.
 */

import {
  SyntaxKind,
  type ArrowFunction,
  type ArrayLiteralExpression,
  type CallExpression,
  type DeleteExpression,
  type ElementAccessExpression,
  type Expression,
  type FunctionExpression,
  type Identifier,
  type ObjectLiteralExpression,
  type PropertyAccessExpression,
} from "../../ast/nodes.js";
import { SymbolKind, type FunctionInfo } from "../../binder/binder.js";
import { i64, numberLiteral, XT_TRUE, XT_UNDEFINED } from "../values.js";
import {
  BUILTIN_METHODS,
  CONSOLE_METHODS,
  CTOR_FUNCTIONS,
  GLOBAL_FUNCTIONS,
  MATH_CONSTANTS,
  MATH_FUNCTIONS,
  NAMESPACE_PROPERTIES,
  NAMESPACE_STATICS,
  propertyNameText,
} from "./tables.js";
import type { Generator } from "./generator.js";

export interface CallMethods {
  emitCall(this: Generator, node: CallExpression): string;
  tryEmitBuiltinCall(this: Generator, node: CallExpression, callee: PropertyAccessExpression): string | undefined;
  runtimeCall(this: Generator, name: string, args: readonly string[]): string;
  emitArguments(this: Generator, args: readonly Expression[]): { argc: number; ptr: string };
  emitDelete(this: Generator, node: DeleteExpression): string;
  emitPropertyAccess(this: Generator, node: PropertyAccessExpression): string;
  emitElementAccess(this: Generator, node: ElementAccessExpression): string;
  emitOptional(this: Generator, objectValue: string, compute: () => string): string;
  emitArrayLiteral(this: Generator, node: ArrayLiteralExpression): string;
  emitObjectLiteral(this: Generator, node: ObjectLiteralExpression): string;
  emitClosure(this: Generator, node: ArrowFunction | FunctionExpression): string;
  emitClosureValue(this: Generator, fn: FunctionInfo): string;
  emitSuperConstructor(this: Generator, node: CallExpression): string;
  emitSuperCall(this: Generator, node: CallExpression, access: PropertyAccessExpression): string;
}

export const callMethods: CallMethods = {
  emitCall(node: CallExpression): string {
    const callee = node.expression;
    if (node.optional) {
      const calleeValue = this.emitExpression(callee);
      return this.emitOptional(calleeValue, () => {
        const args = this.emitArguments(node.arguments);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @xt_closure_call(i64 ${calleeValue}, i32 ${args.argc}, i64* ${args.ptr})`);
        return result;
      });
    }
    if (
      (callee.kind === SyntaxKind.PropertyAccessExpression || callee.kind === SyntaxKind.ElementAccessExpression) &&
      (callee as PropertyAccessExpression | ElementAccessExpression).optional
    ) {
      // `obj?.method(args)` / `obj?.[key](args)`: guard the receiver, then
      // dispatch through the runtime, which understands object methods as well
      // as the built-in array/string methods.
      const access = callee as PropertyAccessExpression | ElementAccessExpression;
      const object = this.emitExpression(access.expression);
      return this.emitOptional(object, () => {
        const name =
          access.kind === SyntaxKind.PropertyAccessExpression
            ? this.stringValue((access as PropertyAccessExpression).name.text)
            : this.emitExpression((access as ElementAccessExpression).argumentExpression);
        const args = this.emitArguments(node.arguments);
        const result = this.reg();
        this.emit(
          `  ${result} = call i64 @xt_call_method(i64 ${object}, i64 ${name}, i32 ${args.argc}, i64* ${args.ptr})`,
        );
        return result;
      });
    }
    if (callee.kind === SyntaxKind.PropertyAccessExpression) {
      const access = callee as PropertyAccessExpression;
      const special = this.tryEmitBuiltinCall(node, access);
      if (special) return special;
      if (access.expression.kind === SyntaxKind.Identifier && (access.expression as Identifier).text === "super") {
        return this.emitSuperCall(node, access);
      }
      const object = this.emitExpression(access.expression);
      const name = this.stringValue(access.name.text);
      const args = this.emitArguments(node.arguments);
      const result = this.reg();
      this.emit(`  ${result} = call i64 @xt_call_method(i64 ${object}, i64 ${name}, i32 ${args.argc}, i64* ${args.ptr})`);
      return result;
    }
    if (callee.kind === SyntaxKind.ElementAccessExpression) {
      const access = callee as ElementAccessExpression;
      const object = this.emitExpression(access.expression);
      const name = this.emitExpression(access.argumentExpression);
      const args = this.emitArguments(node.arguments);
      const result = this.reg();
      this.emit(`  ${result} = call i64 @xt_call_method(i64 ${object}, i64 ${name}, i32 ${args.argc}, i64* ${args.ptr})`);
      return result;
    }
    if (callee.kind === SyntaxKind.Identifier) {
      if ((callee as Identifier).text === "super") return this.emitSuperConstructor(node);
      const symbol = this.binding.symbolOfIdentifier.get(callee as Identifier);
      if (symbol && symbol.kind === SymbolKind.Function) {
        const declaration = symbol.declarations[0];
        const fn = declaration ? this.binding.functionOfNode.get(declaration) : undefined;
        if (fn) {
          const args = this.emitArguments(node.arguments);
          const result = this.reg();
          this.emit(
            `  ${result} = call i64 @${this.functionName(fn)}(i64 ${i64(XT_UNDEFINED)}, i64 ${i64(XT_UNDEFINED)}, i32 ${args.argc}, i64* ${args.ptr})`,
          );
          return result;
        }
      }
      const globalFunction = !symbol ? GLOBAL_FUNCTIONS[(callee as Identifier).text] : undefined;
      if (globalFunction) {
        const args = this.emitArguments(node.arguments);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @${globalFunction}(i32 ${args.argc}, i64* ${args.ptr})`);
        return result;
      }
      const builtin = this.builtins[(callee as Identifier).text];
      if (!symbol && builtin) {
        this.extraDeclarations.add(
          builtin.returnVoid ? `declare void @${builtin.symbol}(i32, i64*)` : `declare i64 @${builtin.symbol}(i32, i64*)`,
        );
        const args = this.emitArguments(node.arguments);
        if (builtin.returnVoid) {
          this.emit(`  call void @${builtin.symbol}(i32 ${args.argc}, i64* ${args.ptr})`);
          return i64(XT_UNDEFINED);
        }
        const result = this.reg();
        this.emit(`  ${result} = call i64 @${builtin.symbol}(i32 ${args.argc}, i64* ${args.ptr})`);
        return result;
      }
      const ctor = !symbol ? CTOR_FUNCTIONS[(callee as Identifier).text] : undefined;
      if (ctor) {
        const args = this.emitArguments(node.arguments);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @${ctor}(i32 ${args.argc}, i64* ${args.ptr})`);
        return result;
      }
    }
    const calleeValue = this.emitExpression(callee);
    const args = this.emitArguments(node.arguments);
    const result = this.reg();
    this.emit(`  ${result} = call i64 @xt_closure_call(i64 ${calleeValue}, i32 ${args.argc}, i64* ${args.ptr})`);
    return result;
  },

  tryEmitBuiltinCall(node: CallExpression, callee: PropertyAccessExpression): string | undefined {
    const target = callee.expression;
    const method = callee.name.text;
    const targetIdentifier = target.kind === SyntaxKind.Identifier ? (target as Identifier) : undefined;
    const targetSymbol = targetIdentifier ? this.binding.symbolOfIdentifier.get(targetIdentifier) : undefined;

    if (targetIdentifier && targetIdentifier.text === "console" && !targetSymbol) {
      const consoleFn = CONSOLE_METHODS[method];
      if (consoleFn) {
        const args = this.emitArguments(node.arguments);
        this.emit(`  call void @${consoleFn}(i32 ${args.argc}, i64* ${args.ptr})`);
        return i64(XT_UNDEFINED);
      }
      return undefined;
    }

    if (targetIdentifier && targetIdentifier.text === "Math" && !targetSymbol) {
      if (!MATH_FUNCTIONS.has(method)) return undefined;
      const name = this.stringValue(method);
      const args = this.emitArguments(node.arguments);
      const result = this.reg();
      this.emit(`  ${result} = call i64 @xt_math_call(i64 ${name}, i32 ${args.argc}, i64* ${args.ptr})`);
      return result;
    }

    if (targetIdentifier && targetIdentifier.text === "JSON" && !targetSymbol) {
      const fn = method === "parse" ? "xt_json_parse" : method === "stringify" ? "xt_json_stringify" : undefined;
      if (!fn) return undefined;
      const args = this.emitArguments(node.arguments);
      const result = this.reg();
      this.emit(`  ${result} = call i64 @${fn}(i32 ${args.argc}, i64* ${args.ptr})`);
      return result;
    }

    if (targetIdentifier && !targetSymbol) {
      const dispatcher = NAMESPACE_STATICS[targetIdentifier.text];
      if (dispatcher && targetIdentifier.text !== "Math" && targetIdentifier.text !== "JSON") {
        const name = this.stringValue(method);
        const args = this.emitArguments(node.arguments);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @${dispatcher}(i64 ${name}, i32 ${args.argc}, i64* ${args.ptr})`);
        return result;
      }
    }

    void BUILTIN_METHODS;
    return undefined;
  },

  runtimeCall(name: string, args: readonly string[]): string {
    const result = this.reg();
    this.emit(`  ${result} = call i64 @${name}(${args.map((a) => `i64 ${a}`).join(", ")})`);
    return result;
  },

  emitArguments(args: readonly Expression[]): { argc: number; ptr: string } {
    if (args.length === 0) return { argc: 0, ptr: "null" };
    const ptr = `%args${this.current.allocas.length}`;
    this.current.allocas.push(`${ptr} = alloca i64, i32 ${args.length}`);
    for (let index = 0; index < args.length; index++) {
      const value = this.emitExpression(args[index]!);
      const slot = this.reg();
      this.emit(`  ${slot} = getelementptr i64, i64* ${ptr}, i32 ${index}`);
      this.emit(`  store i64 ${value}, i64* ${slot}`);
    }
    return { argc: args.length, ptr };
  },

  emitDelete(node: DeleteExpression): string {
    const target = node.expression;
    if (target.kind === SyntaxKind.PropertyAccessExpression) {
      const access = target as PropertyAccessExpression;
      const object = this.emitExpression(access.expression);
      const key = this.stringValue(access.name.text);
      return this.runtimeCall("xt_delete", [object, key]);
    }
    if (target.kind === SyntaxKind.ElementAccessExpression) {
      const access = target as ElementAccessExpression;
      const object = this.emitExpression(access.expression);
      const key = this.emitExpression(access.argumentExpression);
      return this.runtimeCall("xt_delete", [object, key]);
    }
    return i64(XT_TRUE);
  },

  emitPropertyAccess(node: PropertyAccessExpression): string {
    if (node.expression.kind === SyntaxKind.Identifier && (node.expression as Identifier).text === "super") {
      const thisValue = this.emitThis();
      const proto = this.reg();
      this.emit(`  ${proto} = call i64 @xt_object_get_prototype(i64 ${thisValue})`);
      const parentProto = this.reg();
      this.emit(`  ${parentProto} = call i64 @xt_object_get_prototype(i64 ${proto})`);
      const key = this.stringValue(node.name.text);
      const value = this.reg();
      this.emit(`  ${value} = call i64 @xt_get(i64 ${parentProto}, i64 ${key})`);
      return value;
    }
    if (
      node.name.text in MATH_CONSTANTS &&
      node.expression.kind === SyntaxKind.Identifier &&
      (node.expression as Identifier).text === "Math" &&
      !this.binding.symbolOfIdentifier.get(node.expression as Identifier)
    ) {
      return numberLiteral(MATH_CONSTANTS[node.name.text]!);
    }
    if (
      node.expression.kind === SyntaxKind.Identifier &&
      !this.binding.symbolOfIdentifier.get(node.expression as Identifier)
    ) {
      const getter = NAMESPACE_PROPERTIES[(node.expression as Identifier).text];
      if (getter) {
        const key = this.stringValue(node.name.text);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @${getter}(i64 ${key})`);
        return result;
      }
    }
    const object = this.emitExpression(node.expression);
    const access = (): string => {
      if (node.name.text === "length") {
        const result = this.reg();
        this.emit(`  ${result} = call i64 @xt_array_length(i64 ${object})`);
        return result;
      }
      const key = this.stringValue(node.name.text);
      const result = this.reg();
      this.emit(`  ${result} = call i64 @xt_get(i64 ${object}, i64 ${key})`);
      return result;
    };
    return node.optional ? this.emitOptional(object, access) : access();
  },

  emitElementAccess(node: ElementAccessExpression): string {
    const object = this.emitExpression(node.expression);
    const access = (): string => {
      const key = this.emitExpression(node.argumentExpression);
      const result = this.reg();
      this.emit(`  ${result} = call i64 @xt_get(i64 ${object}, i64 ${key})`);
      return result;
    };
    return node.optional ? this.emitOptional(object, access) : access();
  },

  /**
   * Evaluate `object` once and, when it is neither `null` nor `undefined`, run
   * `compute` to produce the value; otherwise short-circuit to `undefined`.
   */
  emitOptional(objectValue: string, compute: () => string): string {
    const result = this.alloca();
    this.emit(`  store i64 ${i64(XT_UNDEFINED)}, i64* ${result}`);
    const nullish = this.reg();
    this.emit(`  ${nullish} = call i64 @xt_is_nullish(i64 ${objectValue})`);
    const truthy = this.reg();
    this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${nullish})`);
    const condition = this.reg();
    this.emit(`  ${condition} = icmp ne i32 ${truthy}, 0`);
    const someLabel = this.label("opt.some");
    const endLabel = this.label("opt.end");
    this.terminate(`br i1 ${condition}, label %${endLabel}, label %${someLabel}`);
    this.startBlock(someLabel);
    const value = compute();
    this.emit(`  store i64 ${value}, i64* ${result}`);
    this.terminate(`br label %${endLabel}`);
    this.startBlock(endLabel);
    const merged = this.reg();
    this.emit(`  ${merged} = load i64, i64* ${result}`);
    return merged;
  },

  emitArrayLiteral(node: ArrayLiteralExpression): string {
    const hasSpread = node.elements.some((element) => element.kind === SyntaxKind.SpreadElement);
    if (!hasSpread) {
      const args = this.emitArguments(node.elements);
      const result = this.reg();
      this.emit(`  ${result} = call i64 @xt_array_new(i32 ${args.argc}, i64* ${args.ptr})`);
      return result;
    }
    let array = this.runtimeCall("xt_array_new", [`0`, `null`]);
    for (const element of node.elements) {
      if (element.kind === SyntaxKind.SpreadElement) {
        const spread = this.emitExpression((element as { expression: Expression }).expression);
        array = this.runtimeCall("xt_array_spread", [array, spread]);
      } else {
        const value = this.emitExpression(element);
        array = this.runtimeCall("xt_array_push", [array, value]);
      }
    }
    return array;
  },

  emitObjectLiteral(node: ObjectLiteralExpression): string {
    const object = this.reg();
    this.emit(`  ${object} = call i64 @xt_object_new()`);
    for (const property of node.properties) {
      if (property.kind === SyntaxKind.PropertyAssignment) {
        const name = propertyNameText(property.name);
        const key = this.stringValue(name);
        const value = this.emitExpression(property.initializer);
        this.emit(`  call i64 @xt_set(i64 ${object}, i64 ${key}, i64 ${value})`);
      } else if (property.kind === SyntaxKind.ShorthandPropertyAssignment) {
        const identifier = property.name;
        const key = this.stringValue(identifier.text);
        const value = this.emitIdentifier(identifier);
        this.emit(`  call i64 @xt_set(i64 ${object}, i64 ${key}, i64 ${value})`);
      } else if (property.kind === SyntaxKind.SpreadElement) {
        const spread = this.emitExpression((property as { expression: Expression }).expression);
        this.emit(`  call i64 @xt_object_spread(i64 ${object}, i64 ${spread})`);
      } else {
        this.unsupported(property, "object spread");
      }
    }
    return object;
  },

  emitClosure(node: ArrowFunction | FunctionExpression): string {
    const fn = this.binding.functionOfNode.get(node);
    if (!fn) {
      this.unsupported(node, "closure");
      return i64(XT_UNDEFINED);
    }
    return this.emitClosureValue(fn);
  },

  emitClosureValue(fn: FunctionInfo): string {
    const count = fn.captures.length + (fn.capturesThis ? 1 : 0);
    let envPtr = "null";
    if (count > 0) {
      envPtr = `%env${this.current.allocas.length}`;
      this.current.allocas.push(`${envPtr} = alloca i64, i32 ${count}`);
      for (let index = 0; index < fn.captures.length; index++) {
        const symbol = fn.captures[index]!;
        const slot = this.current.slots.get(symbol.id);
        let raw = i64(XT_UNDEFINED);
        if (slot) {
          raw = this.reg();
          this.emit(`  ${raw} = load i64, i64* ${slot.ptr}`);
        }
        const target = this.reg();
        this.emit(`  ${target} = getelementptr i64, i64* ${envPtr}, i32 ${index}`);
        this.emit(`  store i64 ${raw}, i64* ${target}`);
      }
      if (fn.capturesThis) {
        let thisValue = i64(XT_UNDEFINED);
        if (this.current.thisPtr) {
          thisValue = this.reg();
          this.emit(`  ${thisValue} = load i64, i64* ${this.current.thisPtr}`);
        }
        const target = this.reg();
        this.emit(`  ${target} = getelementptr i64, i64* ${envPtr}, i32 ${fn.captures.length}`);
        this.emit(`  store i64 ${thisValue}, i64* ${target}`);
      }
    }
    const cast = this.reg();
    this.emit(`  ${cast} = bitcast i64 (i64, i64, i32, i64*)* @${this.functionName(fn)} to i8*`);
    const closure = this.reg();
    this.emit(`  ${closure} = call i64 @xt_closure_new(i8* ${cast}, i32 ${count}, i64* ${envPtr})`);
    return closure;
  },

  emitSuperConstructor(node: CallExpression): string {
    const thisValue = this.emitThis();
    const proto = this.reg();
    this.emit(`  ${proto} = call i64 @xt_object_get_prototype(i64 ${thisValue})`);
    const parentProto = this.reg();
    this.emit(`  ${parentProto} = call i64 @xt_object_get_prototype(i64 ${proto})`);
    const key = this.stringValue("__ctor");
    const ctor = this.reg();
    this.emit(`  ${ctor} = call i64 @xt_get(i64 ${parentProto}, i64 ${key})`);
    const args = this.emitArguments(node.arguments);
    const result = this.reg();
    this.emit(`  ${result} = call i64 @xt_call_with_this(i64 ${ctor}, i64 ${thisValue}, i32 ${args.argc}, i64* ${args.ptr})`);
    return result;
  },

  emitSuperCall(node: CallExpression, access: PropertyAccessExpression): string {
    const thisValue = this.emitThis();
    const proto = this.reg();
    this.emit(`  ${proto} = call i64 @xt_object_get_prototype(i64 ${thisValue})`);
    const parentProto = this.reg();
    this.emit(`  ${parentProto} = call i64 @xt_object_get_prototype(i64 ${proto})`);
    const key = this.stringValue(access.name.text);
    const method = this.reg();
    this.emit(`  ${method} = call i64 @xt_get(i64 ${parentProto}, i64 ${key})`);
    const args = this.emitArguments(node.arguments);
    const result = this.reg();
    this.emit(`  ${result} = call i64 @xt_call_with_this(i64 ${method}, i64 ${thisValue}, i32 ${args.argc}, i64* ${args.ptr})`);
    return result;
  },
};
