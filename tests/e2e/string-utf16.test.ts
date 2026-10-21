/**
 * Differential coverage for JavaScript's UTF-16 string view over the runtime's
 * UTF-8 storage: `length`, indexing, slicing, searching, splitting, padding,
 * iteration, surrogate round-trips, JSON and RegExp offsets.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("UTF-16 string semantics", (harness) => {
  const { runProgram, expectSameOutputAsNode } = harness;

  it("matches Node for length, indexing, slicing and searching", () => {
    const source = `
const s = "中文😀aé";
const show = (v: unknown) => console.log(JSON.stringify(v));
show(s.length);
for (let i = 0; i < s.length; i++) show(s[i]);
for (let i = 0; i <= s.length; i++) show(s.codePointAt(i));
show(s.slice(0, 4));
show(s.slice(1, 5));
show(s.slice(2, 3));
show(s.slice(3, 4));
show(s.slice(-3));
show(s.substring(3, 1));
show(s.substring(1, 100));
show(s.substr(2, 2));
show(s.substr(-2));
show(s.charAt(2));
show(s.charCodeAt(2));
show(s.at(-1));
show(s.at(-5));
show(s.indexOf("😀"));
show(s.indexOf("a", 4));
show(s.indexOf("", 100));
show(s.lastIndexOf("中"));
show(s.lastIndexOf("😀", 3));
show(s.lastIndexOf("a", 2));
show(s.includes("😀"));
show(s.includes("文", 2));
show(s.startsWith("中文"));
show(s.startsWith("😀", 2));
show(s.endsWith("aé"));
show(s.endsWith("😀", 4));
show(s.split("😀"));
show(s.split("", 4));
show(s.split("a"));
show("😀".repeat(3));
show("x".padStart(4, "😀"));
show("x".padEnd(4, "😀"));
show("中".padStart(3, "ab"));
show("a😀b中".startsWith("😀", 2));
show("a😀b中".indexOf("😀", 2));
show("a😀b中".endsWith("a", 2));
show("a😀b中".lastIndexOf("😀", 2));
show("a😀b中".lastIndexOf("\\uDE00", 2));
show(s.length === 6);
`;
    expectSameOutputAsNode(source, { name: "utf16_indexing" });
  });

  it("matches Node for iteration, keys, surrogate round-trips and JSON", () => {
    const source = `
const s = "中文😀aé";
const show = (v: unknown) => console.log(JSON.stringify(v));
show([...s]);
show(Array.from(s));
show(Object.keys(s));
show(Object.values("😀"));
show(Object.entries("😀"));
const high = "\\uD83D";
const low = "\\uDE00";
show(high + low);
show((high + low).length);
show((high + low) === "😀");
show(String.fromCharCode(0x4e2d, 0xd83d, 0xde00));
show(String.fromCharCode(0x10000));
show(String.fromCodePoint(0x1f600));
show(String.fromCodePoint(0x4e2d));
show(JSON.stringify("\\uD83D"));
show(JSON.stringify("\\uDE00"));
show(JSON.stringify("\\uD83D\\uDE00"));
show(JSON.stringify("\\u{1F600}"));
show("\\u{1F600}".length);
show("\\u4e2d".length);
show(JSON.stringify(JSON.parse('"\\\\u4e2d\\\\ud83d\\\\ude00"')));
show(JSON.parse('"\\\\ud83d\\\\ude00"').length);
show(JSON.parse('"\\\\ud83d"').length);
let built = "";
for (const ch of s) built += ch;
show(built);
show(built === s);
let keys = "";
for (const key in "😀") keys += key;
show(keys);
show(JSON.stringify({ ..."😀" }));
show("a😀".replaceAll("", "-"));
show("a😀".replaceAll("😀", "中"));
show("a😀b".replace("", "-"));
`;
    expectSameOutputAsNode(source, { name: "utf16_iteration_json" });
  });

  it("reports RegExp offsets in UTF-16 code units", () => {
    const source = `
const show = (v: unknown) => console.log(JSON.stringify(v));
show("中😀x1".match(/1/)!.index);
show("中😀x".search(/x/));
show("中1".replace(/1/, (m: string, at: number) => String(at)));
show("a1b2".replace(/\\d/g, (m: string, at: number) => String(at)));
show("中1".replace(/1/, "#"));
show(JSON.stringify("中😀中".split(/😀/)));
`;
    expectSameOutputAsNode(source, { name: "utf16_regex_offsets" });
  });

  it("normalizes surrogate pairs across lazy concatenation boundaries", () => {
    const source = `
const prefix = "x".repeat(40);
const high = "\\uD83D";
const low = "\\uDE00";
const s = prefix + high + low;
const show = (v: unknown) => console.log(JSON.stringify(v));
show(s.length);
show(s.slice(-2));
show(s === prefix + "😀");
show(s.charCodeAt(40));
show(s.charCodeAt(41));
show(s.codePointAt(40));
show(s.endsWith("😀"));
show(s.indexOf("😀"));
`;
    expectSameOutputAsNode(source, { name: "utf16_lazy_boundary" });
  });

  it("prints a lone surrogate as the replacement character", () => {
    expect(runProgram('console.log("😀"[0], "中".length);', { name: "utf16_console" })).toBe("� 1");
  });
});
