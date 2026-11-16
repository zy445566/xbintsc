/**
 * Promise-heavy code under a tiny GC threshold.
 *
 * The collector reaches promises, coroutines and generator frames through
 * *typed* pointers (`reaction->promise`, `g_active_async`, …), not through
 * values on the stack. Those pointers used to be dereferenced without checking
 * that they name a live block: one stale pointer crashed the mark phase at a
 * garbage address (`0xffffffffffffffff` in the pi build). This test keeps the
 * promise/async bookkeeping exercised while collections run constantly.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end compilation (GC and promises)", (harness) => {
  const { runProgram } = harness;

  it("keeps promise chains correct while collecting constantly", () => {
    const source = `
      function step(value: number): Promise<number> {
        return new Promise<number>((resolve) => { setTimeout(() => resolve(value + 1), 1); });
      }
      async function run(): Promise<void> {
        let total = 0;
        for (let i = 0; i < 40; i++) total += await step(i);
        let filler = 0;
        for (let i = 0; i < 4000; i++) filler += String(i).length;
        console.log("sum", total, "filler", filler > 0);
      }
      run();
    `;
    expect(runProgram(source, { env: { XT_GC_THRESHOLD: "65536" } })).toBe("sum 820 filler true");
  });

  it("keeps settled and rejected promises alive across collections", () => {
    const source = `
      const settled: Promise<string>[] = [];
      for (let i = 0; i < 60; i++) settled.push(Promise.resolve("v" + i));
      const rejected: Promise<string>[] = [];
      for (let i = 0; i < 30; i++) {
        const promise = Promise.reject(new Error("e" + i));
        promise.catch((): void => {});
        rejected.push(promise);
      }
      async function run(): Promise<void> {
        const values = await Promise.all(settled);
        let filler = 0;
        for (let i = 0; i < 4000; i++) filler += String(i).length;
        console.log("count", values.length, values[59], rejected.length, filler > 0);
      }
      run();
    `;
    expect(runProgram(source, { env: { XT_GC_THRESHOLD: "65536" } })).toBe("count 60 v59 30 true");
  });
});
