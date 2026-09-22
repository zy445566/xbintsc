/**
 * Literal lowering: array and object literals (including spread elements).
 */

import {
  SyntaxKind,
  type ArrayLiteralExpression,
  type Expression,
  type ObjectLiteralExpression,
} from "../../../ast/nodes.js";
import { propertyNameText } from "../tables.js";
import type { Generator } from "../generator.js";

export interface LiteralCallMethods {
  emitArrayLiteral(this: Generator, node: ArrayLiteralExpression): string;
  emitObjectLiteral(this: Generator, node: ObjectLiteralExpression): string;
}

export const literalCallMethods: LiteralCallMethods = {
  emitArrayLiteral(node: ArrayLiteralExpression): string {
    const hasSpread = node.elements.some((element) => element.kind === SyntaxKind.SpreadElement);
    if (!hasSpread) {
      const args = this.emitArguments(node.elements);
      const result = this.reg();
      this.emit(`  ${result} = call i64 @xt_array_new(i32 ${args.argc}, i64* ${args.ptr})`);
      return result;
    }
    const array = this.reg();
    this.emit(`  ${array} = call i64 @xt_array_new(i32 0, i64* null)`);
    for (const element of node.elements) {
      if (element.kind === SyntaxKind.SpreadElement) {
        const spread = this.emitExpression((element as { expression: Expression }).expression);
        this.emit(`  call i64 @xt_array_spread(i64 ${array}, i64 ${spread})`);
      } else {
        const value = this.emitExpression(element);
        this.emit(`  call i64 @xt_array_push(i64 ${array}, i64 ${value})`);
      }
    }
    return array;
  },

  emitObjectLiteral(node: ObjectLiteralExpression): string {
    const object = this.reg();
    this.emit(`  ${object} = call i64 @xt_object_new()`);
    for (const property of node.properties) {
      if (property.kind === SyntaxKind.PropertyAssignment) {
        const nameNode = property.name;
        let key: string;
        if (nameNode.kind === SyntaxKind.ComputedPropertyName) {
          const keyValue = this.emitExpression((nameNode as { expression: Expression }).expression);
          key = this.reg();
          this.emit(`  ${key} = call i64 @xt_to_string(i64 ${keyValue})`);
        } else {
          key = this.stringValue(propertyNameText(nameNode));
        }
        const value = this.emitExpression(property.initializer);
        if (property.accessor === "get") {
          this.emit(`  call i64 @xt_object_define_getter(i64 ${object}, i64 ${key}, i64 ${value})`);
        } else if (property.accessor === "set") {
          this.emit(`  call i64 @xt_object_define_setter(i64 ${object}, i64 ${key}, i64 ${value})`);
        } else {
          this.emit(`  call i64 @xt_set(i64 ${object}, i64 ${key}, i64 ${value})`);
        }
      } else if (property.kind === SyntaxKind.ShorthandPropertyAssignment) {
        if (property.initializer) {
          // `{ a = 1 }` is only legal as a destructuring target, not a value.
          this.unsupported(property, "default value in object literal");
          continue;
        }
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
};
