/**
 * Node's `util` module.
 *
 * Exposes the commonly used helpers (`format`, `inspect`, `isDeepStrictEqual`,
 * `inherits`, `deprecate`, `promisify`) and the `isX` type predicates. Both
 * `import { format } from "util"` and `import util from "util"` (namespace
 * calls) are supported; the implementation lives in
 * `runtime/ext_node/util/util.c`.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const symbols: Record<string, string> = {
  format: "xt_util_format",
  formatWithOptions: "xt_util_format_with_options",
  inspect: "xt_util_inspect",
  isDeepStrictEqual: "xt_util_is_deep_strict_equal",
  inherits: "xt_util_inherits",
  deprecate: "xt_util_deprecate",
  promisify: "xt_util_promisify",
  isString: "xt_util_is_string",
  isNumber: "xt_util_is_number",
  isBoolean: "xt_util_is_boolean",
  isUndefined: "xt_util_is_undefined",
  isNull: "xt_util_is_null",
  isFunction: "xt_util_is_function",
  isArray: "xt_util_is_array",
  isObject: "xt_util_is_object",
  isBuffer: "xt_util_is_buffer",
  isDate: "xt_util_is_date",
  isRegExp: "xt_util_is_regexp",
  isPromise: "xt_util_is_promise",
  isError: "xt_util_is_error",
};

const exports: ModuleExports = Object.fromEntries(
  Object.entries(symbols).map(([name, symbol]) => [name, { symbol }]),
);

export const utilModule: NodeModule = {
  name: "util",
  namespace: "util",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/util/util.c")],
  builtins: () => ({}),
  exports: () => exports,
};
