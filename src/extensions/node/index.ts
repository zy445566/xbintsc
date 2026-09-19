/**
 * Node.js compatibility extension.
 *
 * Registering it links the C sources under `runtime/ext_node/` and exposes the
 * modules each implements, so `import { readFileSync } from "fs"` in TypeScript
 * resolves to the C implementation without the core compiler knowing anything
 * about Node. Every Node module lives in its own subfolder next to this file
 * (`fs/`, `path/`, `os/`, `process/`, `buffer/`, `stream/`, `net/`, `dgram/`,
 * `http/`, `fs-promises/`), with the runtime counterpart under
 * `runtime/ext_node/`.
 */

import type { Extension, ExtensionModule } from "../registry.js";
import type { NodeModule } from "./module.js";
import { fsModule } from "./fs/index.js";
import { fsPromisesModule } from "./fs-promises/index.js";
import { pathModule } from "./path/index.js";
import { osModule } from "./os/index.js";
import { processModule } from "./process/index.js";
import { bufferModule } from "./buffer/index.js";
import { streamModule } from "./stream/index.js";
import { netModule } from "./net/index.js";
import { dgramModule } from "./dgram/index.js";
import { httpModule } from "./http/index.js";

/** Every Node module the extension currently provides. */
const modules: readonly NodeModule[] = [
  fsModule,
  fsPromisesModule,
  pathModule,
  osModule,
  processModule,
  bufferModule,
  streamModule,
  netModule,
  dgramModule,
  httpModule,
];

export const nodeExtension: Extension = {
  name: "node",
  description: "Node.js host APIs (fs, path, os, process, buffer, stream, net, dgram, http)",
  // `http` reuses the `net` sources; de-duplicate so each C file links once.
  runtimeSources: () => [...new Set(modules.flatMap((module) => module.runtimeSources()))],
  // ws2_32 is linked unconditionally by the driver (the core event loop uses
  // winsock on Windows), so the extension does not add it again.
  // Node APIs are reached through `import ... from "fs"` (or the `node:`
  // scheme) rather than as bare globals, so each module is exposed for the
  // code generator to resolve imported bindings against.
  modules: () => {
    const exposed: Record<string, ExtensionModule> = {};
    for (const module of modules) {
      const entry: ExtensionModule = {
        namespace: module.namespace,
        exports: module.exports ? module.exports() : module.builtins(),
      };
      exposed[module.name] = entry;
      exposed[`node:${module.name}`] = entry;
    }
    return exposed;
  },
};
