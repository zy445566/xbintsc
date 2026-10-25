/**
 * Assembles the LLVM IR generator from its method groups.
 *
 * The implementation is split across `context` (shared state and primitives)
 * and several method-group modules. Each group contributes an interface plus a
 * plain object of methods; they are merged onto the prototype here so the
 * groups can freely call one another through `this`.
 */

import type { SourceFileNode } from "../../ast/nodes.js";
import type { DiagnosticBag } from "../../diagnostics/diagnostic.js";
import { bind } from "../../binder/binder.js";
import { checkProgram } from "../../checker/checker.js";
import { GeneratorContext } from "./context.js";
import { moduleMethods, type ModuleMethods } from "./module.js";
import { statementMethods, type StatementMethods } from "./statements.js";
import { expressionMethods, type ExpressionMethods } from "./expressions.js";
import { callMethods, type CallMethods } from "./calls.js";
import { numericMethods, type NumericMethods } from "./numeric.js";
import { debugMethods, type DebugMethods } from "./debug.js";
import type { CodegenOptions, CodegenResult } from "./state.js";

export class Generator extends GeneratorContext {}

export interface Generator
  extends ModuleMethods,
    StatementMethods,
    ExpressionMethods,
    CallMethods,
    NumericMethods,
    DebugMethods {}

Object.assign(
  Generator.prototype,
  moduleMethods,
  statementMethods,
  expressionMethods,
  callMethods,
  numericMethods,
  debugMethods,
);

export type { CodegenOptions, CodegenResult } from "./state.js";

export function generate(
  sourceFile: SourceFileNode,
  diagnostics: DiagnosticBag,
  options: CodegenOptions = {},
): CodegenResult {
  // Bind once: the checker and the generator must see the same symbol table.
  const binding = bind(sourceFile);
  if (options.check !== false) {
    checkProgram(sourceFile, binding, diagnostics, {
      builtins: options.builtins ?? {},
      globalValues: options.globalValues ?? {},
    });
  }
  const generator = new Generator(sourceFile, diagnostics, options, binding);
  return { ir: generator.run(), binding: generator.binding };
}
