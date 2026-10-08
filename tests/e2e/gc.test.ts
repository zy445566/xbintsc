import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

/**
 * The collector is normally armed with a 16 MiB threshold, which small test
 * programs never reach. Setting `XT_GC_THRESHOLD` pins the trigger so a
 * collection runs before (almost) every allocation, turning these into GC
 * stress tests: every temporary an expression creates is collected while the
 * surrounding computation is still live.
 */
const gc = { env: { XT_GC_THRESHOLD: "1" } };
/** A looser trigger: frequent collections without collecting on every single allocation. */
const gcTight = { env: { XT_GC_THRESHOLD: "8192" } };

describeE2E("garbage collector (stress: collect on every allocation)", (harness) => {
  it("collects short-lived objects while accumulating a result", () => {
    expect(
      harness.runProgram(
        `
        let total = 0;
        for (let i = 0; i < 20000; i++) {
          const s = "item-" + i;
          const o = { n: i, s, nested: { deeper: [i, s] } };
          total += o.nested.deeper.length + o.s.length;
        }
        console.log(total);
      `,
        gc,
      ),
    ).toBe("228890");
  });

  it("matches Node for a mix of every heap-backed kind", () => {
    harness.expectSameOutputAsNode(
      `
      const out: string[] = [];
      function makeAdder(n: number) { return (x: number) => x + n; }
      let sum = 0;
      for (let i = 0; i < 5000; i++) sum += makeAdder(i)(1);
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
      out.push(JSON.stringify({
        sum, m: m.size, mg: m.get("k2999"), s: s.size, sh: s.has(1500),
        taSum, re: match && match[1], gsum, err: e.name + ":" + e.message,
      }));
      console.log(out.join("\\n"));
    `,
      gc,
    );
  });

  it("keeps module globals reachable across collections", () => {
    harness.expectSameOutputAsNode(
      `
      const items: string[] = [];
      let counter = { value: 0 };
      function bump() { counter.value += 1; items.push("x" + counter.value); }
      for (let i = 0; i < 5000; i++) bump();
      console.log(items.length, counter.value, items[4999]);
    `,
      gc,
    );
  });

  it("keeps suspended generators alive across collections", () => {
    harness.expectSameOutputAsNode(
      `
      function* range(n: number) {
        const buf: number[] = [];
        for (let i = 0; i < n; i++) { buf.push(i); yield buf.length; }
      }
      const it = range(500);
      let acc = 0;
      for (let step = it.next(); !step.done; step = it.next()) {
        // Allocate garbage between resumes; the generator's private stack and
        // its captured buffer must survive.
        const garbage = "z".repeat(50) + step.value;
        acc += garbage.length + step.value;
      }
      console.log(acc);
    `,
      gc,
    );
  });

  it("keeps main-stack locals reachable while a generator runs", () => {
    // The collection happens on the generator's private stack; the only
    // reference to `onlyMain` lives in the suspended caller's frame.
    harness.expectSameOutputAsNode(
      `
      function* work() {
        let sink = 0;
        for (let i = 0; i < 100000; i++) { const t = { i }; sink += t.i; }
        yield sink;
      }
      function main() {
        const it = work();
        const onlyMain = { tag: "main-only" };
        const v = it.next().value;
        console.log(onlyMain.tag + ":" + v);
      }
      main();
    `,
      gcTight,
    );
  });

  it("keeps outer suspended generators reachable during nested resumption", () => {
    harness.expectSameOutputAsNode(
      `
      function* inner() {
        let s = 0;
        for (let i = 0; i < 60000; i++) { const t = { i }; s += t.i; }
        yield s;
        return 0;
      }
      function* outer() {
        const onlyOuter = { tag: "outer" };
        const r = yield* inner();
        console.log(onlyOuter.tag + ":" + r);
        return 0;
      }
      function main() {
        const it = outer();
        const onlyMain = { tag: "main" };
        const v = it.next().value;
        console.log(onlyMain.tag + ":" + v);
        it.next();
      }
      main();
    `,
      gcTight,
    );
  });

  it("keeps pending promise reactions alive across collections", () => {
    harness.expectSameOutputAsNode(
      `
      const log: string[] = [];
      const p = new Promise<number>((resolve) => {
        // Allocate garbage while the promise is still pending.
        for (let i = 0; i < 2000; i++) { const tmp = { i, s: "p" + i }; void tmp; }
        resolve(21);
      });
      p.then((v) => v * 2).then((v) => log.push("v=" + v));
      Promise.resolve("done").then((v) => log.push(v));
      console.log(log.join(","));
    `,
      gc,
    );
  });

  it("survives an event loop that allocates on every tick", () => {
    harness.expectSameOutputAsNode(
      `
      let ticks = 0;
      let log = "";
      const id = setInterval(() => {
        const garbage = { tick: ticks, pad: "g".repeat(200) };
        log += garbage.tick;
        ticks++;
        if (ticks === 3) { clearInterval(id); console.log(log); }
      }, 1);
    `,
      gc,
    );
  });

  it("keeps readdirSync Dirents alive while the walk sorts them", () => {
    // `readdirSync(path, { withFileTypes: true })` used to accumulate entries in
    // a raw buffer the collector cannot see, so a collection triggered by a
    // later entry swept the `Dirent` objects created earlier; the next
    // `entry.isDirectory()` then threw "object has no callable method
    // 'isDirectory'". The self-host bootstrap performs exactly this walk (plus
    // a comparator that allocates) while fingerprinting the runtime directory.
    const files: Record<string, string> = { "dir-walk/sub/.keep": "" };
    for (let i = 0; i < 40; i++) files[`dir-walk/f${i}.txt`] = "x";
    harness.expectSameOutputAsNode(
      `
      import { readdirSync } from "node:fs";
      import { join } from "node:path";
      const base = join(${JSON.stringify(harness.workdir)}, "dir-walk");
      const entries = readdirSync(base, { withFileTypes: true })
        .sort((a, b) => a.name.localeCompare(b.name));
      let dirs = 0;
      let files = 0;
      for (const entry of entries) {
        if (entry.isDirectory()) dirs++;
        else if (entry.isFile()) files++;
      }
      console.log(entries.length + ":" + dirs + ":" + files);
    `,
      { ...gc, extensions: true, files },
    );
  });
});
