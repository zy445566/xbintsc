/**
 * Node's `util/types` subpath module: the `util.types` predicate functions as
 * named exports, reusing the `util` module's `isX` runtime symbols.
 */

import type { NodeModule } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const symbols: Record<string, string> = {
  isAnyArrayBuffer: "xt_util_is_array_buffer",
  isArrayBuffer: "xt_util_is_array_buffer",
  isDate: "xt_util_is_date",
  isMap: "xt_util_is_map",
  isPromise: "xt_util_is_promise",
  isRegExp: "xt_util_is_regexp",
  isSet: "xt_util_is_set",
  isTypedArray: "xt_util_is_typed_array",
  isUint8Array: "xt_util_is_uint8_array",
  isWeakMap: "xt_util_is_weak_map",
  isWeakSet: "xt_util_is_weak_set",
  isProxy: "xt_util_is_proxy",
};

const exports: ModuleExports = Object.fromEntries(
  Object.entries(symbols).map(([name, symbol]) => [name, { symbol }]),
);

export const utilTypesModule: NodeModule = {
  name: "util/types",
  runtimeSources: () => [],
  builtins: () => ({}),
  exports: () => exports,
};
