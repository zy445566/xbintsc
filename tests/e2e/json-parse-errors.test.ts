/**
 * `JSON.parse` error handling.
 *
 * The parser used to return `0`/`undefined` for malformed input instead of
 * throwing, so `try { JSON.parse(input) } catch` never ran. It now validates
 * the grammar (numbers, strings, literals, separators, trailing text) and
 * throws a catchable `SyntaxError`.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("JSON.parse errors", (harness) => {
  it("throws a catchable SyntaxError for malformed input", () => {
    const stdout = harness.runProgram(`
      const bad = [
        "", "{bad", "{\\"a\\":}", "[1,]", "1 2", "undefined", "NaN", "'x'",
        "{", "[1", "\\"abc", "01", "1.", "1e", "tru", "{\\"a\\" 1}", "{\\"a\\":1,}",
      ];
      for (const text of bad) {
        let caught = false;
        try {
          JSON.parse(text);
        } catch (e) {
          caught = true;
          console.log("throw=" + (e instanceof SyntaxError) + ":" + (e as any).name +
                      ":" + ((e as any).message.length > 0));
        }
        if (!caught) console.log("no-throw=" + text);
      }

      // A valid document still parses, including escapes and nested containers.
      console.log("valid=" + JSON.stringify(
        JSON.parse(" {\\"a\\": [1, true, null, \\"\\u4e2d\\"]} ")));
      // The reviver keeps working on the successful path.
      console.log("reviver=" + JSON.stringify(
        JSON.parse("{\\"n\\":1}", (key: string, value: any) =>
          key === "n" ? (value as number) + 1 : value)));
    `);

    const lines = stdout.trim().split("\n");
    expect(lines).toHaveLength(19);
    for (const line of lines.slice(0, 17)) {
      expect(line).toBe("throw=true:SyntaxError:true");
    }
    expect(lines[17]).toBe('valid={"a":[1,true,null,"中"]}');
    expect(lines[18]).toBe('reviver={"n":2}');
    expect(stdout).not.toContain("no-throw=");
  });
});
