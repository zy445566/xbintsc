import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

/**
 * TEMPORARY diagnostic for the Windows ARM64 `gc.test.ts` "mix" failure, where
 * the closure-accumulated `sum` comes out one ULP high. It is removed once the
 * root cause is fixed.
 */
describeE2E("gc diag", (harness) => {
  const src = `
    function makeAdder(n: number) { return (x: number) => x + n; }
    function direct(n: number, x: number) { return x + n; }
    let sum = 0;
    let badTerm = -1;
    let badSum = -1;
    let first = 0;
    let last = 0;
    for (let i = 0; i < 5000; i++) {
      const t = makeAdder(i)(1);
      if (i === 0) first = t;
      if (i === 4999) last = t;
      if (t !== i + 1 && badTerm < 0) badTerm = i;
      sum += t;
      if (sum !== (sum | 0) && badSum < 0) badSum = i;
    }
    const afterLoop = sum;
    // Variant that keeps every closure alive, so a precise array scan must
    // retain the boxes even if a register/stack root is missed.
    const keep: any[] = [];
    let ksum = 0;
    let kbad = -1;
    for (let i = 0; i < 5000; i++) {
      const f = makeAdder(i);
      keep.push(f);
      const t = f(1);
      if (t !== i + 1 && kbad < 0) kbad = i;
      ksum += t;
    }
    let dsum = 0;
    for (let i = 0; i < 5000; i++) dsum += direct(i, 1);
    let psum = 0;
    for (let i = 0; i < 5000; i++) psum += i + 1;
    // Heavy allocation phase, mirroring the real test.
    const m = new Map<string, number>();
    const s = new Set<number>();
    for (let i = 0; i < 3000; i++) { m.set("k" + i, i); s.add(i); }
    const ta = new Uint16Array(1000);
    for (let i = 0; i < ta.length; i++) ta[i] = i * 3;
    let taSum = 0; for (const v of ta) taSum += v;
    const re = /a(b+)c/;
    const match = re.exec("xxabbbcyy");
    function* g() { for (let i = 0; i < 10; i++) yield i * i; }
    let gsum = 0; for (const v of g()) gsum += v;
    const e = new RangeError("boom");
    console.log(
      "closure=" + (sum === (sum | 0)) + ":" + sum +
      " afterLoop=" + afterLoop +
      " keep=" + (ksum === (ksum | 0)) + ":" + ksum + " kbad=" + kbad +
      " direct=" + (dsum === (dsum | 0)) + ":" + dsum +
      " plain=" + (psum === (psum | 0)) + ":" + psum +
      " badTerm=" + badTerm + " badSum=" + badSum +
      " first=" + first + " last=" + last +
      " m=" + m.size + " mg=" + m.get("k2999") + " s=" + s.size + " sh=" + s.has(1500) +
      " taSum=" + taSum + " re=" + (match && match[1]) + " gsum=" + gsum +
      " err=" + e.name + ":" + e.message
    );
  `;

  it("closure sum diagnostics", () => {
    const gc = { env: { XT_GC_THRESHOLD: "1" } };
    const withGc = harness.runProgram(src, gc);
    const withoutGc = harness.runProgram(src);
    const expected =
      "gc[closure=true:12502500 afterLoop=12502500 keep=true:12502500 kbad=-1 direct=true:12502500 plain=true:12502500 badTerm=-1 badSum=-1 first=1 last=5000 m=3000 mg=2999 s=3000 sh=true taSum=1498500 re=bbb gsum=285 err=RangeError:boom] " +
      "nogc[closure=true:12502500 afterLoop=12502500 keep=true:12502500 kbad=-1 direct=true:12502500 plain=true:12502500 badTerm=-1 badSum=-1 first=1 last=5000 m=3000 mg=2999 s=3000 sh=true taSum=1498500 re=bbb gsum=285 err=RangeError:boom]";
    expect("gc[" + withGc.trim() + "] nogc[" + withoutGc.trim() + "]").toBe(expected);
  });
});
