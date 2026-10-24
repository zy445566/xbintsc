/**
 * Names the compiler resolves without a user declaration.
 *
 * The tables are the same ones code generation uses, so "known global" and
 * "codegen can lower it" cannot drift apart: a name added to a table becomes
 * legal in the checker in the same commit.
 */

import {
  BUILTIN_FUNCTION_VALUES,
  CTOR_FUNCTIONS,
  ERROR_CONSTRUCTORS,
  FIRST_CLASS_CONSTRUCTORS,
  GLOBAL_FUNCTIONS,
  NAMESPACE_PROPERTIES,
  NAMESPACE_STATICS,
  TYPED_ARRAYS,
} from "../codegen/generator/tables.js";

const KNOWN_GLOBALS: ReadonlySet<string> = new Set<string>([
  ...Object.keys(GLOBAL_FUNCTIONS),
  ...Object.keys(BUILTIN_FUNCTION_VALUES),
  ...Object.keys(ERROR_CONSTRUCTORS),
  ...Object.keys(CTOR_FUNCTIONS),
  ...Object.keys(TYPED_ARRAYS),
  ...Object.keys(NAMESPACE_STATICS),
  ...Object.keys(NAMESPACE_PROPERTIES),
  ...FIRST_CLASS_CONSTRUCTORS,
  // Handled directly in `emitIdentifier` rather than through a table.
  "console",
  "undefined",
  "NaN",
  "Infinity",
  "arguments",
]);

/** True when `name` is a runtime global (or an extension-provided builtin). */
export function isKnownGlobalName(name: string, extensionBuiltins?: ReadonlySet<string>): boolean {
  return KNOWN_GLOBALS.has(name) || (extensionBuiltins?.has(name) ?? false);
}
