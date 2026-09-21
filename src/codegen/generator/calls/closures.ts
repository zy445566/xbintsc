/**
 * Closure lowering: arrow/function expressions and their captured environments.
 */

import type { ArrowFunction, FunctionExpression } from "../../../ast/nodes.js";
import type { FunctionInfo } from "../../../binder/binder.js";
import { i64, XT_UNDEFINED } from "../../values.js";
import type { Generator } from "../generator.js";

export interface ClosureCallMethods {
  emitClosure(this: Generator, node: ArrowFunction | FunctionExpression): string;
  emitClosureValue(this: Generator, fn: FunctionInfo): string;
}

export const closureCallMethods: ClosureCallMethods = {
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
};
