/**
 * Node's `test` module (`node:test`).
 *
 * A minimal synchronous test runner: `test(name, fn)` executes `fn` immediately,
 * catching assertion failures and reporting TAP output. `import test from
 * "node:test"` binds the callable default export; `import { test }` and
 * `test.skip` / `test.todo` / `test.only` are also provided.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const runTest = { symbol: "xt_node_test" } as const;

const exports: ModuleExports = {
  default: runTest,
  test: runTest,
  it: runTest,
  describe: runTest,
  skip: { symbol: "xt_node_test_skip" },
  todo: { symbol: "xt_node_test_todo" },
  only: { symbol: "xt_node_test" },
  before: { symbol: "xt_node_test_hook" },
  after: { symbol: "xt_node_test_hook" },
  beforeEach: { symbol: "xt_node_test_hook" },
  afterEach: { symbol: "xt_node_test_hook" },
};

export const testModule: NodeModule = {
  name: "test",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/test/test.c")],
  builtins: () => ({}),
  exports: () => exports,
};
