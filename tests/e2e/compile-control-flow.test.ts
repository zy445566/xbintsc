/**
 * End-to-end tests for control flow: exceptions, equality, labels and optional chains.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end control flow", (harness) => {
  const { runProgram } = harness;

  it("supports try/catch/finally including returns and loop breaks", () => {
    const source = `
      function safe(n: number): number {
        try {
          if (n < 0) throw "negative";
          return n * 2;
        } catch (err) {
          return -1;
        }
      }
      function cleanup(): string {
        let log = "";
        try {
          log += "try;";
          throw "boom";
        } catch (e) {
          log += "catch:" + e + ";";
        } finally {
          log += "finally";
        }
        return log;
      }
      console.log(safe(5), safe(-2), cleanup());
    `;
    expect(runProgram(source)).toBe("10 -1 try;catch:boom;finally");
  });

  it("runs finally blocks on early return, break and continue", () => {
    const source = `
      const events: string[] = [];
      function f(n: number): string {
        try {
          events.push("try" + n);
          if (n === 1) return "ret1";
          if (n === 2) throw "boom" + n;
        } catch (e) {
          events.push("catch" + e);
          if (n === 3) return "ret3";
        } finally {
          events.push("finally" + n);
        }
        return "done" + n;
      }
      console.log(f(0), f(1), f(2), f(3));
      console.log(events.join("|"));
      const loop: string[] = [];
      for (let i = 0; i < 5; i++) {
        try {
          if (i === 2) break;
          if (i === 4) continue;
          loop.push("i" + i);
        } finally {
          loop.push("F" + i);
        }
      }
      console.log(loop.join("|"));
      function nested(): string {
        try {
          try {
            return "inner";
          } finally {
            events.push("innerFinally");
          }
        } finally {
          events.push("outerFinally");
        }
      }
      console.log(nested(), events[events.length - 2], events[events.length - 1]);
    `;
    expect(runProgram(source)).toBe(
      [
        "done0 ret1 done2 done3",
        "try0|finally0|try1|finally1|try2|catchboom2|finally2|try3|finally3",
        "i0|F0|i1|F1|F2",
        "inner innerFinally outerFinally",
      ].join("\n"),
    );
  });

  it("implements abstract equality with ToPrimitive", () => {
    const source = `
      console.log(([] as any) == 0, ([] as any) == "", ([1] as any) == 1, ([1, 2] as any) == "1,2");
      console.log(({} as any) == "[object Object]");
      console.log(([1] as any) == [1], ([] as any) == false, ([] as any) == null);
      console.log(0 == "0", 0 == false, "" == false, null == undefined, null == 0, undefined == 0);
      console.log(NaN == NaN, (1 as any) == "1");
    `;
    expect(runProgram(source)).toBe(
      [
        "true true true true",
        "true",
        "false true false",
        "true true true true false false",
        "false true",
      ].join("\n"),
    );
  });

  it("supports labeled break and continue across nested loops", () => {
    const source = `
      const out: string[] = [];
      outer: for (let i = 0; i < 3; i++) {
        inner: for (let j = 0; j < 3; j++) {
          if (j === 1) continue outer;
          if (i === 2) break outer;
          out.push(i + "," + j);
        }
      }
      block: {
        out.push("before");
        if (out.length > 0) break block;
        out.push("unreachable");
      }
      out.push("after");
      function f(): number {
        let total = 0;
        loop: for (let i = 0; i < 5; i++) {
          try {
            if (i === 2) continue loop;
            if (i === 4) break loop;
            total += i;
          } finally {
            total += 100;
          }
        }
        return total;
      }
      console.log(out.join("|"), f());
    `;
    expect(runProgram(source)).toBe("0,0|1,0|before|after 504");
  });

  it("supports optional chaining", () => {
    const source = `
      const obj = { a: { b: 5 }, m: (x: number) => x + 1 };
      const empty: any = null;
      console.log(obj?.a?.b, empty?.a?.b, obj?.m?.(10), empty?.m?.(10));
      const arr = [1, 2, 3];
      console.log(arr?.length, arr?.map((x) => x * 2).join(","), empty?.length);
    `;
    expect(runProgram(source)).toBe("5 undefined 11 undefined\n3 2,4,6 undefined");
  });

  it("short-circuits a whole optional chain but throws past the guard", () => {
    const source = `
      const empty: any = null;
      const nested: any = { a: { b: null } };
      // A \`?.\` short-circuits every later member, so nothing past it is evaluated.
      console.log("A", empty?.a.b.c);
      console.log("B", (nested?.a)?.b);
      console.log("C", nested.a?.b?.c);
      // Once the chain has started, a non-optional member on null throws.
      try {
        console.log(nested?.b.c);
        console.log("D-ok");
      } catch (e) {
        console.log("D-threw");
      }
      try {
        console.log(nested.a?.b.c);
        console.log("E-ok");
      } catch (e) {
        console.log("E-threw");
      }
    `;
    expect(runProgram(source)).toBe("A undefined\nB null\nC undefined\nD-threw\nE-threw");
  });

  it("defaults Array.sort to string order and reveals circular references", () => {
    const source = `
      console.log([10, 1, 2, 20].sort().join(","));
      console.log(["banana", "apple", "cherry"].sort().join(","));
      console.log(JSON.stringify([3, undefined, 1, 2, undefined].sort()));
      console.log([5, 3, 8, 1].sort((a, b) => a - b).join(","));
      const o: any = { name: "x" };
      o.self = o;
      o.child = { parent: o };
      console.log(o);
      const arr: any = [1, 2];
      arr.push(arr);
      console.log(arr);
    `;
    expect(runProgram(source)).toBe(
      [
        "1,10,2,20",
        "apple,banana,cherry",
        "[1,2,3,null,null]",
        "1,3,5,8",
        "{ name: 'x', self: [Circular *1], child: { parent: [Circular *1] } }",
        "[ 1, 2, [Circular *1] ]",
      ].join("\n"),
    );
  });
});
