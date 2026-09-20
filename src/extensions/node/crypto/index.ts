/**
 * Node's `crypto` module: `createHash(...).update(...).digest("hex")`, backed
 * by a self-contained SHA-256 in `runtime/ext_node/crypto`.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { BuiltinFunction, ModuleExports } from "../../registry.js";

const builtins: Record<string, BuiltinFunction> = {
  createHash: { symbol: "xt_crypto_create_hash" },
};

const exports: ModuleExports = {
  createHash: { symbol: "xt_crypto_create_hash" },
};

export const cryptoModule: NodeModule = {
  name: "crypto",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/crypto/crypto.c")],
  builtins: () => builtins,
  exports: () => exports,
};
