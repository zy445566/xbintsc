/**
 * Node.js compatibility extension.
 *
 * Registering it links the C sources under `runtime/ext_node/` and exposes the
 * builtins each module implements, so `readFileSync("...")` in TypeScript
 * resolves to the C implementation without the core compiler knowing anything
 * about Node. Every Node module lives in its own subfolder next to this file
 * (currently `fs/`), with the runtime counterpart under `runtime/ext_node/`.
 */

import type { BuiltinFunction, Extension } from "../registry.js";
import type { NodeModule } from "./module.js";
import { fsModule } from "./fs/index.js";

/** Every Node module the extension currently provides. */
const modules: readonly NodeModule[] = [fsModule];

export const nodeExtension: Extension = {
  name: "node",
  description: "Node.js host APIs (fs, ...)",
  runtimeSources: () => modules.flatMap((module) => module.runtimeSources()),
  builtins: () => {
    const merged: Record<string, BuiltinFunction> = {};
    for (const module of modules) Object.assign(merged, module.builtins());
    return merged;
  },
};
