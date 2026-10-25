/**
 * `super` lowering: parent constructor delegation and parent method calls.
 */

import {
  SyntaxKind,
  type CallExpression,
  type ElementAccessExpression,
  type Expression,
  type Identifier,
  type PropertyAccessExpression,
} from "../../../ast/nodes.js";
import type { Generator } from "../generator.js";

/** True when `node` is the `super` keyword (it never binds to a symbol). */
export function isSuperKeyword(node: Expression): boolean {
  return node.kind === SyntaxKind.Identifier && (node as Identifier).text === "super";
}

export interface SuperCallMethods {
  emitSuperConstructor(this: Generator, node: CallExpression): string;
  emitSuperCall(this: Generator, node: CallExpression, access: PropertyAccessExpression): string;
  /** `super[key](...)`: look the method up on the parent holder and call it
   *  with the current `this`. */
  emitSuperElementCall(this: Generator, node: CallExpression, access: ElementAccessExpression): string;
  /** The object `super.x` reads from: the parent prototype for an instance
   * method, the parent constructor for a `static` method or static block. */
  emitSuperHolder(this: Generator): string;
}

export const superCallMethods: SuperCallMethods = {
  emitSuperConstructor(node: CallExpression): string {
    const thisValue = this.emitThis();
    const classInfo = this.current.fn.classInfo;
    const parent = classInfo?.parentExpression;
    const errorName = parent ? this.errorFamilyName(parent) : undefined;
    if (errorName) {
      const args = this.emitArguments(node.arguments);
      const nameValue = this.stringValue(errorName);
      const result = this.reg();
      this.emit(
        `  ${result} = call i64 @xt_error_init(i64 ${thisValue}, i64 ${nameValue}, i32 ${args.argc}, i64* ${args.ptr})`,
      );
      return result;
    }
    /* The parent prototype is decided by the *class* whose constructor is
     * running, never by walking `this`: an instance of a subclass sits at the
     * bottom of a longer chain, so `getPrototype(getPrototype(this))` from a
     * base constructor resolves to that very class again (`C extends B extends
     * A`: inside `B`, the walk answers `B.prototype`) and `super()` recurses
     * forever. `getPrototype(getPrototype(C))` is `C.prototype`'s parent, which
     * is what the heritage clause installed. */
    const owner = classInfo ? this.emitClassReference(classInfo) : thisValue;
    const proto = this.reg();
    this.emit(`  ${proto} = call i64 @xt_function_get_prototype(i64 ${owner})`);
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
    const parentProto = this.emitSuperHolder();
    const key = this.stringValue(access.name.text);
    const method = this.reg();
    this.emit(`  ${method} = call i64 @xt_get(i64 ${parentProto}, i64 ${key})`);
    const args = this.emitArguments(node.arguments);
    const result = this.reg();
    this.emit(`  ${result} = call i64 @xt_call_with_this(i64 ${method}, i64 ${thisValue}, i32 ${args.argc}, i64* ${args.ptr})`);
    return result;
  },

  emitSuperElementCall(node: CallExpression, access: ElementAccessExpression): string {
    const thisValue = this.emitThis();
    const parentProto = this.emitSuperHolder();
    const key = this.emitExpression(access.argumentExpression);
    const method = this.reg();
    this.emit(`  ${method} = call i64 @xt_get(i64 ${parentProto}, i64 ${key})`);
    const args = this.emitArguments(node.arguments);
    const result = this.reg();
    this.emit(`  ${result} = call i64 @xt_call_with_this(i64 ${method}, i64 ${thisValue}, i32 ${args.argc}, i64* ${args.ptr})`);
    return result;
  },

  emitSuperHolder(): string {
    /* Same reasoning as `emitSuperConstructor`: the holder is a property of the
     * *class*, not of the receiver. Walking `this` would resolve `super.foo()`
     * inside a base method to the receiver's own prototype, so a subclass
     * instance would call the override it was trying to bypass. */
    const classInfo = this.current.fn.classInfo;
    if (classInfo) {
      const owner = this.emitClassReference(classInfo);
      /* A static context has the constructor as `this`, and its own prototype
       * already is the parent constructor (static inheritance). */
      if (this.current.fn.isStatic) {
        const parentCtor = this.reg();
        this.emit(`  ${parentCtor} = call i64 @xt_object_get_prototype(i64 ${owner})`);
        return parentCtor;
      }
      const ownProto = this.reg();
      this.emit(`  ${ownProto} = call i64 @xt_function_get_prototype(i64 ${owner})`);
      const parentProto = this.reg();
      this.emit(`  ${parentProto} = call i64 @xt_object_get_prototype(i64 ${ownProto})`);
      return parentProto;
    }
    const thisValue = this.emitThis();
    const holder = this.reg();
    this.emit(`  ${holder} = call i64 @xt_object_get_prototype(i64 ${thisValue})`);
    if (this.current.fn.isStatic) return holder;
    const parentProto = this.reg();
    this.emit(`  ${parentProto} = call i64 @xt_object_get_prototype(i64 ${holder})`);
    return parentProto;
  },
};
