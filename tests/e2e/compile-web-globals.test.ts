/**
 * End-to-end tests for the Web platform globals the runtime provides:
 * `AbortController` / `AbortSignal`, `Headers`, `Response`, plus the
 * first-class `JSON`, `ArrayBuffer` and `DataView` values they rely on.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end compilation (web platform globals)", (harness) => {
  const { runProgram } = harness;

  it("aborts a signal and runs its listeners once", () => {
    const source = `
      const controller = new AbortController();
      const signal = controller.signal;
      console.log("initial", signal.aborted, signal.reason === undefined);
      let fired = 0;
      signal.addEventListener("abort", () => { fired += 1; });
      signal.addEventListener("abort", () => { fired += 10; }, { once: true });
      controller.abort();
      console.log("aborted", signal.aborted, signal.reason.name, fired);
      controller.abort();
      console.log("idempotent", fired);
      try { signal.throwIfAborted(); } catch (error) { console.log("threw", error.name); }
      console.log("instanceof", signal instanceof AbortSignal, controller instanceof AbortController);
    `;
    expect(runProgram(source)).toBe(
      [
        "initial false true",
        "aborted true AbortError 11",
        "idempotent 11",
        "threw AbortError",
        "instanceof true true",
      ].join("\n"),
    );
  });

  it("fires AbortSignal.timeout and follows AbortSignal.any", () => {
    const source = `
      async function main() {
        const timeout = AbortSignal.timeout(1);
        console.log("timeout initial", timeout.aborted);
        await new Promise((resolve) => setTimeout(resolve, 25));
        console.log("timeout fired", timeout.aborted, timeout.reason.name);
        const first = new AbortController();
        const second = new AbortController();
        const any = AbortSignal.any([first.signal, second.signal]);
        console.log("any initial", any.aborted);
        second.abort();
        console.log("any aborted", any.aborted, any.reason.name);
        const pre = AbortSignal.abort("custom");
        console.log("static abort", pre.aborted, pre.reason);
      }
      main();
    `;
    expect(runProgram(source)).toBe(
      ["timeout initial false", "timeout fired true TimeoutError", "any initial false", "any aborted true AbortError", "static abort true custom"].join(
        "\n",
      ),
    );
  });

  it("builds Headers and Response values", () => {
    const source = `
      const headers = new Headers({ "Content-Type": "application/json", "X-Test": "1" });
      console.log("get", headers.get("content-type"), headers.has("X-TEST"));
      headers.append("x-test", "2");
      console.log("append", headers.get("x-test"));
      headers.set("X-Test", "3");
      console.log("set", headers.get("x-test"));
      headers.delete("x-test");
      console.log("delete", headers.has("x-test"), headers instanceof Headers);
      const response = new Response("hello world", { status: 201, statusText: "Created", headers });
      console.log("response", response.status, response.ok, response.statusText);
      console.log("nested", response.headers instanceof Headers, response instanceof Response);
      response.text().then((text) => console.log("body", text));
      response.arrayBuffer().then((buffer) => console.log("bytes", buffer.byteLength));
    `;
    expect(runProgram(source)).toBe(
      [
        "get application/json true",
        "append 1, 2",
        "set 3",
        "delete false true",
        "response 201 true Created",
        "nested true true",
        "body hello world",
        "bytes 11",
      ].join("\n"),
    );
  });

  it("exposes JSON, ArrayBuffer and DataView as values", () => {
    const source = `
      const stringify = JSON.stringify;
      const parse = JSON.parse;
      console.log("json", typeof stringify, stringify({ a: 1 }), parse('{"b":2}').b);
      const Buffer = ArrayBuffer;
      console.log("arraybuffer", typeof Buffer.prototype, new Buffer(4).byteLength);
      console.log("dataview", typeof DataView.prototype);
      const bytes = new Uint8Array([1, 2, 3]);
      console.log("u8", Object.getPrototypeOf(Uint8Array.prototype) !== undefined, bytes.length);
    `;
    expect(runProgram(source)).toBe(
      ["json function {\"a\":1} 2", "arraybuffer object 4", "dataview object", "u8 true 3"].join("\n"),
    );
  });
});
