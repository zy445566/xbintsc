/**
 * Node's `readline` module.
 *
 * `createInterface` (and the `Interface` constructor) return a line reader over
 * standard input. The implementation offloads the blocking stdin read to the
 * runtime's async I/O pool, so `question` and `on("line")` do not block timers
 * or the event loop; it lives in
 * `runtime/ext_node/readline/readline.c`. The cursor helpers write the same
 * ANSI sequences Node emits.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const exports: ModuleExports = {
  createInterface: { symbol: "xt_readline_create_interface" },
  Interface: { symbol: "xt_readline_interface_ctor", isConstructor: true },
  cursorTo: { symbol: "xt_readline_cursor_to" },
  moveCursor: { symbol: "xt_readline_move_cursor" },
  clearLine: { symbol: "xt_readline_clear_line" },
  clearScreenDown: { symbol: "xt_readline_clear_screen_down" },
  emitKeypressEvents: { symbol: "xt_readline_emit_keypress_events" },
};

export const readlineModule: NodeModule = {
  name: "readline",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/readline/readline.c")],
  builtins: () => ({}),
  exports: () => exports,
};
