/**
 * Node's `string_decoder` module: the `StringDecoder` class.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  StringDecoder: { symbol: "xt_string_decoder_ctor", isConstructor: true },
};

export const stringDecoderModule: NodeModule = {
  name: "string_decoder",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/string_decoder/string_decoder.c")],
  builtins: () => ({}),
  exports: () => exports,
};
