/**
 * Node's deprecated `constants` module: the file-system flags older packages
 * read (`O_RDONLY` / `O_WRONLY` / `O_SYMLINK`, the `F_OK` access checks, ...).
 */

import type { NodeModule } from "../module.js";
import { resolveFrom } from "../module.js";
import type { ModuleExports } from "../../registry.js";

const names: readonly string[] = [
  "F_OK",
  "R_OK",
  "W_OK",
  "X_OK",
  "O_RDONLY",
  "O_WRONLY",
  "O_RDWR",
  "O_CREAT",
  "O_EXCL",
  "O_TRUNC",
  "O_APPEND",
  "O_SYMLINK",
];

const exports: ModuleExports = Object.fromEntries(
  names.map((name) => [name, { valueSymbol: `xt_constants_${name.toLowerCase()}` }]),
);

export const constantsModule: NodeModule = {
  name: "constants",
  runtimeSources: () => [resolveFrom(import.meta.url, "../../../../runtime/ext_node/constants/constants.c")],
  builtins: () => ({}),
  exports: () => exports,
};
