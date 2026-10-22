import { it } from "vitest";
import { describeE2E } from "./harness.js";

/**
 * `async function*` and `for await...of` are backed by the same stackful
 * coroutine as generators and async functions. The body can await a timer and
 * still resume at the right yield, and every `.next()` request gets a promise.
 */
describeE2E("async generators and for await...of", (harness) => {
  it("yields across awaits and completes with a return value", () => {
    harness.expectSameOutputAsNode(`
      async function* count(limit: number) {
        for (let i = 0; i < limit; i++) {
          await Promise.resolve(i);
          yield i * 2;
        }
        return 99;
      }
      async function main() {
        const values: number[] = [];
        for await (const value of count(3)) values.push(value);
        const it = count(2);
        const first = await it.next();
        const second = await it.next();
        const done = await it.next();
        console.log(JSON.stringify({ values, first, second, done }));
      }
      main();
    `);
  });

  it("suspends on a timer between yields", () => {
    harness.expectSameOutputAsNode(`
      async function delay(ms: number) {
        return new Promise<void>((resolve) => setTimeout(resolve, ms));
      }
      async function* timed() {
        yield "a";
        await delay(2);
        yield "b";
      }
      async function main() {
        const log: string[] = [];
        setTimeout(() => log.push("timer"), 0);
        for await (const value of timed()) log.push("yield:" + value);
        console.log(log.join(","));
      }
      main();
    `);
  });

  it("supports throw, catch and finally inside the body", () => {
    harness.expectSameOutputAsNode(`
      async function* catching() {
        try {
          yield 1;
          yield 2;
        } catch (error) {
          yield "caught:" + (error as Error).message;
        } finally {
          yield "finally";
        }
      }
      async function main() {
        const it = catching();
        const out: string[] = [];
        out.push(JSON.stringify(await it.next()));
        out.push(JSON.stringify(await it.throw(new Error("boom"))));
        out.push(JSON.stringify(await it.next()));
        out.push(JSON.stringify(await it.next()));
        console.log(out.join("\\n"));
      }
      main();
    `);
  });

  it("iterates sync generators, arrays and custom async iterables", () => {
    harness.expectSameOutputAsNode(`
      function* syncValues() { yield 1; yield 2; }
      const custom = {
        [Symbol.asyncIterator]() {
          let i = 0;
          return {
            next() {
              i++;
              if (i <= 2) return Promise.resolve({ value: i * 10, done: false });
              return Promise.resolve({ value: undefined, done: true });
            },
          };
        },
      };
      async function main() {
        const out: number[] = [];
        for await (const value of syncValues()) out.push(value);
        for await (const value of [3, 4]) out.push(value);
        for await (const value of custom) out.push(value);
        console.log(out.join(","));
      }
      main();
    `);
  });

  it("keeps a suspended async generator and its await chain alive across collections", () => {
    harness.expectSameOutputAsNode(
      `
      async function* range(n: number) {
        const buf: number[] = [];
        for (let i = 0; i < n; i++) {
          const hold = { i, text: "v" + i };
          await Promise.resolve(i);
          buf.push(hold.i);
          yield buf.length;
        }
      }
      async function main() {
        let sum = 0;
        for await (const value of range(300)) {
          const garbage = "x".repeat(40) + value;
          sum += garbage.length;
        }
        console.log(sum);
      }
      main();
    `,
      { env: { XT_GC_THRESHOLD: "1" } },
    );
  });

  it("resolves queued next() calls in order", () => {
    harness.expectSameOutputAsNode(`
      async function* values() { yield 1; yield 2; }
      async function main() {
        const it = values();
        const first = it.next();
        const second = it.next();
        console.log(JSON.stringify(await first), JSON.stringify(await second));
      }
      main();
    `);
  });

  it("returns done from return() and rejects an escaped throw", () => {
    harness.expectSameOutputAsNode(`
      async function* values() { yield 1; }
      async function* failing() { yield 1; throw new Error("bad"); }
      async function main() {
        const it = values();
        const returned = await it.return(7);
        const after = await it.next();
        const f = failing();
        const first = await f.next();
        let message = "";
        try {
          await f.next();
        } catch (error) {
          message = (error as Error).message;
        }
        console.log(JSON.stringify({ returned, after, first, message }));
      }
      main();
    `);
  });
});
