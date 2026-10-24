/**
 * The opt-out semantic checker (`--check` / `--no-check`).
 *
 * It runs after binding and before code generation. The first three passes are
 * inference-free:
 *
 *   1. identifier references with no declaration and no runtime global,
 *   2. same-scope declarations that cannot merge (`let x` twice, ...),
 *   3. calls whose argument count cannot match a locally-visible signature.
 *
 * The fourth pass uses a deliberately small structural type model to check
 * assignability, property existence and simple flow narrowing. It understands
 * annotations, interfaces, classes, arrays, functions, unions and simple
 * generics; anything else is `any`, so unsupported syntax can hide a bug but
 * never reject working code.
 */

import type { SourceFileNode } from "../ast/nodes.js";
import type { BindResult } from "../binder/binder.js";
import type { DiagnosticBag } from "../diagnostics/diagnostic.js";
import { checkCallArity } from "./calls.js";
import { reportDeclarationConflicts } from "./declarations.js";
import { checkTypes } from "./typecheck.js";
import { checkUnresolvedNames } from "./undefined.js";

export interface CheckOptions {
  /** Globals supplied by the registered extensions (`print`, `__registerScript`, ...). */
  readonly builtins?: Readonly<Record<string, unknown>>;
}

export function checkProgram(
  sourceFile: SourceFileNode,
  binding: BindResult,
  diagnostics: DiagnosticBag,
  options: CheckOptions = {},
): void {
  const builtinNames = options.builtins ? new Set(Object.keys(options.builtins)) : undefined;
  checkUnresolvedNames(sourceFile, binding, diagnostics, builtinNames);
  reportDeclarationConflicts(sourceFile, binding, diagnostics);
  checkCallArity(sourceFile, binding, diagnostics);
  checkTypes(sourceFile, binding, diagnostics);
}
