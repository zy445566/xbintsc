/**
 * Node's `async_hooks` module: `AsyncResource` (a constructible base class) and
 * the stateless async-id / hook stubs.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  AsyncResource: { symbol: "xt_async_resource_ctor", isConstructor: true },
  AsyncLocalStorage: { symbol: "xt_async_resource_ctor", isConstructor: true },
  executionAsyncId: { symbol: "xt_async_hooks_execution_id" },
  triggerAsyncId: { symbol: "xt_async_hooks_trigger_id" },
  createHook: { symbol: "xt_async_hooks_create_hook" },
};

export const asyncHooksModule: NodeModule = {
  name: "async_hooks",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/async_hooks/async_hooks.c")],
  builtins: () => ({}),
  exports: () => exports,
};
