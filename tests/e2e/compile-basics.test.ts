/**
 * End-to-end tests for core expressions, statements, functions and objects.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end basics", (harness) => {
  const { runProgram } = harness;

  it("evaluates arithmetic and prints results", () => {
    expect(runProgram('console.log("sum", 2 + 3 * 4);')).toBe("sum 14");
  });

  it("interpolates template literals", () => {
    const source = `
      const name = "world";
      console.log(\`Hello, \${name}!\`);
      console.log(\`\${1 + 1}-\${2 + 2}\`, \`plain\`, "[" + \`\` + "]");
    `;
    expect(runProgram(source)).toBe("Hello, world!\n2-4 plain []");
  });

  it("runs recursive functions", () => {
    const source = `
      function fib(n: number): number {
        if (n < 2) return n;
        return fib(n - 1) + fib(n - 2);
      }
      console.log(fib(10));
    `;
    expect(runProgram(source)).toBe("55");
  });

  it("supports loops, arrays and string concatenation", () => {
    const source = `
      const numbers = [1, 2, 3, 4];
      let total = 0;
      for (const n of numbers) {
        total += n;
      }
      console.log("total = " + total);
    `;
    expect(runProgram(source)).toBe("total = 10");
  });

  it("prints objects and arrays with JS-like formatting", () => {
    const source = `
      const user = { name: "ada", tags: ["math", "code"], age: 36 };
      console.log(user);
    `;
    expect(runProgram(source)).toBe("{ name: 'ada', tags: [ 'math', 'code' ], age: 36 }");
  });

  it("orders object keys like JavaScript", () => {
    const source = `
      console.log(JSON.stringify({ b: 1, a: 2, 10: 3, 2: 4 }));
      const o: any = {};
      o.z = 1; o[5] = 2; o.a = 3; o[1] = 4; o["01"] = 5; o[100] = 6;
      console.log(Object.keys(o).join(" "));
      let seen = "";
      for (const k in o) seen += k + ",";
      console.log(seen);
      console.log(JSON.stringify(Object.fromEntries([["b", 1], ["2", 2], ["a", 3]])));
    `;
    expect(runProgram(source)).toBe(
      [
        '{"2":4,"10":3,"b":1,"a":2}',
        "1 5 100 z a 01",
        "1,5,100,z,a,01,",
        '{"2":2,"b":1,"a":3}',
      ].join("\n"),
    );
  });

  it("supports switch statements with fall-through", () => {
    const source = `
      function size(n: number): string {
        switch (n) {
          case 0:
            return "none";
          case 1:
          case 2:
            return "few";
          default:
            return "many";
        }
      }
      console.log(size(0), size(1), size(2), size(7));
    `;
    expect(runProgram(source)).toBe("none few few many");
  });

  it("parses large integer literals without 32-bit clamping", () => {
    // Regression: `parseInt` used to go through `strtol`, whose `long` is only
    // 32 bits on Windows, so hex literals above INT32_MAX (used by the
    // compiler's own `hashText`) were clamped to 2147483647.
    const source = `
      console.log(parseInt("cbf29ce4", 16), parseInt("80000000", 16), parseInt("100000000", 16));
      console.log(parseInt("0xcbf29ce4"), parseInt("0x80000000"), parseInt("4503599627370496"));
      console.log(parseInt("-42"), parseInt("0x10", 10), parseInt("42px"));
      console.log(isNaN(parseInt("xyz")));
    `;
    expect(runProgram(source)).toBe(
      "3421674724 2147483648 4294967296\n3421674724 2147483648 4503599627370496\n-42 0 42\ntrue",
    );
  });

  it("supports default parameters, rest parameters and arguments", () => {
    const source = `
      function greet(name: string = "world", mark: string = "!"): string {
        return "hi " + name + mark;
      }
      function total(...nums: number[]): number {
        let sum = 0;
        for (const n of nums) sum += n;
        return sum;
      }
      function countArgs(): number {
        return arguments.length;
      }
      console.log(greet(), greet("ada"), total(1, 2, 3), countArgs(1, 2, 3, 4));
    `;
    expect(runProgram(source)).toBe("hi world! hi ada! 6 4");
  });

  it("supports Object helpers, spread, in and delete", () => {
    const source = `
      const base = { a: 1, b: 2 };
      const merged = Object.assign({}, base, { c: 3 });
      const copy = { ...base, a: 9 };
      console.log(Object.keys(base).join(","), Object.values(base).join(","));
      console.log(Object.entries(base).map((e) => e[0] + "=" + e[1]).join(","));
      console.log(merged.a, merged.b, merged.c, copy.a, "a" in base, "z" in base);
      delete base.a;
      console.log("a" in base, Object.keys(base).join(","));
    `;
    expect(runProgram(source)).toBe("a,b 1,2\na=1,b=2\n1 2 3 9 true false\nfalse b");
  });

  it("iterates object keys with for...in", () => {
    const source = `
      const obj = { x: 1, y: 2 };
      let out = "";
      for (const key in obj) out += key + "=" + obj[key] + ";";
      console.log(out);
    `;
    expect(runProgram(source)).toBe("x=1;y=2;");
  });
});
