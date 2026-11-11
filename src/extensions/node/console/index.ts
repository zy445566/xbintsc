/**
 * Node's `console` module: the `Console` constructor plus the console methods
 * as named exports.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const voidMethods: readonly string[] = ["log", "info", "warn", "error", "debug"];
const valueMethods: readonly string[] = [
  "dir",
  "trace",
  "assert",
  "count",
  "countReset",
  "group",
  "groupEnd",
  "table",
  "time",
  "timeEnd",
  "timeLog",
];

const exports: ModuleExports = {
  Console: { symbol: "xt_console_ctor", isConstructor: true },
  ...Object.fromEntries(
    voidMethods.map((method) => [method, { symbol: `xt_console_${method === "debug" ? "log" : method}`, returnVoid: true }]),
  ),
  ...Object.fromEntries(
    valueMethods.map((method) => [method, { symbol: `xt_console_${snakeCase(method)}` }]),
  ),
};

function snakeCase(name: string): string {
  return name.replace(/([a-z])([A-Z])/g, "$1_$2").toLowerCase();
}

export const consoleModule: NodeModule = {
  name: "console",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/console/console.c")],
  builtins: () => ({}),
  exports: () => exports,
};
