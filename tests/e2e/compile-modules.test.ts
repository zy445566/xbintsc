/**
 * End-to-end tests for the module bundler.
 */

import { writeFileSync } from "node:fs";
import { join } from "node:path";
import { expect, it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("end-to-end module bundling", (harness) => {
  const { runProgram } = harness;

  it("supports import and export across modules", () => {
    writeFileSync(
      join(harness.workdir, "math_dep.ts"),
      `
      export const PI = 3.14;
      export function add(a: number, b: number): number {
        return a + b;
      }
      export default function greet(name: string): string {
        return "hi " + name;
      }
      `,
    );
    const source = `
      import greet, { add, PI } from "./math_dep";
      console.log(add(2, 3), PI);
      console.log(greet("ada"));
    `;
    expect(runProgram(source)).toBe("5 3.14\nhi ada");
  });

  it("does not re-export default through export *", () => {
    const source = `
      import mine from "./barrel_star";
      console.log(mine);
    `;
    expect(
      runProgram(source, {
        name: "export_star_default",
        files: {
          "dep_star.ts": 'export default "dep";\nexport const named = 1;',
          "barrel_star.ts": 'export default "mine";\nexport * from "./dep_star";',
        },
      }),
    ).toBe("mine");
  });

  it("resolves `.js` specifiers to their TypeScript sources", () => {
    writeFileSync(join(harness.workdir, "esm_dep.ts"), `export const value = 42;`);
    const source = `
      import { value } from "./esm_dep.js";
      console.log(value);
    `;
    expect(runProgram(source)).toBe("42");
  });

  it("bundles JavaScript modules through extension-less imports", () => {
    const source = `
      import { add } from "./js_dep";
      console.log(add(2, 3));
    `;
    expect(
      runProgram(source, {
        files: {
          "js_dep.js": 'export { add } from "./js_math";',
          "js_math.js": "export function add(a, b) { return a + b; }",
        },
      }),
    ).toBe("5");
  });

  it("evaluates a side-effect-only import before the importer", () => {
    const source = `
      import "./side_effect";
      console.log("main");
    `;
    expect(
      runProgram(source, { files: { "side_effect.ts": 'console.log("side");' } }),
    ).toBe("side\nmain");
  });

  it("keeps shorthand keys when bundling renames an imported binding", () => {
    writeFileSync(
      join(harness.workdir, "shorthand_dep.ts"),
      `export function getUser() { return { username: "alice", password: "secret" }; }`,
    );
    const source = `
      import { getUser } from "./shorthand_dep.js";
      const user = getUser();
      const { username } = user;
      console.log(JSON.stringify({ username }));
      console.log(JSON.stringify({ username: username }));
      console.log(JSON.stringify({ username, password: "x" }));
    `;
    expect(runProgram(source)).toBe(
      '{"username":"alice"}\n{"username":"alice"}\n{"username":"alice","password":"x"}',
    );
  });

  it("bundles circular imports with forward references", () => {
    const source = `
      import { a, aUsesB } from "./cycle_a";
      import { bUsesA } from "./cycle_b";
      console.log(a, aUsesB(), bUsesA());
    `;
    expect(
      runProgram(source, {
        files: {
          "cycle_a.ts": 'import { b } from "./cycle_b";\nexport const a = "a";\nexport function aUsesB() { return "a+" + b; }',
          "cycle_b.ts": 'import { a } from "./cycle_a";\nexport const b = "b";\nexport function bUsesA() { return "b+" + a; }',
        },
      }),
    ).toBe("a a+b b+a");
  });

  it("hoists module bindings so a cycle can forward-reference them at module scope", () => {
    const source = `
      import { aPlusB } from "./cycle_scope_a";
      import { bPlusA } from "./cycle_scope_b";
      console.log(aPlusB, bPlusA);
    `;
    expect(
      runProgram(source, {
        files: {
          "cycle_scope_a.ts": 'import { b } from "./cycle_scope_b";\nexport const a = "A";\nexport const aPlusB = b;',
          "cycle_scope_b.ts": 'import { a } from "./cycle_scope_a";\nexport const b = "B";\nexport const bPlusA = a;',
        },
      }),
    ).toBe("B undefined");
  });

  it("rejects a non-literal dynamic import at runtime", () => {
    const source = `
      async function load(specifier: string) {
        try {
          await import(specifier);
          return "loaded";
        } catch {
          return "rejected";
        }
      }
      console.log(await load("node:fs"));
    `;
    expect(runProgram(source)).toBe("rejected");
  });

  it("resolves a computed dynamic import against the linked modules", () => {
    const source = `
      async function main() {
        const fs = await import("node:" + "fs");
        const path = await import("p" + "ath");
        console.log(typeof fs.readFileSync, typeof path.join);
        const missing = "not-" + "a-module";
        try {
          await import(missing);
        } catch (error) {
          console.log("rejected", String(error).includes("not-a-module"));
        }
      }
      main();
    `;
    expect(runProgram(source, { extensions: true })).toBe("function function\nrejected true");
  });

  it("does not evaluate a dynamic-import target until it is called", () => {
    writeFileSync(join(harness.workdir, "lazy_dep.ts"), 'console.log("dep body");\nexport const value = 7;');
    const source = `
      async function load() {
        const loaded = await import("./lazy_dep.js");
        console.log("loaded", loaded.value);
      }
      console.log("ready");
    `;
    // The target's body must not run at startup — only when `load()` is called.
    expect(runProgram(source)).toBe("ready");
  });

  it("does not make a type-only re-export eager", () => {
    // `export type { T } from "./b.js"` is erased, so it must not pull `b` into
    // the startup path (the module can still be present through `import()`).
    writeFileSync(join(harness.workdir, "type_only_dep.ts"), 'console.log("dep body");\nexport type T = number;\nexport const value = 1;\n');
    writeFileSync(
      join(harness.workdir, "type_only_mid.ts"),
      'export type { T } from "./type_only_dep.js";\nexport async function load() { return await import("./type_only_dep.js"); }\n',
    );
    const source = `
      import { load } from "./type_only_mid.js";
      console.log("main", typeof load);
    `;
    expect(runProgram(source)).toBe("main function");
  });

  it("renames destructured top-level bindings without breaking the read", () => {
    writeFileSync(
      join(harness.workdir, "destructure_dep.ts"),
      `export function getUser() { return { username: "alice" }; }`,
    );
    const source = `
      import { getUser } from "./destructure_dep.js";
      const { username, role = "admin" } = getUser();
      console.log(username + ":" + role);
      console.log(JSON.stringify({ username }));
    `;
    expect(runProgram(source)).toBe('alice:admin\n{"username":"alice"}');
  });
});
