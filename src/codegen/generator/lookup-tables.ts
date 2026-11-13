/**
 * Hardening and extras for the builtin lookup tables in `./tables.ts`.
 *
 * The generator reads every table with `table[name]`, where `name` comes from the
 * program's source. A plain object literal inherits `Object.prototype`, so
 * `NAMESPACE_VALUES["toString"]` resolves to `Object.prototype.toString` and the
 * generator used to emit that function's text as an LLVM symbol
 * (`declare i64 @function toString() { [native code] }(i32, i64*)`), which clang
 * rejects. `constructor`, `valueOf` and the other inherited members did the same.
 *
 * `hardenLookupTables` drops the prototype of every table once, before code
 * generation starts. It lives here rather than in `tables.ts` so the tables stay
 * a data module (and the file stays inside the repository's length budget).
 */

import {
  BINARY_RUNTIME,
  BUILTIN_FUNCTION_VALUES,
  BUILTIN_INSTANCEOF,
  CONSOLE_METHODS,
  CTOR_FUNCTIONS,
  ERROR_CONSTRUCTORS,
  GLOBAL_FUNCTIONS,
  MATH_CONSTANTS,
  NAMESPACE_PROPERTIES,
  NAMESPACE_STATICS,
  NAMESPACE_VALUES,
  TYPED_ARRAYS,
} from "./tables.js";

/**
 * Namespaces whose properties are writable from JavaScript (`process.exitCode =
 * 1`), mapped to the runtime setter `(key, value)`. A namespace without an entry
 * is read-only: assigning to one of its properties takes the generic object path.
 */
export const NAMESPACE_PROPERTY_SETTERS: Record<string, string> = {
  process: "xt_process_set",
};

const LOOKUP_TABLES: readonly Record<string, unknown>[] = [
  BINARY_RUNTIME,
  BUILTIN_FUNCTION_VALUES,
  BUILTIN_INSTANCEOF,
  CONSOLE_METHODS,
  CTOR_FUNCTIONS,
  ERROR_CONSTRUCTORS,
  GLOBAL_FUNCTIONS,
  MATH_CONSTANTS,
  NAMESPACE_PROPERTIES,
  NAMESPACE_PROPERTY_SETTERS,
  NAMESPACE_STATICS,
  NAMESPACE_VALUES,
  TYPED_ARRAYS,
];

let hardened = false;

/** Idempotent: called by `generate` before the first expression is lowered. */
export function hardenLookupTables(): void {
  if (hardened) return;
  hardened = true;
  for (const table of LOOKUP_TABLES) Object.setPrototypeOf(table, null);
}
