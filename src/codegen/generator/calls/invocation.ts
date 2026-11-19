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
import { implementationFunctionInfo, SymbolKind } from "../../../binder/binder.js";
import type { ModuleExport } from "../../../extensions/registry.js";
import { i64, XT_UNDEFINED } from "../../values.js";
import {
  BUILTIN_METHODS,
  CONSOLE_METHODS,
  CTOR_FUNCTIONS,
  ERROR_CONSTRUCTORS,
  GLOBAL_FUNCTIONS,
  MATH_FUNCTIONS,
  NAMESPACE_STATICS,
} from "../tables.js";
import { isSuperKeyword } from "./super.js";
import { globalObjectReceiver } from "./unwrap.js";
import type { Generator } from "../generator.js";

export interface InvocationCallMethods {
  emitCall(this: Generator, node: CallExpression): string;
  tryEmitBuiltinCall(this: Generator, node: CallExpression, callee: PropertyAccessExpression): string | undefined;
  emitModuleExport(this: Generator, node: CallExpression, exported: ModuleExport): string;
  runtimeCall(this: Generator, name: string, args: readonly string[]): string;
  emitArguments(this: Generator, args: readonly Expression[]): { argc: string; ptr: string };
}

/**
 * Fixed-arity `Math.<fn>` calls lower to their own runtime entry point instead
 * of `xt_math_call(name, argc, argv)`. The name and arity are known here, so
 * the generated call passes the arguments in registers and the runtime skips
 * the name dispatch and the argument array. A numeric kernel calls these
 * hundreds of times per pixel (the Saltline terrain generator alone executes
 * ~125 million `Math` calls), which made the generic ABI the dominant cost.
 * `min`/`max` stay variadic and keep `xt_math_call`.
 */
const MATH_DIRECT: Record<string, { symbol: string; arity: number }> = {
  abs: { symbol: "xt_math1_abs", arity: 1 },
  floor: { symbol: "xt_math1_floor", arity: 1 },
  ceil: { symbol: "xt_math1_ceil", arity: 1 },
  round: { symbol: "xt_math1_round", arity: 1 },
  trunc: { symbol: "xt_math1_trunc", arity: 1 },
  sqrt: { symbol: "xt_math1_sqrt", arity: 1 },
  cbrt: { symbol: "xt_math1_cbrt", arity: 1 },
  exp: { symbol: "xt_math1_exp", arity: 1 },
  log: { symbol: "xt_math1_log", arity: 1 },
  log2: { symbol: "xt_math1_log2", arity: 1 },
  log10: { symbol: "xt_math1_log10", arity: 1 },
  log1p: { symbol: "xt_math1_log1p", arity: 1 },
  expm1: { symbol: "xt_math1_expm1", arity: 1 },
  sinh: { symbol: "xt_math1_sinh", arity: 1 },
  cosh: { symbol: "xt_math1_cosh", arity: 1 },
  tanh: { symbol: "xt_math1_tanh", arity: 1 },
  asinh: { symbol: "xt_math1_asinh", arity: 1 },
  acosh: { symbol: "xt_math1_acosh", arity: 1 },
  atanh: { symbol: "xt_math1_atanh", arity: 1 },
  fround: { symbol: "xt_math1_fround", arity: 1 },
  clz32: { symbol: "xt_math1_clz32", arity: 1 },
  sin: { symbol: "xt_math1_sin", arity: 1 },
  cos: { symbol: "xt_math1_cos", arity: 1 },
  tan: { symbol: "xt_math1_tan", arity: 1 },
  asin: { symbol: "xt_math1_asin", arity: 1 },
  acos: { symbol: "xt_math1_acos", arity: 1 },
  atan: { symbol: "xt_math1_atan", arity: 1 },
  sign: { symbol: "xt_math1_sign", arity: 1 },
  pow: { symbol: "xt_math2_pow", arity: 2 },
  atan2: { symbol: "xt_math2_atan2", arity: 2 },
  hypot: { symbol: "xt_math2_hypot", arity: 2 },
  imul: { symbol: "xt_math2_imul", arity: 2 },
  random: { symbol: "xt_math0_random", arity: 0 },
};

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
      /* Reading `globalThis.require` is a CommonJS loader probe and reads as
       * `undefined`; calling it is a real CommonJS use, which xbintsc has no
       * runtime for. */
      const globalReceiver = globalObjectReceiver(access.expression);
      if (globalReceiver && !this.binding.symbolOfIdentifier.get(globalReceiver) && access.name.text === "require") {
        this.reportRequireUse(node);
        return i64(XT_UNDEFINED);
      }
      const special = this.tryEmitBuiltinCall(node, access);
      if (special) return special;
      if (isSuperKeyword(access.expression)) return this.emitSuperCall(node, access);
      const object = this.emitExpression(access.expression);
      const name = this.stringValue(access.name.text);
      const args = this.emitArguments(node.arguments);
      const result = this.reg();
      /* Method dispatch is monomorphic at most sites, so the call carries a
       * three-word inline cache: a shape/slot hit calls the cached method value
       * directly instead of walking the dispatch tables and the prototype
       * chain. See `runtime/xt_containers/access.inc`. */
      this.extraDeclarations.add("declare i64 @xt_call_method_ic(i64, i64, i32, i64*, i64*)");
      const cache = `@mt.${this.icCounter++}`;
      this.globals.push(`${cache} = internal global [3 x i64] zeroinitializer`);
      const slot = this.reg();
      this.emit(`  ${slot} = getelementptr [3 x i64], [3 x i64]* ${cache}, i64 0, i64 0`);
      this.emit(
        `  ${result} = call i64 @xt_call_method_ic(i64 ${object}, i64 ${name}, i32 ${args.argc}, i64* ${args.ptr}, i64* ${slot})`,
      );
      return result;
    }
    if (callee.kind === SyntaxKind.ElementAccessExpression) {
      const access = callee as ElementAccessExpression;
      if (isSuperKeyword(access.expression)) return this.emitSuperElementCall(node, access);
      const object = this.emitExpression(access.expression);
      const name = this.emitExpression(access.argumentExpression);
      const args = this.emitArguments(node.arguments);
      const result = this.reg();
      this.emit(`  ${result} = call i64 @xt_call_method(i64 ${object}, i64 ${name}, i32 ${args.argc}, i64* ${args.ptr})`);
      return result;
    }
    if (callee.kind === SyntaxKind.Identifier) {
      const identifier = callee as Identifier;
      if (identifier.text === "super") return this.emitSuperConstructor(node);
      const symbol = this.binding.symbolOfIdentifier.get(identifier);
      // `require("x")` is CommonJS; the ESM form is the only supported import.
      if (!symbol && identifier.text === "require") {
        this.reportRequireUse(node);
        return i64(XT_UNDEFINED);
      }
      if (symbol && symbol.kind === SymbolKind.Import) {
        const exported = this.importExports.get(symbol.id);
        if (exported) return this.emitModuleExport(node, exported);
        const defaultExport = this.importDefaults.get(symbol.id);
        if (defaultExport) return this.emitModuleExport(node, defaultExport);
      }
      if (symbol && symbol.kind === SymbolKind.Function) {
        const fn = implementationFunctionInfo(this.binding, symbol);
        /* Generator and `async` functions are not called directly: the call
         * must create a suspended generator / start a coroutine, so fall
         * through to the closure-call path. The same applies to a function that
         * captures outer bindings (or `this`): the direct call passes no
         * environment, so its free variables would read `undefined` and writes
         * to them would be lost. */
        if (fn && !fn.isGenerator && !fn.isAsync && fn.captures.length === 0 && !fn.capturesThis) {
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
      /* A browser global that is really a window method (`addEventListener`):
       * dispatch through the host object so `this` is the window. The builtin
       * table is consulted first — a host that needs to do extra work for a
       * global callable (`requestAnimationFrame` also arms the frame pump)
       * declares a builtin, and that must win over the plain member call. */
      const windowMember = !symbol ? this.globalValues[identifier.text] : undefined;
      const builtin = this.builtins[(callee as Identifier).text];
      if (windowMember?.member !== undefined && !builtin) {
        const host = this.emitGlobalValue({ symbol: windowMember.symbol });
        const name = this.stringValue(windowMember.member);
        const args = this.emitArguments(node.arguments);
        const result = this.reg();
        this.emit(
          `  ${result} = call i64 @xt_call_method(i64 ${host}, i64 ${name}, i32 ${args.argc}, i64* ${args.ptr})`,
        );
        return result;
      }
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
        // `Error("x")` without `new`: point the innermost frame at the call.
        if (ERROR_CONSTRUCTORS[(callee as Identifier).text]) {
          this.emit(`  call void @xt_stack_set_line(i32 ${this.lineOf(node)})`);
        }
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
      const direct = MATH_DIRECT[method];
      const hasSpread = node.arguments.some((argument) => argument.kind === SyntaxKind.SpreadElement);
      if (direct && !hasSpread && node.arguments.length === direct.arity) {
        const values = node.arguments.map((argument) => this.emitExpression(argument));
        const parameterList = values.map(() => "i64").join(", ");
        /* Numeric literals come back as bare bit patterns (`values.ts` leaves
         * the type to the instruction that consumes them), so every operand is
         * typed here explicitly. */
        const operands = values.map((value) => `i64 ${value}`).join(", ");
        this.extraDeclarations.add(`declare i64 @${direct.symbol}(${parameterList})`);
        const result = this.reg();
        this.emit(`  ${result} = call i64 @${direct.symbol}(${operands})`);
        return result;
      }
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
        // `hasOwn` guards against `Object.prototype` members leaking in as
        // fake exports (`ns.hasOwnProperty(...)` is an object method, not an
        // imported function).
        const exported = Object.prototype.hasOwnProperty.call(moduleExports, method)
          ? moduleExports[method]
          : undefined;
        if (exported) return this.emitModuleExport(node, exported);
      }
    }

    void BUILTIN_METHODS;
    return undefined;
  },

  /** Lower a call to an imported module binding to its runtime symbol. */
  emitModuleExport(node: CallExpression, exported: ModuleExport): string {
    const args = this.emitArguments(node.arguments);
    /* `createRequire(url)` hands back a `require` that answers from the table
     * of modules linked into this binary, so it has to exist first. */
    if (exported.symbol === "xt_module_create_require") this.ensureModuleRegistry();
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
