/**
 * First-class values for imported extension bindings.
 *
 * An imported binding is normally called (`readFileSync(...)`) or namespaced
 * (`path.join(...)`), both lowered before identifier handling. When it is read
 * as a *value* — `const a = assert`, `arr.filter(strictEqual)` — the runtime
 * symbol has no object of its own, so these helpers wrap it in a closure (or,
 * for a whole-namespace import, build an object holding every export).
 */

import { i64, XT_UNDEFINED } from "../../values.js";
import { NAMESPACE_STATICS } from "../tables.js";
import type { ModuleExport, ModuleExports } from "../../../extensions/registry.js";
import type { Expression } from "../../../ast/nodes.js";
import type { Generator } from "../generator.js";

export interface ModuleValueExpressionMethods {
  /** Lower one module export to a first-class closure value. */
  emitModuleExportValue(this: Generator, exported: ModuleExport): string;
  /** Build a namespace object holding every export of an imported module. */
  emitNamespaceObject(this: Generator, exports: ModuleExports): string;
  /** Lower a non-literal `import(specifier)` to a rejected promise. */
  emitDynamicImportReject(this: Generator, specifier: Expression): string;
}

export const moduleValueExpressionMethods: ModuleValueExpressionMethods = {
  emitModuleExportValue(this: Generator, exported: ModuleExport): string {
    if (exported.symbol) {
      // A direct runtime function: wrap its `(argc, argv)` pointer in a closure.
      // A `void` binding (the console output methods) keeps that return type so
      // the declaration matches the runtime's, and gets a trampoline that
      // answers `undefined`.
      const returnsVoid = exported.returnVoid === true;
      const resultType = returnsVoid ? "void" : "i64";
      this.extraDeclarations.add(`declare ${resultType} @${exported.symbol}(i32, i64*)`);
      const fnPtr = this.reg();
      this.emit(`  ${fnPtr} = ptrtoint ${resultType} (i32, i64*)* @${exported.symbol} to i64`);
      const env = this.alloca();
      this.emit(`  store i64 ${fnPtr}, i64* ${env}`);
      const trampoline = returnsVoid ? "xt_voidptr_trampoline" : "xt_fnptr_trampoline";
      const closure = this.reg();
      this.emit(
        `  ${closure} = call i64 @xt_closure_new(i8* bitcast (i64 (i64, i64, i32, i64*)* @${trampoline} to i8*), i32 1, i64* ${env})`,
      );
      /* A native constructor publishes its prototype object, so a class that
       * extends this value chains onto the real `X.prototype` (with its
       * methods) instead of a fresh plain object. */
      if (exported.prototypeSymbol) {
        this.extraDeclarations.add(`declare i64 @${exported.prototypeSymbol}(i32, i64*)`);
        const prototype = this.reg();
        this.emit(`  ${prototype} = call i64 @${exported.prototypeSymbol}(i32 0, i64* null)`);
        this.emit(`  call i64 @xt_function_set_prototype(i64 ${closure}, i64 ${prototype})`);
      }
      return closure;
    }
    if (exported.valueSymbol) {
      // A plain value (`constants.O_WRONLY`, `worker_threads.isMainThread`):
      // the getter has the uniform `(i32, i64*)` ABI and is invoked with no
      // arguments. This makes it work inside a built namespace object, not just
      // as a direct named import.
      this.extraDeclarations.add(`declare i64 @${exported.valueSymbol}(i32, i64*)`);
      const result = this.reg();
      this.emit(`  ${result} = call i64 @${exported.valueSymbol}(i32 0, i64* null)`);
      return result;
    }
    if (exported.namespace && exported.method) {
      const dispatcher = NAMESPACE_STATICS[exported.namespace];
      if (dispatcher) {
        this.extraDeclarations.add(`declare i64 @${dispatcher}(i64, i32, i64*)`);
        const name = this.stringValue(exported.method);
        const fnPtr = this.reg();
        this.emit(`  ${fnPtr} = ptrtoint i64 (i64, i32, i64*)* @${dispatcher} to i64`);
        const env = `%mval${this.current.allocas.length}`;
        this.current.allocas.push(`${env} = alloca i64, i32 2`);
        const slot0 = this.reg();
        this.emit(`  ${slot0} = getelementptr i64, i64* ${env}, i32 0`);
        this.emit(`  store i64 ${name}, i64* ${slot0}`);
        const slot1 = this.reg();
        this.emit(`  ${slot1} = getelementptr i64, i64* ${env}, i32 1`);
        this.emit(`  store i64 ${fnPtr}, i64* ${slot1}`);
        const closure = this.reg();
        this.emit(
          `  ${closure} = call i64 @xt_closure_new(i8* bitcast (i64 (i64, i64, i32, i64*)* @xt_method_trampoline to i8*), i32 2, i64* ${env})`,
        );
        return closure;
      }
    }
    return i64(XT_UNDEFINED);
  },

  emitNamespaceObject(this: Generator, exports: ModuleExports): string {
    const object = this.reg();
    this.emit(`  ${object} = call i64 @xt_object_new()`);
    for (const [name, exported] of Object.entries(exports)) {
      const key = this.stringValue(name);
      const value = this.emitModuleExportValue(exported);
      this.emit(`  call i64 @xt_set(i64 ${object}, i64 ${key}, i64 ${value})`);
    }
    return object;
  },

  emitDynamicImportReject(this: Generator, specifier: Expression): string {
    /* A compiled binary has no module loader, but the modules linked into it
     * are still there: the specifier can name any enabled extension module, so
     * build a table of their namespace objects and let the runtime look it up.
     * Anything else rejects with a message that names the specifier. This is
     * what makes `import("node:" + "fs")` — the usual "load Node built-ins
     * without a bundler" idiom — work. */
    const value = this.emitExpression(specifier);
    const table = this.reg();
    this.emit(`  ${table} = call i64 @xt_object_new()`);
    for (const [name, module] of Object.entries(this.modules)) {
      const exports = module.exports;
      if (!exports) continue;
      const namespace = this.emitNamespaceObject(exports);
      const keys = name.startsWith("node:") ? [name] : [name, `node:${name}`];
      for (const key of keys) {
        this.emit(`  call i64 @xt_set(i64 ${table}, i64 ${this.stringValue(key)}, i64 ${namespace})`);
      }
    }
    this.extraDeclarations.add("declare i64 @xt_dynamic_import(i64, i64)");
    const result = this.reg();
    this.emit(`  ${result} = call i64 @xt_dynamic_import(i64 ${table}, i64 ${value})`);
    return result;
  },
};
