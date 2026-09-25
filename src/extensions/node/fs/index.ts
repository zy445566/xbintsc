/**
 * Node's `fs` module: synchronous file and directory operations backed by
 * `runtime/ext_node/fs`. Asynchronous callbacks are not supported (xbintsc has
 * no event loop), so the API is the `*Sync` surface plus `watch`/`watchFile`
 * (which return API-shaped watchers that never emit).
 *
 * `fs.constants` and `fs.promises` are exposed as value getters, so both
 * `import { constants } from "fs"` and the `fs.constants.F_OK` property form
 * work.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";
import { readFileBuiltins } from "./read-file.js";
import { writeFileBuiltins } from "./write-file.js";
import { fsOpsBuiltins } from "./fs-ops.js";
import { fsStreamBuiltins } from "./streams.js";

const builtins = { ...readFileBuiltins, ...writeFileBuiltins, ...fsOpsBuiltins, ...fsStreamBuiltins };

const exports: ModuleExports = {
  ...builtins,
  constants: { valueSymbol: "xt_fs_constants" },
  promises: { valueSymbol: "xt_fs_promises" },
};

export const fsModule: NodeModule = {
  name: "fs",
  runtimeSources: () => [
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/read_file.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/write_file.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/fs_ops.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/fd_ops.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/meta_ops.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/link_ops.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/copy_ops.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/dir.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/glob.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/watch.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/constants.c"),
    resolveFrom(import.meta.url, "../../../../runtime/ext_node/fs/streams.c"),
  ],
  builtins: () => builtins,
  exports: () => exports,
};
