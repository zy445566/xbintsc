/**
 * End-to-end test for `process` as a first-class object value.
 *
 * Member access on the bare global is lowered to the process namespace getter,
 * but destructuring the value (`const { env } = process`) and keeping a
 * reference read the object itself. chalk's supports-color does
 * `const { env } = process` on the `node:process` default export, which used to
 * crash with "Cannot read properties of undefined (reading 'TERM')".
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end compilation (process value)", (harness) => {
  const { runProgram } = harness;

  it("reads process as a first-class value", () => {
    const source = `
      import processValue from "node:process";
      const { env, pid, argv, platform } = process;
      const proc = process;
      console.log(typeof env, Object.keys(env).length > 0, typeof pid, typeof platform);
      console.log(argv.length >= 1, typeof proc.cwd, proc.cwd().length > 0);
      console.log(processValue.platform === process.platform, typeof processValue.stdout.write);
    `;
    expect(runProgram(source, { extensions: true })).toBe(
      "object true number string\ntrue function true\ntrue function",
    );
  });
});
