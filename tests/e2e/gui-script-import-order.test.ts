/**
 * GUI e2e: `<script src>` module imports run after `loadHTML`.
 *
 * A browser evaluates a module script's import graph after the document has
 * been parsed, so the imported modules already see real elements (and any DOM
 * work at their top level is not wasted on an empty document). The `.html`
 * loader marks relative imports as deferred; the bundler wraps the module and
 * the registered script invokes it at `loadHTML` time.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";
import { nodeExtension } from "../../src/extensions/node/index.js";

const harness = createGuiHarness();

const PROBE_PAGE = `<html><body>
  <div class="touch"></div><div class="touch"></div><div class="touch"></div>
  <canvas id="probe-canvas" width="120" height="80"></canvas>
  <script src="./probe.ts"></script>
</body></html>`;

const PAGE = `<html><body>
  <div id="box">hi</div>
  <script src="./app.ts"></script>
  <script src="./app2.ts"></script>
  <script src="./app3.ts"></script>
  <script src="./app4.ts"></script>
  <script src="./app6.ts"></script>
  <script src="./app7.ts"></script>
  <script src="./app8.ts"></script>
  <script src="./app9.ts"></script>
  <script src="./app10.ts"></script>
  <script src="./app11.ts"></script>
</body></html>`;

describe.skipIf(!guiAvailable)("gui extension — deferred <script src> imports", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("evaluates the import graph after loadHTML and shares module state", () => {
    const { value, stdout } = harness.compileAndRun(
      "script-import-order",
      `
      import { createWindow } from "gui";
      import page from "./page.html";
      import { shared } from "./shared";

      const win = createWindow({ title: "deferred", width: 320, height: 240 });
      console.log("shared-main=" + shared);
      const label = (name: string, value: any): void => console.log(name + "=" + value);
      label("before", (win as any).imported === undefined ? "undefined" : "set");
      label("before-deep", (win as any).deep === undefined ? "undefined" : "set");
      win.loadHTML(page);
      label("after", (win as any).imported);
      label("after-deep", (win as any).deep);
      label("body", (win as any).scriptBody);
      win.close();
      `,
      {
        extensions: [nodeExtension],
        files: {
          "page.html": PAGE,
          "app.ts": `
import { probe } from "./helper";
const box = document.getElementById("box");
console.log("app=" + box.textContent);
( window as any ).scriptBody = probe() + ":" + box.textContent;
`,
          "app2.ts": `
import { bump } from "./counter";
console.log("app2=" + bump());
`,
          "helper.ts": `
import "./deep";
const box = document.getElementById("box");
( window as any ).imported = "yes:" + box.textContent;
console.log("helper=" + box.textContent);
export function probe(): string { return "probe"; }
`,
          "deep.ts": `
( window as any ).deep = "yes";
console.log("deep");
`,
          "counter.ts": `
import { seed } from "./seed";
let n = 0;
export function bump(): number { n += seed; return n; }
`,
          "seed.ts": "export const seed = 1;",
          "app3.ts": 'import { value } from "./reexport"; console.log("app3=" + value);',
          "reexport.ts": 'import { value } from "./value"; export { value };',
          "app6.ts": 'import { shared } from "./shared"; console.log("app6=" + shared);',
          "shared.ts": 'export const shared = "s"; console.log("shared-eager");',
          "app7.ts": 'import { A, E } from "./klass"; console.log("app7=" + new A().x + ":" + E.One);',
          "klass.ts": 'export class A { x = 1; } export enum E { One }',
          "app8.ts": 'import make from "./anon"; console.log("app8=" + make());',
          "anon.ts": 'export default function () { return 5; }',
          "app9.ts": 'import Shape from "./anonclass"; console.log("app9=" + new Shape().x);',
          "anonclass.ts": 'export default class { x = 3; }',
          "app10.ts": 'import * as math from "./mathmod"; console.log("app10=" + math.add(2, 3) + math.tag);',
          "mathmod.ts": 'export function add(a: number, b: number): number { return a + b; } export const tag = "m";',
          "app11.ts": 'import { readEmpty } from "./guilib"; console.log("app11=" + (readEmpty() === ""));',
          "guilib.ts": 'import { readFileSync } from "fs"; export function readEmpty(): string { return readFileSync("/dev/null", "utf8"); }',
          "value.ts": 'export const value = "v";',
          "app4.ts": 'import { value } from "./wide"; console.log("app4=" + value);',
          "wide.ts": 'export * from "./value";',
        },
      },
    );

    /* Module top-level code runs while the script functions run (during
     * loadHTML), not at program start. */
    expect(value("before")).toBe("undefined");
    expect(value("before-deep")).toBe("undefined");
    expect(value("after")).toBe("yes:hi");
    expect(value("after-deep")).toBe("yes");
    expect(value("body")).toBe("probe:hi");
    expect(stdout).toContain("helper=hi");
    expect(stdout).toContain("deep");
    /* The script order is document order, and the shared module was evaluated
     * once, so the second script sees the incremented state. */
    expect(stdout.indexOf("helper=hi")).toBeLessThan(stdout.indexOf("app="));
    expect(stdout).toContain("app2=1");
    expect(stdout).toContain("app3=v");
    expect(stdout).toContain("app4=v");
    expect(stdout).toContain("app6=s");
    expect(stdout).toContain("app7=1:0");
    expect(stdout).toContain("app8=5");
    expect(stdout).toContain("app9=3");
    expect(stdout).toContain("app10=5m");
    expect(stdout).toContain("app11=true");
    expect(stdout).toContain("shared-main=s");
    /* The shared module was already reachable eagerly and must not run twice. */
    expect(stdout.match(/shared-eager/g)?.length).toBe(1);
  });

  it("gives imported modules the real window, document and canvas", () => {
    const { value } = harness.compileAndRun(
      "script-import-probe",
      `
      import { createWindow } from "gui";
      import page from "./probe.html";

      const win = createWindow({ title: "probe", width: 1280, height: 720 });
      const label = (name: string, value: any): void => console.log(name + "=" + value);
      label("before", (win as any).probe === undefined ? "undefined" : "set");
      win.loadHTML(page);
      label("probe", (win as any).probe);
      win.close();
      `,
      {
        files: {
          "probe.html": PROBE_PAGE,
          "probe.ts": `
const canvas: any = document.getElementById("probe-canvas");
const gl: any = canvas.getContext("webgl2");
const span: any = document.createElement("span");
span.dataset.touch = "ok";
const touches = document.querySelectorAll(".touch");
( window as any ).probe = [
  window.innerWidth + "x" + window.innerHeight,
  touches.length,
  span.tagName + ":" + span.dataset.touch,
  canvas.width + "x" + canvas.height,
  gl != null
].join(",");
console.log("probe-module=" + ( window as any ).probe);
`,
        },
      },
    );

    expect(value("before")).toBe("undefined");
    expect(value("probe")).toBe("1280x720,3,SPAN:ok,120x80,true");
  });
});
