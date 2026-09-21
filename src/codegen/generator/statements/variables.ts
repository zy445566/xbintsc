/**
 * Variable lowering: declarations, destructuring bindings, default values and
 * enum objects.
 */

import {
  SyntaxKind,
  type ArrayBindingPattern,
  type BindingName,
  type EnumDeclaration,
  type Expression,
  type ObjectBindingPattern,
  type VariableStatement,
} from "../../../ast/nodes.js";
import { i64, numberLiteral, XT_UNDEFINED } from "../../values.js";
import { propertyNameText } from "../tables.js";
import type { Generator } from "../generator.js";

export interface VariableStatementMethods {
  emitVariableStatement(this: Generator, statement: VariableStatement): void;
  emitBindingPattern(this: Generator, name: BindingName, value: string): void;
  emitBindingDefault(this: Generator, value: string, initializer: Expression): string;
  emitEnum(this: Generator, statement: EnumDeclaration): void;
}

export const variableStatementMethods: VariableStatementMethods = {
  emitVariableStatement(statement: VariableStatement): void {
    for (const declaration of statement.declarationList.declarations) {
      if (declaration.name.kind === SyntaxKind.Identifier) {
        const symbol = this.binding.symbolOfDeclaration.get(declaration);
        if (!symbol) {
          if (declaration.initializer) this.emitExpression(declaration.initializer);
          continue;
        }
        if (this.current.slots.has(symbol.id)) {
          // Hoisted `var` (or an already-declared slot): keep evaluating the
          // initializer for its side effects, but do not redeclare the slot.
          if (declaration.initializer) this.emitExpression(declaration.initializer);
          continue;
        }
        if (symbol.boxed) {
          // A captured binding lives in a box. Create the box *before*
          // evaluating the initializer so a self-referencing closure
          // (`const f = () => f()`) captures the box, not a copy of the
          // still-uninitialised value.
          this.declareSlot(symbol, i64(XT_UNDEFINED));
          if (declaration.initializer) this.writeSlot(symbol, this.emitExpression(declaration.initializer));
          continue;
        }
        const initial = declaration.initializer ? this.emitExpression(declaration.initializer) : i64(XT_UNDEFINED);
        this.declareSlot(symbol, initial);
      } else {
        const initial = declaration.initializer ? this.emitExpression(declaration.initializer) : i64(XT_UNDEFINED);
        this.emitBindingPattern(declaration.name, initial);
      }
    }
  },

  /** Lower a destructuring binding pattern, declaring each bound identifier. */
  emitBindingPattern(name: BindingName, value: string): void {
    if (name.kind === SyntaxKind.Identifier) {
      const symbol = this.binding.symbolOfDeclaration.get(name);
      if (!symbol) return;
      if (this.current.slots.has(symbol.id)) this.writeSlot(symbol, value);
      else this.declareSlot(symbol, value);
      return;
    }
    if (name.kind === SyntaxKind.ArrayBindingPattern) {
      const pattern = name as ArrayBindingPattern;
      for (let index = 0; index < pattern.elements.length; index++) {
        const element = pattern.elements[index];
        if (!element) continue;
        let elementValue: string;
        if (element.dotDotDotToken) {
          const startPtr = this.alloca();
          this.emit(`  store i64 ${numberLiteral(index)}, i64* ${startPtr}`);
          const sliceName = this.stringValue("slice");
          elementValue = this.reg();
          this.emit(`  ${elementValue} = call i64 @xt_call_method(i64 ${value}, i64 ${sliceName}, i32 1, i64* ${startPtr})`);
        } else {
          elementValue = this.reg();
          this.emit(`  ${elementValue} = call i64 @xt_get(i64 ${value}, i64 ${numberLiteral(index)})`);
        }
        if (element.initializer) elementValue = this.emitBindingDefault(elementValue, element.initializer);
        this.emitBindingPattern(element.name, elementValue);
      }
      return;
    }
    const pattern = name as ObjectBindingPattern;
    for (const element of pattern.elements) {
      const key = element.propertyName ?? (element.name.kind === SyntaxKind.Identifier ? element.name : undefined);
      if (element.dotDotDotToken) {
        this.unsupported(element, "object rest destructuring");
        continue;
      }
      const keyValue = this.stringValue(key ? propertyNameText(key) : "undefined");
      let elementValue = this.reg();
      this.emit(`  ${elementValue} = call i64 @xt_get(i64 ${value}, i64 ${keyValue})`);
      if (element.initializer) elementValue = this.emitBindingDefault(elementValue, element.initializer);
      this.emitBindingPattern(element.name, elementValue);
    }
  },

  /** `x = fallback` in a binding pattern: only apply when `x` is `undefined`. */
  emitBindingDefault(value: string, initializer: Expression): string {
    const fallback = this.emitExpression(initializer);
    const isUndefined = this.reg();
    this.emit(`  ${isUndefined} = call i64 @xt_seq(i64 ${value}, i64 ${i64(XT_UNDEFINED)})`);
    const truthy = this.reg();
    this.emit(`  ${truthy} = call i32 @xt_truthy(i64 ${isUndefined})`);
    const condition = this.reg();
    this.emit(`  ${condition} = icmp ne i32 ${truthy}, 0`);
    const result = this.reg();
    this.emit(`  ${result} = select i1 ${condition}, i64 ${fallback}, i64 ${value}`);
    return result;
  },

  /**
   * `enum E { A, B }` (and `const enum`) lowers to a runtime object with both
   * forward (`E.A`) and reverse (`E[0] === "A"`) numeric mappings.
   */
  emitEnum(statement: EnumDeclaration): void {
    const symbol = this.binding.symbolOfDeclaration.get(statement);
    const object = this.reg();
    this.emit(`  ${object} = call i64 @xt_object_new()`);
    let next = 0;
    for (const member of statement.members) {
      const name = propertyNameText(member.name);
      const key = this.stringValue(name);
      let value: string;
      if (member.initializer) {
        value = this.emitExpression(member.initializer);
        if (member.initializer.kind === SyntaxKind.NumericLiteral) {
          const numeric = (member.initializer as { value?: number }).value ?? 0;
          next = numeric + 1;
        } else {
          next++; 
        }
      } else {
        value = numberLiteral(next);
        next++;
      }
      this.emit(`  call i64 @xt_set(i64 ${object}, i64 ${key}, i64 ${value})`);
      // Reverse mapping (only meaningful for numeric members).
      if (!member.initializer || member.initializer.kind === SyntaxKind.NumericLiteral) {
        const reverseKey = this.stringValue(name);
        this.emit(`  call i64 @xt_set(i64 ${object}, i64 ${value}, i64 ${reverseKey})`);
      }
    }
    if (symbol) this.declareSlot(symbol, object);
  },
};
