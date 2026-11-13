/**
 * End-to-end tests for the `node:crypto` random and PBKDF2 surface.
 *
 * The global Web Crypto face is covered by `crypto-random.test.ts`; these cases
 * exercise the module exports, their synchronous callback style, the buffer /
 * typed-array argument paths and the range errors.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("node:crypto random", (harness) => {
  const { runProgram } = harness;

  it("fills buffers and generates integers in both call styles", () => {
    const source = `
      import { randomBytes, randomFillSync, randomFill, randomInt, randomUUID } from "node:crypto";
      const first = randomBytes(16);
      console.log("bytes", first.length, first.equals(randomBytes(16)));
      console.log("zero", randomBytes(0).length);
      const filled = randomFillSync(new Uint8Array(8));
      let nonzero = false;
      for (const byte of filled) if (byte !== 0) nonzero = true;
      console.log("fill", filled.length, nonzero);
      const view = new Uint16Array(4);
      randomFillSync(view, 1, 2);
      randomFillSync(new Uint8Array(4), 10);
      console.log("view", view.length);
      console.log("int", randomInt(10) < 10, randomInt(5, 10) >= 5, randomInt(7) < 7);
      const uuid = randomUUID();
      console.log("uuid", uuid.length, uuid.charAt(14));
      let calls = 0;
      randomBytes(4, (error, buffer) => {
        calls++;
        console.log("cbytes", error === null, buffer.length);
      });
      randomFill(new Uint8Array(5), (error, buffer) => {
        calls++;
        console.log("cfill", error === null, buffer.length);
      });
      randomFill(new Uint8Array(6), 1, 2, (error, buffer) => {
        calls++;
        console.log("cfill-range", error === null, buffer.length);
      });
      randomInt(10, (error, value) => {
        calls++;
        console.log("cint", error === null, value >= 0 && value < 10);
      });
      randomInt(2, 8, (error, value) => {
        calls++;
        console.log("cint-range", error === null, value >= 2 && value < 8);
      });
      console.log("calls", calls);
      try {
        randomBytes(-1);
      } catch (error) {
        console.log("negative", error instanceof RangeError);
      }
      try {
        randomBytes(2147483648);
      } catch (error) {
        console.log("huge", error instanceof RangeError);
      }
      try {
        randomFillSync({} as any);
      } catch (error) {
        console.log("fill-type", error instanceof TypeError);
      }
      try {
        (randomInt as any)();
      } catch (error) {
        console.log("int-args", error instanceof TypeError);
      }
      try {
        randomInt(10, 5);
      } catch (error) {
        console.log("int-range", error instanceof RangeError);
      }
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      [
        "bytes 16 false",
        "zero 0",
        "fill 8 true",
        "view 4",
        "int true true true",
        "uuid 36 4",
        "cbytes true 4",
        "cfill true 5",
        "cfill-range true 6",
        "cint true true",
        "cint-range true true",
        "calls 5",
        "negative true",
        "huge true",
        "fill-type true",
        "int-args true",
        "int-range true",
      ].join("\n"),
    );
  });

  it("derives PBKDF2 keys across blocks and rejects bad arguments", () => {
    const source = `
      import { pbkdf2, pbkdf2Sync } from "node:crypto";
      const key = pbkdf2Sync("password", "salt", 10, 32, "sha256");
      console.log("key", key.length, key.toString("hex").length);
      console.log("default", pbkdf2Sync("password", "salt", 10, 32).equals(key));
      console.log("long", pbkdf2Sync("p".repeat(80), "salt", 2, 100).length);
      console.log("zero", pbkdf2Sync("p", "s", 1, 0).length);
      console.log("typed", pbkdf2Sync(new Uint8Array([1, 2, 3]), new Uint8Array([4, 5]), 2, 16).length);
      let calls = 0;
      pbkdf2("password", "salt", 2, 16, "sha256", (error, derived) => {
        calls++;
        console.log("callback", error === null, derived.length);
      });
      console.log("calls", calls);
      try {
        (pbkdf2Sync as any)("p", "s", 1);
      } catch (error) {
        console.log("args", error instanceof TypeError);
      }
      try {
        (pbkdf2Sync as any)(null, "s", 1, 16);
      } catch (error) {
        console.log("bytes", error instanceof TypeError);
      }
      try {
        pbkdf2Sync("p", "s", 0, 16);
      } catch (error) {
        console.log("iterations", error instanceof RangeError);
      }
      try {
        pbkdf2Sync("p", "s", 1, 16, "md5");
      } catch (error) {
        console.log("digest", (error as Error).message.includes("unsupported"));
      }
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      [
        "key 32 64",
        "default true",
        "long 100",
        "zero 0",
        "typed 16",
        "callback true 16",
        "calls 1",
        "args true",
        "bytes true",
        "iterations true",
        "digest true",
      ].join("\n"),
    );
  });
});
