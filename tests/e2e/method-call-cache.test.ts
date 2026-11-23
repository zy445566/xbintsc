/**
 * Method-call inline cache: a hit calls the cached slot's current value, so
 * every way a callee can change has to be visible on the next call.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end method call cache", (harness) => {
  const { runProgram } = harness;

  it("observes method reassignment, deletion and prototype changes behind the call cache", () => {
    /* A method call carries a shape/slot inline cache, so every way a callee can
     * change — an own-property overwrite, a delete that falls back to the
     * inherited method, a prototype method replaced or added after the first
     * call, a getter that returns a function, and a polymorphic site — has to be
     * visible on the next call. */
    const source = `
      class Runner {
        tag(): string { return "proto"; }
        get accessor(): () => string { return () => "accessor"; }
      }
      const first = new Runner();
      const second = new Runner();

      console.log(first.tag(), second.tag());

      (first as any).tag = () => "own";
      console.log(first.tag(), second.tag());

      delete (first as any).tag;
      console.log(first.tag());

      (Runner.prototype as any).tag = function (): string { return "replaced"; };
      console.log(first.tag(), second.tag());

      (Runner.prototype as any).extra = function (): string { return "added"; };
      console.log((first as any).extra());

      console.log(first.accessor(), first.accessor());

      class Other { tag(): string { return "other"; } }
      const other = new Other();
      const mixed = [first, other, first, other];
      console.log(mixed.map((item) => item.tag()).join(","));

      const plain = { greet: () => "plain" };
      console.log(plain.greet());
      plain.greet = () => "rewritten";
      console.log(plain.greet());
      const plainAgain = { greet: plain.greet };
      console.log(plainAgain.greet());
    `;
    expect(runProgram(source)).toBe(
      [
        "proto proto",
        "own proto",
        "proto",
        "replaced replaced",
        "added",
        "accessor accessor",
        "replaced,other,replaced,other",
        "plain",
        "rewritten",
        "rewritten",
      ].join("\n"),
    );
  });

  it("observes an own method added after the site warmed on a prototype method", () => {
    /* three.js gives every `Euler` an own `_onChangeCallback` from its
     * `Object3D` constructor, while thousands of other Eulers already warmed
     * the `this._onChangeCallback()` call site with the prototype's empty
     * method. The cache used to keep calling the prototype method, so
     * `rotation.set(...)` never synced the quaternion: mouse input changed the
     * camera's Euler angles but the rendered 3D scene stayed still. */
    const source = `
      class Euler {
        x = 0;
        _onChangeCallback(): void {}
        set(x: number): Euler { this.x = x; this._onChangeCallback(); return this; }
      }
      const plain: Euler[] = [];
      for (let i = 0; i < 2000; i++) {
        const e = new Euler();
        plain.push(e);
        e.set(i);
      }
      let fired = -1;
      const e = new Euler();
      (e as any)._onChangeCallback = () => { fired = e.x; };
      e.set(42);
      console.log("fired=" + fired + " plain=" + plain.length);
    `;
    expect(runProgram(source)).toBe("fired=42 plain=2000");
  });
});

