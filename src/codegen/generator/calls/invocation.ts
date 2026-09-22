/**
 * Call invocation: direct calls, built-in/namespace dispatch, imported
 * bindings, argument marshalling and generic runtime calls.
 */

import {
  SyntaxKind,
  type CallExpression,
  type ElementAccessExpression,
  type Expression,
  type Identifier,
  type PropertyAccessExpression,
  type SpreadElement,
} from "../../../ast/nodes.js";
import { SymbolKind } from "../../../binder/binder.js";
import type { ModuleExport } from "../../../extensions/registry.js";
import { i64, XT_UNDEFINED } from "../../values.js";
import {
  BUILTIN_METHODS,
  CONSOLE_METHODS,
  CTOR_FUNCTIONS,
  GLOBAL_FUNCTIONS,
  MATH_FUNCTIONS,
  NAMESPACE_STATICS,
} from "../tables.js";
import type { Generator } from "../generator.js";

export interface InvocationCallMethods {
  emitCall(this: Generator, node: CallExpression): string;
  tryEmitBuiltinCall(this: Generator, node: CallExpression, callee: PropertyAccessExpression): string | undefined;
  emitModuleExport(this: Generator, node: CallExpression, exported: ModuleExport): string;
  runtimeCall(this: Generator, name: string, args: readonly string[]): string;
  emitArguments(this: Generator, args: readonly Expression[]): { argc: string; ptr: string };
}

export const invocationCallMethods: InvocationCallMethods = {
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
      if (symbol && symbol.kind === SymbolKind.Import) {
        const exported = this.importExports.get(symbol.id);
        if (exported) return this.emitModuleExport(node, exported);
      }
      if (symbol && symbol.kind === SymbolKind.Function) {
        const declaration = symbol.declarations[0];
        const fn = declaration ? this.binding.functionOfNode.get(declaration) : undefined;
        /* Generator functions are not called directly: the call must create a
         * suspended generator, so fall through to the closure-call path. */
        if (fn && !fn.isGenerator) {
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

    // `import * as ns from "path"` (or a default import) aliases a runtime
    // namespace: `ns.join(...)` lowers to the same dispatcher as `path.join`.
    if (targetIdentifier) {
      const namespace = this.namespaceOfSymbol(targetSymbol);
      if (namespace) {
        const dispatcher = NAMESPACE_STATICS[namespace];
        if (dispatcher) {
          const name = this.stringValue(method);
          const args = this.emitArguments(node.arguments);
          const result = this.reg();
          this.emit(`  ${result} = call i64 @${dispatcher}(i64 ${name}, i32 ${args.argc}, i64* ${args.ptr})`);
          return result;
        }
      }
      // Modules with no namespace dispatcher (`fs`, `child_process`, ...): lower
      // `import fs from "node:fs"; fs.readFileSync(...)` through the module's
      // named exports, exactly like a named import of `readFileSync`.
      const moduleExports = this.moduleExportsOfSymbol(targetSymbol);
      if (moduleExports) {
        const exported = moduleExports[method];
        if (exported) return this.emitModuleExport(node, exported);
      }
    }

    void BUILTIN_METHODS;
    return undefined;
  },

  /** Lower a call to an imported module binding to its runtime symbol. */
  emitModuleExport(node: CallExpression, exported: ModuleExport): string {
    const args = this.emitArguments(node.arguments);
    if (exported.symbol) {
      this.extraDeclarations.add(
        exported.returnVoid
          ? `declare void @${exported.symbol}(i32, i64*)`
          : `declare i64 @${exported.symbol}(i32, i64*)`,
      );
      if (exported.returnVoid) {
        this.emit(`  call void @${exported.symbol}(i32 ${args.argc}, i64* ${args.ptr})`);
        return i64(XT_UNDEFINED);
      }
      const result = this.reg();
      this.emit(`  ${result} = call i64 @${exported.symbol}(i32 ${args.argc}, i64* ${args.ptr})`);
      return result;
    }
    if (exported.namespace && exported.method) {
      const dispatcher = NAMESPACE_STATICS[exported.namespace];
      if (dispatcher) {
        const name = this.stringValue(exported.method);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @${dispatcher}(i64 ${name}, i32 ${args.argc}, i64* ${args.ptr})`);
        return result;
      }
    }
    this.unsupported(node, "imported function");
    return i64(XT_UNDEFINED);
  },

  runtimeCall(name: string, args: readonly string[]): string {
    const result = this.reg();
    this.emit(`  ${result} = call i64 @${name}(${args.map((a) => `i64 ${a}`).join(", ")})`);
    return result;
  },

  emitArguments(args: readonly Expression[]): { argc: string; ptr: string } {
    if (args.length === 0) return { argc: "0", ptr: "null" };
    const hasSpread = args.some((element) => element.kind === SyntaxKind.SpreadElement);
    if (!hasSpread) {
      const ptr = `%args${this.current.allocas.length}`;
      this.current.allocas.push(`${ptr} = alloca i64, i32 ${args.length}`);
      for (let index = 0; index < args.length; index++) {
        const value = this.emitExpression(args[index]!);
        const slot = this.reg();
        this.emit(`  ${slot} = getelementptr i64, i64* ${ptr}, i32 ${index}`);
        this.emit(`  store i64 ${value}, i64* ${slot}`);
      }
      return { argc: String(args.length), ptr };
    }
    // At least one `...spread`: accumulate the argument list in a runtime
    // array, which grows to whatever length the spread produces.
    const array = this.reg();
    this.emit(`  ${array} = call i64 @xt_array_new(i32 0, i64* null)`);
    for (const element of args) {
      if (element.kind === SyntaxKind.SpreadElement) {
        const value = this.emitExpression((element as SpreadElement).expression);
        this.emit(`  call i64 @xt_array_spread(i64 ${array}, i64 ${value})`);
      } else {
        const value = this.emitExpression(element);
        this.emit(`  call i64 @xt_array_push(i64 ${array}, i64 ${value})`);
      }
    }
    const argc = this.reg();
    this.emit(`  ${argc} = call i32 @xt_array_size(i64 ${array})`);
    const ptr = this.reg();
    this.emit(`  ${ptr} = call i64* @xt_array_items(i64 ${array})`);
    return { argc, ptr };
  },
};
