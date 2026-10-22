/**
 * `Date.parse` / `new Date(string)` for ISO 8601 input.
 *
 * Both used to score every string as 0 (the epoch), so a parsed date silently
 * became 1970-01-01. The supported subset is date-only (`YYYY-MM-DD`, UTC),
 * optional `THH:MM[:SS[.mmm]]`, and a `Z` / `±HH:MM` / `±HHMM` offset.
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("Date parsing", (harness) => {
  it("parses ISO 8601 strings like Node", () => {
    harness.expectSameOutputAsNode(`
      console.log(Date.parse("2020-01-01T00:00:00.000Z"));
      console.log(new Date("2020-01-01T12:30:15.250+02:00").toISOString());
      console.log(
        new Date("2020-01-01").getUTCFullYear(),
        new Date("2020-01-01").getUTCMonth(),
        new Date("2020-01-01").getUTCDate(),
      );
      console.log(Number.isNaN(Date.parse("not a date")));
      console.log(Number.isNaN(new Date("nope").getTime()));
      console.log(new Date("2020-02-29T00:00:00Z").toISOString());
      console.log(Number.isNaN(Date.parse("2020-02-30")));
      console.log(new Date("2020-06-15 08:05:00Z").toISOString());
      console.log(new Date("2020-06-15T08:05:00+0800").toISOString());
    `);
  });

  it("throws a RangeError for toISOString on an invalid date", () => {
    harness.expectSameOutputAsNode(`
      try {
        new Date("nope").toISOString();
      } catch (e: any) {
        console.log(e instanceof RangeError, e.name);
      }
    `);
  });
});
