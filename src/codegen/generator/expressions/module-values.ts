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
import type { Generator } from "../generator.js";

export interface ModuleValueExpressionMethods {
  /** Lower one module export to a first-class closure value. */
  emitModuleExportValue(this: Generator, exported: ModuleExport): string;
  /** Build a namespace object holding every export of an imported module. */
  emitNamespaceObject(this: Generator, exports: ModuleExports): string;
}

export const moduleValueExpressionMethods: ModuleValueExpressionMethods = {
  emitModuleExportValue(this: Generator, exported: ModuleExport): string {
    if (exported.symbol) {
      // A direct runtime function: wrap its `(argc, argv)` pointer in a closure.
      this.extraDeclarations.add(`declare i64 @${exported.symbol}(i32, i64*)`);
      const fnPtr = this.reg();
      this.emit(`  ${fnPtr} = ptrtoint i64 (i32, i64*)* @${exported.symbol} to i64`);
      const env = this.alloca();
      this.emit(`  store i64 ${fnPtr}, i64* ${env}`);
      const closure = this.reg();
      this.emit(
        `  ${closure} = call i64 @xt_closure_new(i8* bitcast (i64 (i64, i64, i32, i64*)* @xt_fnptr_trampoline to i8*), i32 1, i64* ${env})`,
      );
      return closure;
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
};
