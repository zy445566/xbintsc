/**
 * `.wasm` asset loader: embed an imported WebAssembly binary as a `Uint8Array`.
 *
 * The bundler passes binary loaders a byte string (one Latin-1 code unit per
 * byte) so the module's bytes are not mangled by UTF-8 decoding. The generated
 * module decodes base64 at startup and exports the typed array, which is
 * exactly what `WebAssembly.compile` / `instantiate` accept:
 *
 *   import bytes from "./add.wasm";
 *   const { instance } = await WebAssembly.instantiate(bytes);
 *
 * This keeps the binary inside the executable — the compiled program needs no
 * file at run time.
 */

import type { AssetLoader } from "./registry.js";

const loadWasmBytes: AssetLoader = (_path, source) => {
  const base64 = Buffer.from(source, "latin1").toString("base64");
  const moduleSource =
    `const __xt_wasm_text = atob("${base64}");\n` +
    "const __xt_wasm_bytes = new Uint8Array(__xt_wasm_text.length);\n" +
    "for (let __xt_wasm_index = 0; __xt_wasm_index < __xt_wasm_text.length; __xt_wasm_index++) {\n" +
    "  __xt_wasm_bytes[__xt_wasm_index] = __xt_wasm_text.charCodeAt(__xt_wasm_index);\n" +
    "}\n" +
    "export default __xt_wasm_bytes;\n";
  return { moduleSource };
};

export const loadWasmAsset: AssetLoader = Object.assign(loadWasmBytes, {
  binary: true as const,
});
