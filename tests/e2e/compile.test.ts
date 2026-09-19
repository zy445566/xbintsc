/**
 * End-to-end tests: compile TypeScript all the way to a native binary and run
 * it. Skipped automatically when no clang-compatible compiler is available.
 */

import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { build } from "../../src/driver/compiler.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { nodeExtension } from "../../src/extensions/node/index.js";
import { hasClang } from "../helpers.js";

const describeWithClang = hasClang() ? describe : describe.skip;

describeWithClang("end-to-end compilation", () => {
  let workdir: string;
  let cacheDir: string;

  beforeAll(() => {
    workdir = mkdtempSync(join(tmpdir(), "xbintsc-e2e-"));
    cacheDir = join(workdir, ".cache");
  });

  afterAll(() => {
    rmSync(workdir, { recursive: true, force: true });
  });

  function runProgram(source: string, options: { extensions?: boolean; name?: string } = {}): string {
    const result = runProgramFull(source, options);
    expect(result.status).toBe(0);
    return result.stdout.trim();
  }

  function runProgramFull(
    source: string,
    options: { extensions?: boolean; name?: string } = {},
  ): { stdout: string; stderr: string; status: number | null } {
    const name = options.name ?? `program_${Math.random().toString(36).slice(2)}`;
    const entry = join(workdir, `${name}.ts`);
    writeFileSync(entry, source);
    const extensions = options.extensions ? createDefaultRegistry().register(nodeExtension) : undefined;
    const result = build(entry, {
      emit: "exe",
      outDir: join(workdir, "out"),
      cacheDir,
      extensions,
    });
    expect(result.diagnostics.filter((d) => d.category === "error")).toEqual([]);
    const executed = spawnSync(result.outputPath, [], { encoding: "utf8" });
    return { stdout: executed.stdout, stderr: executed.stderr, status: executed.status };
  }

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

  it("implements closures with by-reference captures", () => {
    const source = `
      const makeCounter = () => {
        let count = 0;
        return () => {
          count += 1;
          return count;
        };
      };
      const next = makeCounter();
      console.log(next(), next(), next());
    `;
    expect(runProgram(source)).toBe("1 2 3");
  });

  it("prints objects and arrays with JS-like formatting", () => {
    const source = `
      const user = { name: "ada", tags: ["math", "code"], age: 36 };
      console.log(user);
    `;
    expect(runProgram(source)).toBe("{ name: ada, tags: [ math, code ], age: 36 }");
  });

  it("reads files through the node extension", () => {
    const dataPath = join(workdir, "data.txt");
    writeFileSync(dataPath, "from a file");
    const source = `console.log(readFileSync(${JSON.stringify(dataPath)}));`;
    expect(runProgram(source, { extensions: true })).toBe("from a file");
  });

  it("writes and manages files through the node extension", () => {
    const base = join(workdir, `fsops_${Math.random().toString(36).slice(2)}`);
    const source = `
      const base = ${JSON.stringify(base)};
      mkdirSync(base + "/a/b", { recursive: true });
      writeFileSync(base + "/a/b/file.txt", "hello");
      appendFileSync(base + "/a/b/file.txt", " world");
      console.log(readFileSync(base + "/a/b/file.txt"));
      console.log(readFileSync(base + "/a/b/file.txt", "hex"));
      console.log(existsSync(base + "/a/b/file.txt"), existsSync(base + "/missing"));
      copyFileSync(base + "/a/b/file.txt", base + "/a/copy.txt");
      renameSync(base + "/a/copy.txt", base + "/a/moved.txt");
      console.log(readdirSync(base + "/a").sort().join(","));
      const info = statSync(base + "/a/b/file.txt");
      console.log(info.size, info.isFile(), info.isDirectory());
      rmSync(base + "/a", { recursive: true });
      console.log(existsSync(base + "/a"));
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      "hello world\n68656c6c6f20776f726c64\ntrue false\nb,moved.txt\n11 true false\nfalse",
    );
  });

  it("supports hex and base64 encodings for fs reads and writes", () => {
    const base = workdir;
    const source = `
      const base = ${JSON.stringify(base)};
      const hexPath = base + "/encoding_${Math.random().toString(36).slice(2)}.hex.txt";
      const b64Path = base + "/encoding_${Math.random().toString(36).slice(2)}.b64.txt";
      writeFileSync(hexPath, "68656c6c6f", "hex");
      writeFileSync(b64Path, "aGVsbG8=", "base64");
      console.log(readFileSync(hexPath), readFileSync(b64Path));
      console.log(readFileSync(hexPath, "base64"));
      rmSync(hexPath);
      rmSync(b64Path);
    `;
    expect(runProgram(source, { extensions: true })).toBe("hello hello\naGVsbG8=");
  });

  it("provides the path module", () => {
    const source = `
      console.log(path.join("a", "b", "..", "c"));
      console.log(path.resolve("/tmp", "a", "..", "b"));
      console.log(path.dirname("/x/y/z.txt"), path.basename("/x/y/z.txt"), path.extname("/x/y/z.txt"));
      console.log(path.basename("/foo/bar.test.js", ".js"));
      console.log(path.normalize("/a//b/./c/../d"));
      console.log(path.isAbsolute("/a"), path.isAbsolute("a"));
      console.log(path.relative("/a/b/c", "/a/d"));
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      "a/c\n/tmp/b\n/x/y z.txt .txt\nbar.test\n/a/b/d\ntrue false\n../../d",
    );
  });

  it("provides the os and process modules", () => {
    const source = `
      console.log(os.platform().length > 0, os.arch().length > 0, os.homedir().length > 0, os.tmpdir().length > 0);
      console.log(process.platform.length > 0, process.arch.length > 0, process.pid > 0, process.cwd().length > 0);
      console.log(Object.keys(process.env).length > 0, process.argv.length >= 1);
    `;
    expect(runProgram(source, { extensions: true })).toBe("true true true true\ntrue true true true\ntrue true");
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

  it("provides common array and string methods", () => {
    const source = `
      const xs = [3, 1, 2];
      xs.push(4);
      const doubled = xs.map((x) => x * 2).filter((x) => x > 4);
      const total = xs.reduce((a, x) => a + x, 0);
      console.log(xs.join("-"), doubled.join(","), total, xs.includes(2), xs.indexOf(1));
      const s = "  Hello World  ";
      console.log(s.trim().toUpperCase(), s.trim().slice(0, 5), "a,b,c".split(",").join("|"));
    `;
    expect(runProgram(source)).toBe("3-1-2-4 6,8 10 true 1\nHELLO WORLD Hello a|b|c");
  });

  it("supports Math, global functions and console levels", () => {
    const source = `
      console.log(Math.max(1, 9, 4), Math.abs(-3), Math.floor(2.9), Math.pow(2, 8), Math.PI > 3.14);
      console.log(parseInt("42px"), parseFloat("2.5x"), isNaN(NaN), isFinite(1), Number("7"), String(9), Boolean(0));
      console.warn("warned");
      console.error("failed");
    `;
    const result = runProgramFull(source);
    expect(result.status).toBe(0);
    expect(result.stdout.trim()).toBe("9 3 2 256 true\n42 2.5 true true 7 9 false");
    expect(result.stderr.trim()).toBe("warned\nfailed");
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

  it("supports optional chaining", () => {
    const source = `
      const obj = { a: { b: 5 }, m: (x: number) => x + 1 };
      const empty = null;
      console.log(obj?.a?.b, empty?.a?.b, obj?.m?.(10), empty?.m?.(10));
      const arr = [1, 2, 3];
      console.log(arr?.length, arr?.map((x) => x * 2).join(","), empty?.length);
    `;
    expect(runProgram(source)).toBe("5 undefined 11 undefined\n3 2,4,6 undefined");
  });

  it("supports classes with new, this, statics, inheritance and instanceof", () => {
    const source = `
      class Animal {
        name: string;
        constructor(name: string) {
          this.name = name;
        }
        speak(): string {
          return this.name + " makes a sound";
        }
        static kind(): string {
          return "animal";
        }
      }
      class Dog extends Animal {
        constructor(name: string) {
          super(name);
        }
        speak(): string {
          return super.speak() + " (woof)";
        }
      }
      const dog = new Dog("Rex");
      console.log(dog.speak());
      console.log(Animal.kind(), dog instanceof Dog, dog instanceof Animal);
    `;
    expect(runProgram(source)).toBe("Rex makes a sound (woof)\nanimal true true");
  });

  it("supports async/await and promises", () => {
    const source = `
      async function greet(name: string): Promise<string> {
        return "hello " + name;
      }
      async function main(): Promise<void> {
        const message = await greet("world");
        console.log(message);
        const all = await Promise.all([Promise.resolve(1), Promise.resolve(2)]);
        console.log(all[0] + all[1]);
        const settled = await Promise.allSettled([Promise.resolve(1), Promise.reject("no")]);
        console.log(settled[0].status, settled[1].status);
        try {
          await Promise.reject("boom");
        } catch (err) {
          console.log("caught", err);
        }
      }
      main();
      Promise.resolve(10).then((x) => console.log("then", x + 1));
    `;
    expect(runProgram(source)).toBe("hello world\n3\nfulfilled rejected\ncaught boom\nthen 11");
  });

  it("supports Map, Set, JSON and extended standard library methods", () => {
    const source = `
      const map = new Map<string, number>();
      map.set("a", 1);
      map.set("b", 2);
      const nums = new Set<number>();
      nums.add(3);
      nums.add(3);
      console.log(map.get("a"), map.size, nums.has(3), nums.size);
      const obj = { x: 1, y: [2, 3] };
      console.log(JSON.stringify(obj));
      console.log(JSON.parse('{"k":5}').k);
      console.log([3, 1, 2].sort().join(","), [1, 2, 3].find((n) => n > 1));
      console.log("abc".padStart(5, "*"), (255).toString(16), (3.14159).toFixed(2));
      console.log(Number.isInteger(3), Array.isArray([1]), Math.imul(2, 3));
    `;
    expect(runProgram(source)).toBe(
      "1 2 true 1\n{\"x\":1,\"y\":[2,3]}\n5\n1,2,3 2\n**abc ff 3.14\ntrue true 6",
    );
  });

  it("supports import and export across modules", () => {
    writeFileSync(
      join(workdir, "math_dep.ts"),
      `
      export const PI = 3.14;
      export function add(a: number, b: number): number {
        return a + b;
      }
      export default function greet(name: string): string {
        return "hi " + name;
      }
      `,
    );
    const source = `
      import greet, { add, PI } from "./math_dep";
      console.log(add(2, 3), PI);
      console.log(greet("ada"));
    `;
    expect(runProgram(source)).toBe("5 3.14\nhi ada");
  });
});
