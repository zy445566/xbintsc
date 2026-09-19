/**
 * Node's `fs` module: text file reads backed by `runtime/ext_node/fs`.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import { readFileBuiltins } from "./read-file.js";

export const fsModule: NodeModule = {
  name: "fs",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/read_file.c")],
  builtins: () => readFileBuiltins,
};
