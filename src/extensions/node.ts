/**
 * Node.js compatibility extension.
 *
 * Registering it links `runtime/ext_node.c` and exposes the builtins it
 * implements, so `readFileSync("...")` in TypeScript resolves to the C
 * implementation without the core compiler knowing anything about Node.
 */

import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";
import type { Extension } from "./registry.js";

const here = dirname(fileURLToPath(import.meta.url));

export const nodeExtension: Extension = {
  name: "node",
  description: "Node.js host APIs (readFileSync, ...)",
  runtimeSources: () => [resolve(here, "../../runtime/ext_node.c")],
  builtins: () => ({
    readFileSync: { symbol: "xt_node_read_text_file" },
    readTextFile: { symbol: "xt_node_read_text_file" },
  }),
};
