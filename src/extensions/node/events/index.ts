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

const eventEmitter: ModuleExports[string] = {
  symbol: "xt_event_emitter_ctor",
  isConstructor: true,
  // `class X extends EventEmitter` chains onto this prototype, so the
  // inherited listener API is reachable (and `x instanceof EventEmitter`
  // holds when the same constructor value is compared).
  prototypeSymbol: "xt_events_prototype",
};

const exports: ModuleExports = {
  EventEmitter: eventEmitter,
  // Node's `events` module exports the EventEmitter constructor itself, so
  // `require("node:events")` and `import events from "node:events"` are the
  // callable class, not a namespace object. Code that does
  // `class Dispatcher extends require("node:events")` (undici) depends on it.
  default: eventEmitter,
  defaultMaxListeners: { valueSymbol: "xt_events_default_max_listeners" },
  ...Object.fromEntries(methods.map((method) => [method, { namespace: "events", method }])),
};

export const eventsModule: NodeModule = {
  name: "events",
  namespace: "events",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/events/events.c")],
  builtins: () => ({}),
  exports: () => exports,
};
