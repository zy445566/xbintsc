/**
 * Node's `stream/web` subpath module (WHATWG streams, not implemented).
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  ReadableStreamTee: { valueSymbol: "xt_stream_web_tee" },
  ReadableStream: { symbol: "xt_stream_web_unsupported_ctor", isConstructor: true },
  WritableStream: { symbol: "xt_stream_web_unsupported_ctor", isConstructor: true },
  TransformStream: { symbol: "xt_stream_web_unsupported_ctor", isConstructor: true },
};

export const streamWebModule: NodeModule = {
  name: "stream/web",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/stream_web/stream_web.c")],
  builtins: () => ({}),
  exports: () => exports,
};
