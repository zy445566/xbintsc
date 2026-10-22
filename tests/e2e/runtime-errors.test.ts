/**
 * Runtime-internal failures must be observable as real Error values.
 *
 * Every runtime helper used to throw a bare `"TypeError: ..."` string, so a
 * user `catch (e)` saw `typeof e === "string"`, `e.message === undefined` and
 * `e instanceof Error === false` even though `new Error(...)` worked. This
 * suite pins the Error-family behavior and, just as importantly, that a
 * user-thrown string is still thrown unchanged.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("runtime errors are Error instances", (harness) => {
  it("throws Error-family objects from internal runtime failures", () => {
    harness.expectSameOutputAsNode(`
      try {
        const value: any = null;
        value.member;
      } catch (e: any) {
        console.log("null-read", e instanceof Error, e instanceof TypeError, e.name, e.message);
        console.log("stack-head", e.stack.split("\\n")[0]);
      }

      try {
        new Array(-1);
      } catch (e: any) {
        console.log("array-length", e instanceof Error, e instanceof RangeError, e.name, e.message);
      }

      try {
        const value: any = undefined;
        value();
      } catch (e: any) {
        console.log("not-callable", e instanceof TypeError, e.name, e.message);
      }
    `);
  });

  it("leaves a user-thrown value untouched", () => {
    harness.expectSameOutputAsNode(`
      try {
        throw "boom";
      } catch (e: any) {
        console.log("string", typeof e, e);
      }
      try {
        throw new Error("user");
      } catch (e: any) {
        console.log("error", e instanceof Error, e.name, e.message);
      }
    `);
  });
});
