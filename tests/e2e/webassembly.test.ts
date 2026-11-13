/**
 * WebAssembly execution and `.wasm` asset imports.
 *
 * The byte arrays are tiny fixed modules produced by clang/wasm-ld and are
 * embedded as base64 so the test needs no wasm toolchain, only the pinned
 * wasm3 interpreter that the `wasm` link feature compiles.
 */

import { writeFileSync } from "node:fs";
import { join } from "node:path";
import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

const ADD = "AGFzbQEAAAABDgNgAABgAn9/AX9gAAF/AwQDAAECBQMBAAIGKwd/AUGAiAQLfwBBgAgLfwBBgAgLfwBBgAgLfwBBgIgEC38AQQALfwBBAQsHhQEKBm1lbW9yeQIAEV9fd2FzbV9jYWxsX2N0b3JzAAADYWRkAAEFZ2V0NDIAAgxfX2Rzb19oYW5kbGUDAQpfX2RhdGFfZW5kAwINX19nbG9iYWxfYmFzZQMDC19faGVhcF9iYXNlAwQNX19tZW1vcnlfYmFzZQMFDF9fdGFibGVfYmFzZQMGChEDAgALBwAgASAAagsEAEEqCwA7BG5hbWUBIAMAEV9fd2FzbV9jYWxsX2N0b3JzAQNhZGQCBWdldDQyBxIBAA9fX3N0YWNrX3BvaW50ZXIAJglwcm9kdWNlcnMBDHByb2Nlc3NlZC1ieQEFY2xhbmcGMTUuMC4y";
const MATH = "AGFzbQEAAAABDwNgAABgAXwBfGACfn4BfgMEAwABAgUDAQACBisHfwFBgIgEC38AQYAIC38AQYAIC38AQYAIC38AQYCIBAt/AEEAC38AQQELB4UBCgZtZW1vcnkCABFfX3dhc21fY2FsbF9jdG9ycwAABGhhbGYAAQRwYWlyAAIMX19kc29faGFuZGxlAwEKX19kYXRhX2VuZAMCDV9fZ2xvYmFsX2Jhc2UDAwtfX2hlYXBfYmFzZQMEDV9fbWVtb3J5X2Jhc2UDBQxfX3RhYmxlX2Jhc2UDBgoeAwIACw4AIABEAAAAAAAA4D+iCwoAIABCAYYgAXwLADsEbmFtZQEgAwARX193YXNtX2NhbGxfY3RvcnMBBGhhbGYCBHBhaXIHEgEAD19fc3RhY2tfcG9pbnRlcgAmCXByb2R1Y2VycwEMcHJvY2Vzc2VkLWJ5AQVjbGFuZwYxNS4wLjI=";
const MEMORY = "AGFzbQEAAAABEgRgAABgAAF/YAF/AX9gAn9/AAMFBAABAgMFAwEAAgYrB38BQZCIBAt/AEGACAt/AEGICAt/AEGACAt/AEGQiAQLfwBBAAt/AEEBCweYAQsGbWVtb3J5AgARX193YXNtX2NhbGxfY3RvcnMAAAVmaXJzdAABCXJlYWRfYnl0ZQACCndyaXRlX2J5dGUAAwxfX2Rzb19oYW5kbGUDAQpfX2RhdGFfZW5kAwINX19nbG9iYWxfYmFzZQMDC19faGVhcF9iYXNlAwQNX19tZW1vcnlfYmFzZQMFDF9fdGFibGVfYmFzZQMGCjAEAgALCwBBACwAgIiAgAALDgAgAEGAiICAAGosAAALEAAgAEGAiICAAGogAToAAAsLDwEAQYAICwhoZWxsbwAAAABXBG5hbWUBMgQAEV9fd2FzbV9jYWxsX2N0b3JzAQVmaXJzdAIJcmVhZF9ieXRlAwp3cml0ZV9ieXRlBxIBAA9fX3N0YWNrX3BvaW50ZXIJCAEABS5kYXRhACYJcHJvZHVjZXJzAQxwcm9jZXNzZWQtYnkBBWNsYW5nBjE1LjAuMg==";
const IMPORT = "AGFzbQEAAAABDgNgAn9/AX9gAX8AYAAAAhUCA2VudgNhZGQAAANlbnYDbG9nAAEDAwICAgUDAQACBisHfwFBgIgEC38AQYAIC38AQYAIC38AQYAIC38AQYCIBAt/AEEAC38AQQELB30JBm1lbW9yeQIAEV9fd2FzbV9jYWxsX2N0b3JzAAIDcnVuAAMMX19kc29faGFuZGxlAwEKX19kYXRhX2VuZAMCDV9fZ2xvYmFsX2Jhc2UDAwtfX2hlYXBfYmFzZQMEDV9fbWVtb3J5X2Jhc2UDBQxfX3RhYmxlX2Jhc2UDBgoXAgIACxIAQQJBAxCAgICAABCBgICAAAsAPgRuYW1lASMEAANhZGQBA2xvZwIRX193YXNtX2NhbGxfY3RvcnMDA3J1bgcSAQAPX19zdGFja19wb2ludGVyACYJcHJvZHVjZXJzAQxwcm9jZXNzZWQtYnkBBWNsYW5nBjE1LjAuMg==";

/** TS source that decodes one embedded module into a `Uint8Array`. */
function bytesExpression(name: string, base64: string): string {
  return (
    `const ${name} = (() => {\n` +
    `  const text = atob("${base64}");\n` +
    "  const bytes = new Uint8Array(text.length);\n" +
    "  for (let i = 0; i < text.length; i++) bytes[i] = text.charCodeAt(i);\n" +
    "  return bytes;\n" +
    "})();\n"
  );
}

describeE2E("WebAssembly", (harness) => {
  it("validates, compiles and calls exported functions", () => {
    const output = harness.runProgram(`
      ${bytesExpression("bytes", ADD)}

      async function main(): Promise<void> {
        console.log("validate", WebAssembly.validate(bytes));
        const module = await WebAssembly.compile(bytes);
        const instance = await WebAssembly.instantiate(module, {});
        console.log("add", instance.exports.add(2, 3));
        console.log("get42", instance.exports.get42());
        console.log("metadata", WebAssembly.Module.exports(module).length > 0,
                    WebAssembly.Module.imports(module).length);
        const direct = new WebAssembly.Instance(module, {});
        console.log("again", direct.exports.add(4, 5));
      }
      main();
    `);
    expect(output).toBe(
      ["validate true", "add 5", "get42 42", "metadata true 0", "again 9"].join("\n"),
    );
  });

  it("passes i64 and f64 values across the boundary", () => {
    const output = harness.runProgram(`
      ${bytesExpression("bytes", MATH)}

      async function main(): Promise<void> {
        const result = await WebAssembly.instantiate(bytes, {});
        console.log("half", result.instance.exports.half(3.5));
        console.log("pair", result.instance.exports.pair(2n, 3n).toString());
      }
      main();
    `);
    expect(output).toBe(["half 1.75", "pair 7"].join("\n"));
  });

  it("bridges JS imports and keeps exported memory in sync", () => {
    const output = harness.runProgram(`
      ${bytesExpression("memoryBytes", MEMORY)}
      ${bytesExpression("importBytes", IMPORT)}

      async function main(): Promise<void> {
        const instance = (await WebAssembly.instantiate(memoryBytes, {})).instance;
        const memory = instance.exports.memory;
        console.log("first", instance.exports.first());
        const view = new Uint8Array(memory.buffer);
        instance.exports.write_byte(0, 65);
        console.log("out", view[1024]);
        view[1024] = 66;
        console.log("in", instance.exports.first());

        const imported = (await WebAssembly.instantiate(importBytes, {
          env: {
            add: (a: number, b: number) => a + b,
            log: (value: number) => console.log("log", value),
          },
        })).instance;
        imported.exports.run();
      }
      main();
    `);
    expect(output).toBe(["first 104", "out 65", "in 66", "log 5"].join("\n"));
  });

  it("reports compile errors as WebAssembly.CompileError", () => {
    const output = harness.runProgram(`
      try {
        new WebAssembly.Module(new Uint8Array([0, 1, 2]));
      } catch (error) {
        console.log("compile", error instanceof WebAssembly.CompileError,
                    error instanceof Error, (error as Error).name);
      }
      console.log("invalid", WebAssembly.validate(new Uint8Array([0, 1, 2])));
      console.log("empty", WebAssembly.validate(new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0])));
    `);
    expect(output).toBe(["compile true true CompileError", "invalid false", "empty true"].join("\n"));
  });

  it("imports a .wasm file as an embedded Uint8Array", () => {
    writeFileSync(join(harness.workdir, "embedded.wasm"), Buffer.from(ADD, "base64"));
    const output = harness.runProgram(`
      import bytes from "./embedded.wasm";

      async function main(): Promise<void> {
        console.log("bytes", bytes.length, bytes[0]);
        const result = await WebAssembly.instantiate(bytes, {});
        console.log("sum", result.instance.exports.add(20, 22));
      }
      main();
    `);
    expect(output).toBe(["bytes 336 0", "sum 42"].join("\n"));
  });
});
