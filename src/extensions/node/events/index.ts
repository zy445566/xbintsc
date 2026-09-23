/**
 * Node's `events` module.
 *
 * Exposes the standalone `EventEmitter` class plus its statics. `EventEmitter`
 * is provided both as a global constructor and as a named export, so
 * `new EventEmitter()` and `import { EventEmitter } from "events"` both work.
 * The implementation lives in `runtime/ext_node/events/events.c`.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const methods = ["listenerCount", "getEventListeners", "getMaxListeners", "setMaxListeners", "once", "addAbortListener"] as const;

const exports: ModuleExports = {
  EventEmitter: { symbol: "xt_event_emitter_ctor", isConstructor: true },
  ...Object.fromEntries(methods.map((method) => [method, { namespace: "events", method }])),
};

export const eventsModule: NodeModule = {
  name: "events",
  namespace: "events",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/events/events.c")],
  builtins: () => ({}),
  exports: () => exports,
};
