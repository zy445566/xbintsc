/**
 * Member access lowering: property/element reads, `delete` and optional
 * chaining guards.
 */

import {
  SyntaxKind,
  type DeleteExpression,
  type ElementAccessExpression,
  type Identifier,
  type PropertyAccessExpression,
} from "../../../ast/nodes.js";
import { i64, numberLiteral, XT_TRUE, XT_UNDEFINED } from "../../values.js";
import { MATH_CONSTANTS, NAMESPACE_PROPERTIES } from "../tables.js";
import type { Generator } from "../generator.js";

export interface AccessCallMethods {
  emitDelete(this: Generator, node: DeleteExpression): string;
  emitPropertyAccess(this: Generator, node: PropertyAccessExpression): string;
  emitElementAccess(this: Generator, node: ElementAccessExpression): string;
  emitOptional(this: Generator, objectValue: string, compute: () => string): string;
}

export const accessCallMethods: AccessCallMethods = {
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
    if (node.expression.kind === SyntaxKind.Identifier) {
      const namespace = this.namespaceOfSymbol(
        this.binding.symbolOfIdentifier.get(node.expression as Identifier),
      );
      if (namespace) {
        const getter = NAMESPACE_PROPERTIES[namespace];
        if (getter) {
          const key = this.stringValue(node.name.text);
          const result = this.reg();
          this.emit(`  ${result} = call i64 @${getter}(i64 ${key})`);
          return result;
        }
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
};
