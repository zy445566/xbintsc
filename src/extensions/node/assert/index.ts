/**
 * Node's `assert` module.
 *
 * All assertions dispatch through `runtime/ext_node/assert` via the `assert`
 * namespace (`xt_assert_static`). `import assert from "node:assert"` binds the
 * namespace, so `assert.strictEqual(...)` works, and named imports such as
 * `import { strictEqual } from "assert"` lower to the same dispatcher.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const methods = [
  "ok",
  "fail",
  "equal",
  "notEqual",
  "strictEqual",
  "notStrictEqual",
  "deepEqual",
  "notDeepEqual",
  "deepStrictEqual",
  "notDeepStrictEqual",
  "throws",
  "doesNotThrow",
  "rejects",
  "doesNotReject",
  "match",
  "doesNotMatch",
  "ifError",
] as const;

const exports: ModuleExports = Object.fromEntries(
  methods.map((method) => [method, { namespace: "assert", method }]),
);

export const assertModule: NodeModule = {
  name: "assert",
  namespace: "assert",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/assert/assert.c")],
  builtins: () => ({}),
  exports: () => exports,
};
