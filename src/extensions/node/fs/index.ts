/**
 * Node's `fs` module: synchronous file and directory operations backed by
 * `runtime/ext_node/fs`. Asynchronous callbacks are not supported (xbintsc has
 * no event loop), so only the `*Sync` API is exposed.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import { readFileBuiltins } from "./read-file.js";
import { writeFileBuiltins } from "./write-file.js";
import { fsOpsBuiltins } from "./fs-ops.js";

export const fsModule: NodeModule = {
  name: "fs",
  runtimeSources: () => [
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/read_file.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/write_file.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/fs_ops.c"),
  ],
  builtins: () => ({ ...readFileBuiltins, ...writeFileBuiltins, ...fsOpsBuiltins }),
};
