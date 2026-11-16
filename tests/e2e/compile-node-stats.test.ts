/**
 * `fs.Stats` timestamps are `Date` objects, like Node's.
 *
 * They used to be numbers-only (`mtimeMs`), so any library that calls
 * `stat.mtime.getTime()` — `proper-lockfile` probes the filesystem this way —
 * crashed with "Cannot read properties of undefined (reading 'getTime')".
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end compilation (fs timestamps)", (harness) => {
  const { runProgram } = harness;

  it("reports stat.mtime/atime/ctime/birthtime as Dates", () => {
    const source = `
      import { statSync, writeFileSync } from "node:fs";
      import { tmpdir } from "node:os";
      import { join } from "node:path";
      const file = join(tmpdir(), "xbintsc-stat-timestamps.txt");
      writeFileSync(file, "hello");
      const stat = statSync(file);
      console.log(stat.mtime instanceof Date, typeof stat.mtime.getTime, stat.mtime.getTime() === stat.mtimeMs);
      console.log(stat.atime instanceof Date, stat.ctime instanceof Date, stat.birthtime instanceof Date);
    `;
    expect(runProgram(source, { extensions: true })).toBe("true function true\ntrue true true");
  });
});
