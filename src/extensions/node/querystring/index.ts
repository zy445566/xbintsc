/**
 * Node's `querystring` module.
 *
 * `parse` / `decode` and `stringify` / `encode` are the same functions under
 * two names, alongside `escape` / `unescape`. Both named imports and namespace
 * calls (`import qs from "querystring"`) are supported; the implementation
 * lives in `runtime/ext_node/querystring/querystring.c`.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  parse: { symbol: "xt_querystring_parse" },
  decode: { symbol: "xt_querystring_parse" },
  stringify: { symbol: "xt_querystring_stringify" },
  encode: { symbol: "xt_querystring_stringify" },
  escape: { symbol: "xt_querystring_escape" },
  unescape: { symbol: "xt_querystring_unescape" },
};

export const querystringModule: NodeModule = {
  name: "querystring",
  namespace: "querystring",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/querystring/querystring.c")],
  builtins: () => ({}),
  exports: () => exports,
};
