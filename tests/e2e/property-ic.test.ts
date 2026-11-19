import { it } from "vitest";
import { describeE2E } from "./harness.js";

/**
 * Property reads lower to `xt_get_ic`, which caches (shape, index) per source
 * access. These cases exercise shape sharing across objects, transitions,
 * deletes, accessors and prototype fallback.
 */
describeE2E("inline-cached property access", (harness) => {
  it("reads the same shape from many objects", () => {
    harness.expectSameOutputAsNode(`
      interface Point { x: number; y: number; }
      function make(x: number, y: number): Point { return { x, y }; }
      const points: Point[] = [];
      for (let i = 0; i < 20000; i++) points.push(make(i, i * 2));
      let total = 0;
      for (const point of points) total += point.x + point.y;
      console.log(total, points[19999].x, points[19999].y);
    `);
  });

  it("invalidates the cache after add, delete and shape changes", () => {
    harness.expectSameOutputAsNode(`
      const o: any = { a: 1, b: 2 };
      const first = o.a;
      o.c = 3;
      const second = o.c;
      delete o.b;
      const third = o.b;
      const fourth = o.a;
      o.a = 9;
      console.log(first, second, third, fourth, o.a, Object.keys(o).join(","));
      const p: any = { 2: "two", 1: "one" };
      p[0] = "zero";
      console.log(Object.keys(p).join(","), p[2]);
    `);
  });

  it("stores through the inline cache and refills after shape changes", () => {
    harness.expectSameOutputAsNode(`
      const o: any = { a: 1, b: 2 };
      for (let i = 0; i < 5; i++) o.a = i;
      o.c = 3;
      for (let i = 0; i < 5; i++) o.b = i * 2;
      delete o.a;
      o.a = 42;
      console.log(JSON.stringify(o), Object.keys(o).join(","));

      const proto = { inherited: 1 };
      const child: any = Object.create(proto);
      for (let i = 0; i < 3; i++) child.inherited = i;
      console.log(child.inherited, proto.inherited, Object.keys(child).join(","));

      const accessor = {
        _v: 0,
        get value() { return this._v; },
        set value(v: number) { this._v = v + 1; },
      };
      for (let i = 0; i < 3; i++) accessor.value = i;
      console.log(accessor.value, accessor._v);

      const frozen = Object.freeze({ a: 1 });
      frozen.a = 2;
      console.log(frozen.a);

      class Field { value = 1; }
      const field: any = new Field();
      for (let i = 0; i < 3; i++) field.value = i;
      console.log(field.value, Object.keys(field).join(","));
    `);
  });

  it("keeps getters, prototypes and function properties correct", () => {
    harness.expectSameOutputAsNode(`
      const proto = { inherited: 7 };
      const child = Object.create(proto);
      child.own = 3;
      const fn: any = function named() { return 1; };
      fn.extra = 11;
      console.log(child.own, child.inherited, fn.extra);
      const accessor = {
        _value: 1,
        get value() { return this._value + 1; },
        set value(v: number) { this._value = v; },
      };
      const before = accessor.value;
      accessor.value = 10;
      console.log(before, accessor.value);
    `);
  });
});
