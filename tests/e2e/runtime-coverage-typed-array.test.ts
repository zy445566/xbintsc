/**
 * Runtime-coverage tests for typed arrays (Uint8Array, Int32Array, ...).
 *
 * `subarray` is documented as returning a copy, so the suite deliberately only
 * exercises it through observationally equivalent operations; `keys`/`values`/
 * `entries` return arrays rather than iterators, hence the `Array.from` wrappers
 * that work with both.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

const TIMEOUT = 120000;

describeE2E(
  "runtime coverage: typed arrays",
  (h) => {
    it("exercises the typed-array implementation", () => {
      const source = `
        const a = new Uint8Array(4);
        a[0] = 300;
        a[1] = -1;
        a[2] = 1.7;
        console.log(a[0], a[1], a[2], a.length, a.byteLength, a.byteOffset);

        const b = Uint8Array.from([1, 2, 3], (x: number) => x * 2);
        console.log(b.join("-"));

        const c = Uint8Array.of(1, 2, 3);
        console.log(c.map((x: number) => x + 1).join(""));

        const f = new Float32Array(2);
        f[0] = 0.1;
        console.log(f[0]);

        for (const v of c) console.log(v);
        console.log([...c].join(","));

        console.log(Uint8Array.BYTES_PER_ELEMENT, Float64Array.BYTES_PER_ELEMENT);
        console.log(c.slice(1).toString());
        console.log(c.subarray(1).toString());
        console.log(c.includes(2), c.indexOf(3), c.lastIndexOf(1));

        const i = new Int8Array(2);
        i[0] = 200;
        console.log(i[0]);
        const u16 = new Uint16Array([65535, 65536]);
        console.log(u16[0], u16[1]);
        const i32 = new Int32Array([4294967295, 2147483648]);
        console.log(i32[0], i32[1]);
        const clamped = new Uint8ClampedArray(4);
        clamped[0] = 300;
        clamped[1] = -5;
        clamped[2] = 1.5;
        clamped[3] = 2.5;
        console.log(clamped.join(","));

        const d = new Int32Array([5, 3, 1, 4, 2]);
        d.sort();
        console.log(d.join(","));
        d.reverse();
        console.log(d.join(","));
        console.log(d.at(0), d.at(-1), d.at(100));

        const fill = new Uint8Array(5).fill(7, 1, 3);
        console.log(fill.join(","));

        const target = new Uint8Array(5);
        target.set([1, 2, 3], 1);
        console.log(target.join(","));

        const cp = new Uint8Array([1, 2, 3, 4, 5]);
        cp.copyWithin(0, 3);
        console.log(cp.join(","));

        const filtered = c.filter((x: number) => x > 1);
        console.log(filtered.length, filtered.join(""));
        console.log(c.reduce((acc: number, x: number) => acc + x, 0));
        console.log(c.every((x: number) => x > 0), c.some((x: number) => x === 3));
        console.log(c.find((x: number) => x > 1), c.findIndex((x: number) => x > 1));
        let sum = 0;
        c.forEach((x: number) => {
          sum += x;
        });
        console.log(sum);
        console.log(Array.from(c.keys()).join(","), Array.from(c.values()).join(","));
        console.log(Array.from(c.entries()).map((e: number[]) => e.join(":")).join(","));

        const fromArr = new Uint16Array([1, 2, 3]);
        const copy = new Uint8Array(fromArr);
        console.log(copy.join(","));

        const empty = new Uint8Array();
        console.log(empty.length, empty.byteLength);

        const oob = new Uint8Array(2);
        oob[5] = 7;
        console.log(oob.length, oob[5]);
        let threw = false;
        try {
          oob.length = 0;
        } catch (e) {
          threw = true;
        }
        console.log(threw, oob.length);
      `;
      const stdout = h.runProgram(source, { timeout: TIMEOUT });
      expect(stdout.split("\n")).toEqual([
        "44 255 1 4 4 0",
        "2-4-6",
        "234",
        "0.10000000149011612",
        "1",
        "2",
        "3",
        "1,2,3",
        "1 8",
        "2,3",
        "2,3",
        "true 2 0",
        "-56",
        "65535 0",
        "-1 -2147483648",
        "255,0,2,2",
        "1,2,3,4,5",
        "5,4,3,2,1",
        "5 1 undefined",
        "0,7,7,0,0",
        "0,1,2,3,0",
        "4,5,3,4,5",
        "2 23",
        "6",
        "true true",
        "2 1",
        "6",
        "0,1,2 1,2,3",
        "0:1,1:2,2:3",
        "1,2,3",
        "0 0",
        "2 undefined",
        "true 2",
      ]);
    });
  },
  { tmpPrefix: "xbintsc-runtime-coverage-" },
);
