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
  randomBytes: { symbol: "xt_crypto_random_bytes" },
  randomUUID: { symbol: "xt_crypto_random_uuid" },
  randomInt: { symbol: "xt_crypto_random_int" },
  randomFillSync: { symbol: "xt_crypto_random_fill_sync" },
  randomFill: { symbol: "xt_crypto_random_fill" },
  pbkdf2Sync: { symbol: "xt_crypto_pbkdf2_sync" },
  pbkdf2: { symbol: "xt_crypto_pbkdf2" },
  webcrypto: { symbol: "xt_crypto_webcrypto", valueSymbol: "xt_crypto_webcrypto" },
};

export const cryptoModule: NodeModule = {
  name: "crypto",
  runtimeSources: () => [
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/crypto/crypto.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/crypto/crypto_random.c"),
  ],
  builtins: () => builtins,
  exports: () => exports,
};
