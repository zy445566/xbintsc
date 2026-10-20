/**
 * End-to-end tests for the lazy (rope/cons) string model.
 *
 * Concatenation above the eager-copy threshold produces an immutable cons node
 * whose bytes are materialised on first observation. These tests exercise every
 * kind of observation (length, slicing, searching, case changes, JSON, property
 * keys) and keep chains reachable across a garbage collection.
 */

import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("lazy string concatenation", (harness) => {
  const { runProgram, runProgramFull } = harness;

  it("matches Node for append and prepend chains", () => {
    const source = `
function build(n: number): string {
  let text = "";
  for (let i = 0; i < n; i++) text = text + "abcdefghijklmnopqrstuvwxyz".charAt(i % 26);
  return text;
}
const text = build(2000);
console.log(text.length);
console.log(text.slice(0, 26));
console.log(text.slice(-26));
console.log(text.indexOf("xyz"));
console.log(text.lastIndexOf("abc"));
console.log(text.includes("mnop"));
console.log(text.split("def").length);
console.log(text.toUpperCase().slice(0, 26));
console.log(text.replace("abc", "ABC").slice(0, 26));
console.log(text.replaceAll("a", "!").slice(0, 26));
console.log(text === build(2000));
console.log(text.startsWith("abc"), text.endsWith("xyz"));
console.log(JSON.stringify(text.slice(0, 10)));
console.log(text.charCodeAt(5), text.at(-1));

let prepended = "";
for (let i = 0; i < 2000; i++) prepended = "x" + prepended;
console.log(prepended.length, prepended.slice(0, 3), prepended.slice(-3));

let mixed = "";
for (let i = 0; i < 200; i++) mixed += i;
console.log(mixed.length, mixed.slice(0, 12));

let templated = "";
for (let i = 0; i < 2000; i++) templated = \`\${templated}\${i % 10}\`;
console.log(templated.length, templated.slice(0, 12));

let key = "";
for (let i = 0; i < 40; i++) key += "k";
const holder: any = {};
holder[key] = "v";
console.log(holder[key], Object.keys(holder)[0].length);
console.log([text, prepended].join("|").length);
`;
    harness.expectSameOutputAsNode(source, { name: "string_concat_chains" });
  });

  it("keeps cons nodes reachable across collections", () => {
    const source = `
let text = "";
for (let i = 0; i < 2000; i++) text += "z";
const holder: any = { text: text };
const list: any[] = [text];
text = "";

/* Allocate and drop many objects to force collections while the rope is
   reachable only through the object property and the array element. */
let junk: any = {};
for (let i = 0; i < 40000; i++) junk = { value: i };
junk = {};
const trigger = "x" + "y";

/* Reuse the freed memory before the rope is ever flattened. */
const reuse: any[] = [];
for (let i = 0; i < 5000; i++) reuse.push("a" + "b");

console.log(holder.text.length, holder.text === "z".repeat(2000));
console.log(list[0].length, list[0] === holder.text, reuse.length, trigger);
console.log(holder.text.slice(0, 2), holder.text.slice(-2), holder.text.toUpperCase().slice(0, 3));
`;
    const result = runProgramFull(source, {
      name: "string_concat_gc",
      env: { XT_GC_THRESHOLD: "1048576" },
      timeout: 60000,
    });
    expect(result.status).toBe(0);
    expect(result.stdout.trim()).toBe("2000 true\n2000 true 5000 xy\nzz zz ZZZ");
  });

  it("flattens reused chains without mutating earlier results", () => {
    const source = `
let prefix = "start-";
for (let i = 0; i < 200; i++) prefix += "p";
const first = prefix + "-first";
const second = prefix + "-second";
const prefixCopy = prefix.slice(0);
console.log(first.slice(-6), second.slice(-7));
console.log(prefix.length, first.length, second.length);
console.log(prefix === prefixCopy, prefix === first.slice(0, prefix.length));
`;
    expect(runProgram(source)).toBe("-first -second\n206 212 213\ntrue true");
  });
});
