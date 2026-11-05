/**
 * Entropy: `Math.random` and the global Web Crypto surface.
 *
 * The runtime used to derive `Math.random` from the C `rand()`, whose 15-bit
 * state made per-user salts predictable and collision-prone. It now uses a
 * xoshiro256** stream seeded from the OS CSPRNG, and exposes
 * `crypto.getRandomValues` / `crypto.randomUUID` backed by that CSPRNG.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("entropy", (harness) => {
  it("produces a well-mixed Math.random stream", () => {
    const stdout = harness.runProgram(`
      const seen = new Set<string>();
      let minimum = 1;
      let maximum = 0;
      for (let i = 0; i < 1000; i++) {
        const value = Math.random();
        seen.add(String(value));
        if (value < minimum) minimum = value;
        if (value > maximum) maximum = value;
      }
      console.log("unique=" + seen.size);
      console.log("range=" + (minimum >= 0 && maximum < 1));
    `);
    // 1000 draws from a 32 767-state generator would repeat early; this stream
    // must not.
    expect(stdout).toContain("unique=1000");
    expect(stdout).toContain("range=true");
  });

  it("fills typed arrays and makes v4 UUIDs", () => {
    const stdout = harness.runProgram(`
      const first = new Uint8Array(32);
      const returned = crypto.getRandomValues(first);
      const second = new Uint8Array(32);
      crypto.getRandomValues(second);
      let differs = false;
      let nonZero = false;
      for (let i = 0; i < first.length; i++) {
        if (first[i] !== second[i]) differs = true;
        if (first[i] !== 0) nonZero = true;
      }
      console.log("fill=" + (returned === first) + ":" + differs + ":" + nonZero);
      const uuid = crypto.randomUUID();
      console.log("uuid=" + uuid.length + ":" + uuid.charAt(14) + ":" + (uuid.split("-").length === 5));
      const object = crypto;
      const method = crypto.randomUUID;
      console.log("value=" + (object.randomUUID().length === 36) + ":" +
                  (method().length === 36));
      try {
        crypto.getRandomValues(new Uint8Array(65537));
        console.log("limit=no-throw");
      } catch (e) {
        console.log("limit=" + (e instanceof RangeError));
      }
    `);
    expect(stdout).toContain("fill=true:true:true");
    expect(stdout).toContain("uuid=36:4:true");
    expect(stdout).toContain("value=true:true");
    expect(stdout).toContain("limit=true");
  });
});
