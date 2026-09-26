/**
 * End-to-end tests for closures, classes, destructuring and function semantics.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end objects, functions and classes", (harness) => {
  const { runProgram } = harness;

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

  it("supports #private fields, methods and static members", () => {
    const source = `
      class Counter {
        #count = 0;
        static #instances = 0;
        constructor() { Counter.#bumpStatic(); }
        #bump(by: number): void { this.#count += by; }
        static #bumpStatic(): void { Counter.#instances++; }
        inc(by: number): number { this.#bump(by); return this.#count; }
        get count(): number { return this.#count; }
        set count(v: number) { this.#count = v; }
        static instances(): number { return Counter.#instances; }
      }
      class Sub extends Counter {
        #label = "sub";
        constructor() { super(); this.inc(5); }
        describe(): string { return this.#label + ":" + this.count; }
      }
      const a = new Counter();
      const b = new Counter();
      a.inc(3);
      a.count = 10;
      console.log(a.count, b.count, Counter.instances());
      console.log(new Sub().describe());
    `;
    expect(runProgram(source)).toBe("10 0 2\nsub:5");
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

  it("supports destructuring bindings, enums, regexes and Error", () => {
    const source = `
      const [a, b = 7, ...rest] = [1, undefined, 3, 4];
      const { x, y: z = 9 } = { x: 5 };
      enum Color { Red, Green = 5, Blue }
      const re = /^-[0-9]+/;
      console.log(a, b, rest.join(","), x, z);
      console.log(Color.Red, Color.Green, Color.Blue, Color[5]);
      console.log(re.test("-42abc"), re.test("nope"));
      try { throw new Error("boom"); } catch (e) { console.log(e.name + ": " + e.message); }
    `;
    expect(runProgram(source)).toBe(
      "1 7 3,4 5 9\n0 5 6 Green\ntrue false\nError: boom",
    );
  });

  it("supports destructuring assignments", () => {
    const source = `
      let a = 1, b = 2;
      [a, b] = [b, a];
      console.log(a, b);

      let c = 0, d = 0, rest: number[] = [];
      [c, d = 5, ...rest] = [10, undefined, 30, 40];
      console.log(c, d, rest.join(","));

      let x = 0, y = 0;
      [[x], [y]] = [[100], [200]];
      let p = 0, q = 0, r = 0;
      ({ p, q = 9, p: r } = { q: 7, p: 8 });
      console.log(x, y, p, q, r);

      const target = { v: 0 } as any;
      const list = [0, 0];
      [target.v, list[0]] = [5, 6];
      console.log(target.v, list[0]);

      const pairs = [[1, 2], [3, 4]];
      let u = 0, v = 0;
      for ([u, v] of pairs) console.log(u, v);
      for (const [f, s] of pairs) console.log(f + s);
    `;
    expect(runProgram(source)).toBe(
      "2 1\n10 5 30,40\n100 200 8 7 8\n5 6\n1 2\n3 4\n3\n7",
    );
  });

  it("supports destructuring parameters", () => {
    const source = `
      const pairs = [["a", 1], ["b", 2]];
      const mapped = pairs.map(([name, value]) => [name, { value }]);
      console.log(mapped[0][0], mapped[1][0]);
      console.log(mapped[0][1].value, mapped[1][1].value);

      function sum([a, b]: [number, number]) { return a + b; }
      console.log(sum([3, 4]));

      function greet({ name, greeting = "Hello" }: { name: string; greeting?: string }) {
        return greeting + ", " + name;
      }
      console.log(greet({ name: "Ada" }), greet({ name: "Bob", greeting: "Hi" }));

      const withDefault = ([a, b] = [10, 20]) => a + b;
      console.log(withDefault(), withDefault([1, 2]));
    `;
    expect(runProgram(source)).toBe(
      "a b\n1 2\n7\nHello, Ada Hi, Bob\n30 3",
    );
  });

  it("supports Function.prototype.call, apply and bind", () => {
    const source = `
      function add(a: number, b: number): number { return a + b; }
      console.log(add.call(null, 1, 2), add.apply(null, [5, 6]));

      const obj = {
        base: 10,
        sum(a: number, b: number): number { return this.base + a + b; },
      };
      console.log(obj.sum.call(obj, 1, 2), obj.sum.apply(obj, [3, 4]));

      const bound = obj.sum.bind(obj, 1);
      console.log(bound(2));
      const bound2 = add.bind(null, 10);
      console.log(bound2(5));

      class Greeter {
        prefix = "hi";
        greet(name: string): string { return this.prefix + " " + name; }
      }
      const g = new Greeter();
      console.log(g.greet.call(g, "ada"), g.greet.bind(g)("cy"));
    `;
    expect(runProgram(source)).toBe(
      "3 11\n13 17\n13\n15\nhi ada hi cy",
    );
  });

  it("supports object-literal getters and setters", () => {
    const source = `
      const obj = {
        _x: 0,
        get x(): number { return this._x; },
        set x(v: number) { this._x = v * 2; },
        get double(): number { return this._x * 2; },
        method(a: number, b: number) { return a + b; },
      };
      obj.x = 5;
      console.log(obj.x, obj.double);
      console.log(obj.method(2, 3));
      console.log(Object.keys(obj).join(","));

      const base = 7;
      const o2 = { base, get next() { return base + 1; } };
      console.log(o2.base, o2.next);

      const o3 = { get ["dyn" + "amic"]() { return 42; } };
      console.log(o3.dynamic);
    `;
    expect(runProgram(source)).toBe(
      "10 20\n5\n_x,x,double,method\n7 8\n42",
    );
  });

  it("supports first-class built-in method values", () => {
    const source = `
      const arr = [1, 2, 3];
      console.log(typeof arr.map, arr.map.name, arr.map.length);
      console.log(arr.map?.((x: number) => x * 2).join(","));

      const text = "abc";
      console.log(typeof text.slice, text.slice(1), text.split?.("").join("-"));

      const obj = { base: 10, add(n: number) { return this.base + n; } };
      console.log(obj.add?.(5));

      const detached = arr.map;
      try {
        (detached as any)((x: number) => x);
        console.log("no throw");
      } catch (e) {
        console.log("threw");
      }
    `;
    expect(runProgram(source)).toBe(
      "function map 1\n2,4,6\nfunction bc a-b-c\n15\nthrew",
    );
  });

  it("calls functions through computed and indexed element access", () => {
    const source = `
      const fns = [(n: number) => n + 1, (n: number) => n * 2];
      console.log(fns[0](10), fns[1](10));
      let total = 0;
      for (let i = 0; i < fns.length; i++) total += fns[i](3);
      const table: any = { a: () => "A", b: () => "B" };
      const key = "b";
      console.log(total, table[key]());
    `;
    expect(runProgram(source)).toBe("11 20\n10 B");
  });

  it("supports the Error family as first-class constructors", () => {
    const source = `
      console.log(typeof Error, typeof TypeError, typeof AggregateError);
      console.log(new Error("a") instanceof Error);
      console.log(new TypeError("b") instanceof TypeError);
      console.log(new TypeError("b") instanceof Error);
      console.log(new RangeError("c") instanceof TypeError);
      const e = new Error("boom");
      console.log(String(e), e.toString(), e.name, e.message);

      class MyErr extends Error {
        constructor(m: string) { super(m); this.name = "MyErr"; }
      }
      const m = new MyErr("m");
      console.log(m.name, m.message, m instanceof MyErr, m instanceof Error);

      class CustomRange extends RangeError {}
      const r = new CustomRange("too big");
      console.log(r.name, r.message, r instanceof RangeError, r instanceof Error);

      const agg = new AggregateError([1, 2], "all failed");
      console.log(agg.name, agg.message, JSON.stringify(agg.errors), agg instanceof Error);

      Promise.any([Promise.reject("x"), Promise.reject("y")]).catch((reason) => {
        const err = reason as { name: string; errors: unknown[] };
        console.log(err.name, JSON.stringify(err.errors));
      });
    `;
    expect(runProgram(source)).toBe(
      "function function function\ntrue\ntrue\ntrue\nfalse\n" +
        "Error: boom Error: boom Error boom\n" +
        "MyErr m true true\nRangeError too big true true\n" +
        "AggregateError all failed [1,2] true\nAggregateError [\"x\",\"y\"]",
    );
  });

  it("supports generator functions", () => {
    const source = `
      function* nums() { yield 1; yield 2; yield 3; }
      const g = nums();
      console.log(typeof nums, g.next().value, g.next().value, g.next().value);
      console.log(JSON.stringify(g.next()));

      function* counter(start: number) {
        let x = start;
        while (true) {
          const step = yield x;
          x = step === undefined ? x + 1 : step;
        }
      }
      const c = counter(10);
      console.log(c.next().value, c.next().value, c.next(100).value, c.next().value);

      class Box {
        private n = 0;
        *gen(): Generator<number> { yield this.n++; yield this.n++; }
      }
      const box = new Box();
      console.log([...box.gen()].join(","), [...box.gen()].join(","));

      function* inner() { yield 1; yield 2; }
      function* outer() { yield* inner(); yield 9; }
      console.log([...outer()].join("-"), [...outer()].join("-"));

      function* echo() { const a = yield "first"; const b = yield a; return b; }
      const e = echo();
      console.log(e.next().value, e.next("A").value, JSON.stringify(e.next("B")));

      function* guarded() {
        try { yield "x"; } catch (err) { yield "caught:" + (err as Error).message; }
        yield "end";
      }
      const t = guarded();
      console.log(t.next().value, t.throw(new Error("E")).value, t.next().value);

      function* once() { yield 1; }
      const o = once();
      o.next();
      console.log(JSON.stringify(o.next()), typeof o);

      function* naturals() { let i = 0; while (true) yield i++; }
      const seen: number[] = [];
      for (const v of naturals()) { if (v >= 3) break; seen.push(v); }
      console.log(seen.join(","));
    `;
    expect(runProgram(source)).toBe(
      "function 1 2 3\n{\"done\":true}\n10 11 100 101\n0,1 2,3\n" +
        "1-2-9 1-2-9\nfirst A {\"value\":\"B\",\"done\":true}\n" +
        "x caught:E end\n{\"done\":true} object\n0,1,2",
    );
  });

  it("supports symbols and the well-known symbol protocol", () => {
    const source = `
      const a = Symbol("desc");
      const b = Symbol("desc");
      console.log(typeof Symbol, typeof a, a === a, a === b);
      console.log(a.toString(), String(a), a.description, String(Symbol()));
      console.log(String(Symbol.iterator), typeof Symbol.iterator);

      const holder: Record<string, unknown> = {};
      (holder as any)[a] = 1;
      (holder as any)[b] = 2;
      holder.x = 3;
      console.log(Object.keys(holder).join(","));
      console.log(Object.getOwnPropertySymbols(holder).length);
      console.log((holder as any)[a], (holder as any)[b], (holder as any)[Symbol("desc")]);
      console.log(JSON.stringify(holder), JSON.stringify(a), JSON.stringify([a, 1]));
      console.log(holder);

      const reg = Symbol.for("shared");
      console.log(reg === Symbol.for("shared"), Symbol.keyFor(reg), Symbol.keyFor(a));

      class Range {
        start: number;
        end: number;
        constructor(start: number, end: number) {
          this.start = start;
          this.end = end;
        }
        [Symbol.iterator]() {
          let i = this.start;
          const end = this.end;
          return {
            next() {
              return i < end ? { value: i++, done: false } : { value: undefined, done: true };
            },
          };
        }
      }
      console.log([...new Range(1, 4)].join(","));
      const walked: number[] = [];
      for (const x of new Range(10, 13)) walked.push(x);
      console.log(walked.join(","));

      function* delegates() { yield* new Range(5, 7); }
      console.log([...delegates()].join(","));
    `;
    expect(runProgram(source)).toBe(
      "function symbol true false\n" +
        "Symbol(desc) Symbol(desc) desc Symbol()\n" +
        "Symbol(Symbol.iterator) symbol\n" +
        "x\n2\n1 2 undefined\n" +
        '{"x":3} undefined [null,1]\n' +
        "{ x: 3, Symbol(desc): 1, Symbol(desc): 2 }\n" +
        "true shared undefined\n" +
        "1,2,3\n10,11,12\n5,6",
    );
  });
});
