/**
 * `super` lowering: parent constructor delegation and parent method calls.
 */

import {
  SyntaxKind,
  type CallExpression,
  type Identifier,
  type PropertyAccessExpression,
} from "../../../ast/nodes.js";
import type { Generator } from "../generator.js";

export interface SuperCallMethods {
  emitSuperConstructor(this: Generator, node: CallExpression): string;
  emitSuperCall(this: Generator, node: CallExpression, access: PropertyAccessExpression): string;
}

export const superCallMethods: SuperCallMethods = {
  emitSuperConstructor(node: CallExpression): string {
    const thisValue = this.emitThis();
    const classInfo = this.current.fn.classInfo;
    const parent = classInfo?.parentExpression;
    const unboundBuiltin =
      parent !== undefined &&
      parent.kind === SyntaxKind.Identifier &&
      (parent as Identifier).text === "Error" &&
      !this.binding.symbolOfIdentifier.get(parent as Identifier);
    if (unboundBuiltin) {
      const args = this.emitArguments(node.arguments);
      const result = this.reg();
      this.emit(`  ${result} = call i64 @xt_error_init(i64 ${thisValue}, i32 ${args.argc}, i64* ${args.ptr})`);
      return result;
    }
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
