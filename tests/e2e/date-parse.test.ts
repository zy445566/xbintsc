/**
 * `Date.parse` / `new Date(string)` for ISO 8601 input.
 *
 * Both used to score every string as 0 (the epoch), so a parsed date silently
 * became 1970-01-01. The supported subset is date-only (`YYYY-MM-DD`, UTC),
 * optional `THH:MM[:SS[.mmm]]`, and a `Z` / `±HH:MM` / `±HHMM` offset.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("Date parsing", (harness) => {
  it("formats local and locale date/time strings", () => {
    const stdout = harness.runProgram(`
      // Local components: the expected text does not depend on the host zone.
      const d = new Date(2023, 10, 14, 22, 13, 20);
      console.log(d.toLocaleDateString());
      console.log(d.toLocaleTimeString());
      console.log(d.toLocaleString());
      console.log(d.toDateString());
      console.log(d.toTimeString());
      console.log(new Date(0).toUTCString());
      console.log(d.toLocaleDateString("en-US", { year: "numeric", month: "long", day: "numeric" }));
      console.log(d.toLocaleTimeString("en-US", { hour: "2-digit", minute: "2-digit", hour12: false }));
      console.log(new Date("nope").toLocaleDateString());
    `);
    expect(stdout).toBe(
      [
        "11/14/2023",
        "10:13:20 PM",
        "11/14/2023, 10:13:20 PM",
        "Tue Nov 14 2023",
        "22:13:20",
        "Thu, 01 Jan 1970 00:00:00 GMT",
        "November 14, 2023",
        "22:13",
        "Invalid Date",
      ].join("\n"),
    );
  });

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

  it("gives Date.now millisecond resolution", () => {
    /* `time(NULL)` used to truncate the clock to whole seconds, so 50 samples
     * spread over ~250 ms all reported the same value. */
    const stdout = harness.runProgram(`
      const samples: number[] = [];
      for (let i = 0; i < 50; i++) {
        const until = performance.now() + 5;
        while (performance.now() < until) {}
        samples.push(Date.now());
      }
      const distinct = new Set(samples).size;
      console.log(distinct > 10);
      console.log(Date.now() % 1 === 0);
      console.log(Math.abs(new Date().getTime() - Date.now()) < 1000);
    `);
    expect(stdout).toBe("true\ntrue\ntrue");
  });
});
