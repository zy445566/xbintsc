/**
 * Extension-provided global values.
 *
 * An extension names a global (`window`, `document`, `Image`, `location`) and
 * the runtime getter that produces it; this group turns such a name into a
 * value, including the two richer shapes:
 *
 *   - a global that is a *member of a host object* (`location` off the window),
 *     so a browser global that only exists as a window property is still live;
 *   - `console`, which is both a namespace at call sites and a first-class
 *     object as a value (`console.error.bind(console)`).
 */

import type { GlobalValue } from "../../../extensions/registry.js";
import type { Generator } from "../generator.js";

export interface GlobalValueMethods {
  emitGlobalValue(this: Generator, value: GlobalValue): string;
}

export const globalValueMethods: GlobalValueMethods = {
  /**
   * Read an extension-provided global (`window`, `document`, `new Image()`).
   * The getter is a normal runtime call with the uniform ABI; the optional
   * argument is how one getter serves a whole family of host types.
   */
  emitGlobalValue(value: GlobalValue): string {
    this.extraDeclarations.add(`declare i64 @${value.symbol}(i32, i64*)`);
    let argc = "0";
    let pointer = "i64* null";
    if (value.argument !== undefined) {
      const argument = this.stringValue(value.argument);
      const slot = `%gval${this.current.allocas.length}`;
      this.current.allocas.push(`${slot} = alloca i64, i32 1`);
      const cell = this.reg();
      this.emit(`  ${cell} = getelementptr i64, i64* ${slot}, i32 0`);
      this.emit(`  store i64 ${argument}, i64* ${cell}`);
      argc = "1";
      pointer = `i64* ${slot}`;
    }
    const result = this.reg();
    this.emit(`  ${result} = call i64 @${value.symbol}(i32 ${argc}, ${pointer})`);
    if (value.member === undefined) return result;
    /* A browser global that lives on the window object (`location`): read it
     * off the host object so a getter runs and answers a live value. */
    const name = this.stringValue(value.member);
    return this.runtimeCall("xt_get", [result, name]);
  },
};
