import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

/**
 * WeakMap / WeakSet / WeakRef semantics. The collector integration is exercised
 * with `XT_GC_THRESHOLD=1`, which collects before almost every allocation.
 */
describeE2E("weak collections and weak references", (harness) => {
  it("stores, reads and deletes WeakMap/WeakSet entries", () => {
    harness.expectSameOutputAsNode(`
      const key = { id: 1 };
      const value = { name: "value" };
      const map = new WeakMap<object, object>();
      map.set(key, value);
      const set = new WeakSet<object>([key]);
      const ref = new WeakRef(key);
      console.log(JSON.stringify([
        map.get(key) === value, map.has(key), set.has(key), ref.deref() === key,
        map.delete(key), map.has(key), set.delete(key), set.has(key),
        map.set(key, 7) === map, set.add(key) === set,
        ref instanceof WeakRef, map instanceof WeakMap, set instanceof WeakSet,
        typeof WeakMap, typeof WeakSet, typeof WeakRef,
      ]));
      console.log(Object.prototype.toString.call(map));
      console.log(Object.prototype.toString.call(set));
      console.log(Object.prototype.toString.call(ref));
    `);
  });

  it("accepts an entry array in the constructors", () => {
    harness.expectSameOutputAsNode(`
      const a = { id: "a" };
      const b = { id: "b" };
      const map = new WeakMap([[a, 1], [b, 2]]);
      const set = new WeakSet([a, b]);
      console.log(map.get(a), map.get(b), set.has(a), set.has(b));
    `);
  });

  it("rejects primitive keys and targets", () => {
    harness.expectSameOutputAsNode(`
      const out: string[] = [];
      const map = new WeakMap<object, number>();
      const set = new WeakSet<object>();
      function attempt(name: string, fn: () => void) {
        try { fn(); out.push(name + ":no"); }
        catch (error) { out.push(name + ":" + (error instanceof TypeError)); }
      }
      attempt("map-set", () => map.set(1 as unknown as object, 1));
      attempt("map-get", () => map.get(1 as unknown as object));
      attempt("set-add", () => set.add("x" as unknown as object));
      attempt("ref", () => new WeakRef(1 as unknown as object));
      console.log(out.join(","));
    `);
  });

  it("clears weak entries whose keys are no longer reachable", () => {
    expect(
      harness.runProgram(
        `
        const map = new WeakMap<object, object>();
        // The stack scan is conservative, so a word a *returned* frame left
        // behind still counts as a root until something overwrites it. A key
        // created at the same depth as the churn loop can therefore read as
        // reachable for the whole loop, which made this test depend on the
        // frame layout of the host compiler (it failed on Windows ARM64 while
        // passing elsewhere). Creating the keys this many frames down puts the
        // frame that held them below every frame a collection runs in, so the
        // scan cannot see it at all.
        function make(depth: number) {
          if (depth > 0) {
            const pad = { depth };
            const refs = make(depth - 1);
            void pad;
            return refs;
          }
          const key = { id: "key" };
          const value = { id: "value" };
          map.set(key, value);
          return [new WeakRef(key), new WeakRef(value)];
        }
        const refs = make(32);
        for (let i = 0; i < 40000; i++) {
          const garbage = { i, s: "g" + i };
          void garbage;
        }
        console.log(refs[0].deref() === undefined, refs[1].deref() === undefined);
      `,
        { env: { XT_GC_THRESHOLD: "1" } },
      ),
    ).toBe("true true");
  });

  it("keeps a WeakMap value alive while its key is alive", () => {
    expect(
      harness.runProgram(
        `
        const map = new WeakMap<object, object>();
        function make() {
          const key = { id: "held-key" };
          const value = { id: "held-value" };
          map.set(key, value);
          return { key, ref: new WeakRef(value) };
        }
        const held = make();
        for (let i = 0; i < 40000; i++) {
          const garbage = { i, s: "h" + i };
          void garbage;
        }
        console.log(held.ref.deref() !== undefined, map.get(held.key) !== undefined);
      `,
        { env: { XT_GC_THRESHOLD: "1" } },
      ),
    ).toBe("true true");
  });

});
