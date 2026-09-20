/**
 * Node's `url` module: `pathToFileURL` / `fileURLToPath`, backed by
 * `runtime/ext_node/url`. These are the helpers the compiler uses to relate
 * `import.meta.url`, `process.argv[1]` and filesystem paths.
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { BuiltinFunction, ModuleExports } from "../../registry.js";

const builtins: Record<string, BuiltinFunction> = {
  pathToFileURL: { symbol: "xt_url_path_to_file_url" },
  fileURLToPath: { symbol: "xt_url_file_url_to_path" },
};

const exports: ModuleExports = {
  pathToFileURL: { symbol: "xt_url_path_to_file_url" },
  fileURLToPath: { symbol: "xt_url_file_url_to_path" },
};

export const urlModule: NodeModule = {
  name: "url",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/url/url.c")],
  builtins: () => builtins,
  exports: () => exports,
};
