/**
 * End-to-end tests for the timer globals (`setTimeout` / `setInterval` and the
 * matching `clear*` functions). They are implemented on the core event loop, so
 * they work without any extension and keep the process alive until no timer is
 * pending.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";
import { makeDifferential } from "./differential-helpers.js";

describeE2E("end-to-end timers", (harness) => {
  const { runProgram } = harness;
  const { diff } = makeDifferential(harness);

  diff(
    "orders timers after synchronous code and microtasks",
    `const order: string[] = [];
order.push("sync");
setTimeout(() => order.push("timeout0"), 0);
Promise.resolve().then(() => order.push("microtask"));
setTimeout(() => order.push("timeout5"), 5);
order.push("sync2");
setTimeout(() => console.log(order.join(",")), 20);`,
  );

  it("cancels a pending timeout", () => {
    const source = `
      const id = setTimeout(() => console.log("fired"), 10);
      clearTimeout(id);
      setTimeout(() => console.log("done"), 20);
    `;
    expect(runProgram(source)).toBe("done");
  });

  it("repeats an interval until cleared", () => {
    const source = `
      let n = 0;
      const iv = setInterval(() => {
        n++;
        console.log("tick", n);
        if (n === 3) clearInterval(iv);
      }, 1);
    `;
    expect(runProgram(source)).toBe("tick 1\ntick 2\ntick 3");
  });

  it("forwards extra arguments and supports first-class timer functions", () => {
    const source = `
      const schedule = setTimeout;
      schedule((a: number, b: number) => console.log(a + b), 1, 3, 4);
      console.log(typeof setTimeout, typeof clearInterval);
    `;
    expect(runProgram(source)).toBe("function function\n7");
  });

  it("stops an interval when cleared (clearTimeout also accepts an interval id)", () => {
    // A timer can only be *observed* to have stopped by checking that a later
    // window produces no further ticks, so the assertion compares the count at
    // the moment of `clearTimeout` against the count after another 15ms.
    const source = `
      let n = 0;
      const id = setInterval(() => { n++; }, 2);
      setTimeout(() => {
        clearTimeout(id);
        const atClear = n;
        setTimeout(() => console.log("stable", n === atClear), 15);
      }, 5);
    `;
    expect(runProgram(source)).toBe("stable true");
  });
});
