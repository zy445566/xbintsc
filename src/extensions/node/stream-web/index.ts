/**
 * Node's `stream/web` subpath module.
 *
 * The WHATWG stream classes are implemented by the core runtime (`xt_streams.c`)
 * and are also available as browser globals. This module re-exports the same
 * constructors; only `ReadableStreamTee` and the compression streams stay
 * Node-specific.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  ReadableStream: {
    symbol: "xt_readable_stream_ctor",
    valueSymbol: "xt_readable_stream_constructor_value",
    isConstructor: true,
  },
  ReadableStreamDefaultReader: { symbol: "xt_readable_stream_default_reader_ctor", isConstructor: true },
  ReadableStreamBYOBReader: { symbol: "xt_readable_stream_byob_reader_ctor", isConstructor: true },
  ReadableStreamBYOBRequest: { symbol: "xt_readable_stream_byob_request_ctor", isConstructor: true },
  ReadableStreamDefaultController: {
    symbol: "xt_readable_stream_default_controller_ctor",
    isConstructor: true,
  },
  WritableStream: {
    symbol: "xt_writable_stream_ctor",
    valueSymbol: "xt_writable_stream_constructor_value",
    isConstructor: true,
  },
  WritableStreamDefaultWriter: { symbol: "xt_writable_stream_default_writer_ctor", isConstructor: true },
  WritableStreamDefaultController: {
    symbol: "xt_writable_stream_default_controller_ctor",
    isConstructor: true,
  },
  TransformStream: {
    symbol: "xt_transform_stream_ctor",
    valueSymbol: "xt_transform_stream_constructor_value",
    isConstructor: true,
  },
  TransformStreamDefaultController: {
    symbol: "xt_transform_stream_default_controller_ctor",
    isConstructor: true,
  },
  ByteLengthQueuingStrategy: {
    symbol: "xt_byte_length_queuing_strategy_ctor",
    valueSymbol: "xt_byte_strategy_constructor_value",
    isConstructor: true,
  },
  CountQueuingStrategy: {
    symbol: "xt_count_queuing_strategy_ctor",
    valueSymbol: "xt_count_strategy_constructor_value",
    isConstructor: true,
  },
  TextEncoderStream: {
    symbol: "xt_text_encoder_stream_ctor",
    valueSymbol: "xt_text_encoder_stream_constructor_value",
    isConstructor: true,
  },
  TextDecoderStream: {
    symbol: "xt_text_decoder_stream_ctor",
    valueSymbol: "xt_text_decoder_stream_constructor_value",
    isConstructor: true,
  },
  CompressionStream: { symbol: "xt_compression_stream_ctor", isConstructor: true },
  DecompressionStream: { symbol: "xt_decompression_stream_ctor", isConstructor: true },
  ReadableStreamTee: { valueSymbol: "xt_stream_web_tee" },
};

export const streamWebModule: NodeModule = {
  name: "stream/web",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/stream_web/stream_web.c")],
  builtins: () => ({}),
  exports: () => exports,
};
