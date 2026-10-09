/**
 * The game's inline `<script>` has to work in a browser as well as in the
 * `gui` extension. This checks the *browser* branch without a browser: it
 * extracts the script from `index.html`, gives it a tiny stub of the DOM
 * surface the game uses, runs it, and drives a few frames through the
 * `window.__pelican` hooks it publishes.
 *
 * It is a syntax + host-shim smoke test, not a rendering test: it catches the
 * class of failure that is invisible to the gui e2e suite (a host API that only
 * exists in the engine, a typo in the browser branch, an undeclared global).
 */

import { readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";

const exampleDir = resolve(dirname(fileURLToPath(import.meta.url)), "..", "..", "examples", "gui", "pelican-bike");
const page = readFileSync(join(exampleDir, "index.html"), "utf8");

/** The inline script body (the only `<script lang="ts">` in the document). */
function inlineScript(): string {
  const start = page.indexOf('<script lang="ts">');
  const end = page.indexOf("</script>", start);
  expect(start, "index.html has an inline <script lang=\"ts\"> body").toBeGreaterThan(0);
  return page.slice(start + '<script lang="ts">'.length, end);
}

/** A DOM element stub with the properties and methods the game touches. */
interface ElementStub {
  id: string;
  textContent: string;
  innerHTML: string;
  childElementCount: number;
  /** Writes are recorded so a test can see what the game put on the element. */
  style: {
    setProperty(name: string, value: string): void;
    getPropertyValue(name: string): string;
  };
  classList: { add(): void; remove(): void; toggle(): boolean; contains(): boolean };
  getBoundingClientRect(): { x: number; y: number; width: number; height: number };
  querySelector(): null;
  addEventListener(): void;
  setAttribute(): void;
  getAttribute(): null;
}

function element(id: string, rect: { x: number; y: number; width: number; height: number }): ElementStub {
  const styles = new Map<string, string>();
  return {
    id,
    textContent: "",
    innerHTML: "",
    childElementCount: 0,
    style: {
      setProperty: (name: string, value: string) => void styles.set(name, value),
      getPropertyValue: (name: string) => styles.get(name) ?? "",
    },
    classList: { add: () => {}, remove: () => {}, toggle: () => false, contains: () => false },
    getBoundingClientRect: () => ({ ...rect }),
    querySelector: () => null,
    addEventListener: () => {},
    setAttribute: () => {},
    getAttribute: () => null,
  };
}

interface RunResult {
  /** The value published on `window.__pelican` by the script. */
  hooks: {
    info(): Record<string, unknown>;
    step(dt: number): void;
    jump(): void;
    restart(): void;
    setAuto(on: boolean): void;
    measure(): void;
  };
  /** Frames the script asked the host for. */
  scheduled: number;
  /** Console lines the script produced. */
  logs: string[];
  /** Deliver a key event to the game's own `window` listener. */
  press(type: "keydown" | "keyup", key: string, code: string): void;
  /** The bike's current `bottom` inset, as the game last wrote it. */
  bottom(): string;
}

function runBrowserBranch(framesToDrive: number): RunResult {
  const script = inlineScript();
  const elements: Record<string, ReturnType<typeof element>> = {
    scene: element("scene", { x: 0, y: 40, width: 1000, height: 620 }),
    rider: element("rider", { x: 420, y: 416, width: 24, height: 34 }),
    pogo: element("pogo", { x: 420, y: 416, width: 24, height: 34 }),
    shadow: element("shadow", { x: 400, y: 447, width: 150, height: 3 }),
  };
  const logs: string[] = [];
  let scheduled = 0;
  let pending: ((time: number) => void) | null = null;
  const listeners: Record<string, Array<(event: unknown) => void>> = {};

  const documentStub = {
    getElementById: (id: string) => elements[id] ?? element(id, { x: 0, y: 0, width: 0, height: 0 }),
    querySelector: (selector: string) =>
      selector === ".title" ? { textContent: "鹈鹕骑自行车" } : null,
    createElement: (tag: string) => element(tag, { x: 0, y: 0, width: 0, height: 0 }),
  };

  // The browser `window`: no `on`/`isOpen`/`close`, which is what makes the
  // script pick its browser branch.
  const windowStub: Record<string, unknown> = {
    innerWidth: 1200,
    innerHeight: 800,
    addEventListener: (type: string, handler: (event: unknown) => void) => {
      (listeners[type] ??= []).push(handler);
    },
    requestAnimationFrame: (fn: (time: number) => void) => {
      scheduled += 1;
      pending = fn;
      return scheduled;
    },
  };

  const body = new Function(
    "window",
    "document",
    "console",
    `${script}\n//# sourceURL=pelican-bike-inline.js`,
  );
  body(windowStub, documentStub, {
    log: (...args: unknown[]) => logs.push(args.map(String).join(" ")),
    error: (...args: unknown[]) => logs.push(args.map(String).join(" ")),
  });

  const hooks = windowStub.__pelican as RunResult["hooks"];
  expect(hooks, "the script publishes window.__pelican").toBeTruthy();

  // Drive frames the way a browser would: call the callback a frame scheduled.
  for (let i = 0; i < framesToDrive; i++) {
    const next = pending as ((time: number) => void) | null;
    if (next === null) break;
    pending = null;
    next(1000 + i * 16);
  }

  // Keyboard input goes through addEventListener in this host.
  expect(listeners.keydown, "the game listens for keys").toBeTruthy();
  expect(listeners.keyup, "the game listens for key releases").toBeTruthy();

  /** Deliver one key event the way a browser does, to whatever the game
   * registered on `window`. `key` is the browser spelling (" " for the
   * spacebar, "ArrowUp" for the up arrow), not the engine's. */
  const press = (type: "keydown" | "keyup", key: string, code: string) => {
    for (const handler of listeners[type] ?? []) {
      handler({ key, code, type, preventDefault: () => {}, stopPropagation: () => {} });
    }
  };

  /** What the game last wrote to `#pogo`'s `bottom`, which it lifts when the
   * bird is airborne. */
  const bottom = () => elements.pogo?.style.getPropertyValue("bottom") ?? "";

  return { hooks, scheduled, logs, press, bottom };
}

describe("gui example — pelican bike in a browser", () => {
  it("runs the inline script against a browser-like host", () => {
    const { hooks, scheduled, logs } = runBrowserBranch(120);

    // It picked the browser branch and started a frame loop there.
    expect(scheduled).toBeGreaterThan(50);
    expect(logs.join("\n")).not.toContain("Uncaught");

    // The game logic advanced (the autopilot reports a distance > 0).
    const info = hooks.info();
    expect(info.host).toBe("browser");
    expect(info.ground).toBe(450);
    expect(String(info.hud)).toMatch(/\d{4} \d+ \d+ [▮▯]{7} \d+/);

    // The published hooks work, which is how a browser console drives it.
    hooks.setAuto(false);
    hooks.jump();
    hooks.step(0.016);
    const after = hooks.info();
    expect(after.host).toBe("browser");
    expect(String(after.player)).toBe("420,416");
  });

  it("jumps from a real Space key event, the way a browser spells it", () => {
    const { hooks, press, bottom } = runBrowserBranch(30);

    // The autopilot jumps on its own; turn it off so only the key can move us.
    hooks.setAuto(false);
    hooks.step(0.016);
    const grounded = bottom();

    // A browser delivers `key: " "` (and `code: "Space"`), never the engine's
    // `key: "Space"`. Matching only the engine spelling is what broke this.
    press("keydown", " ", "Space");
    press("keyup", " ", "Space");
    hooks.step(0.016);

    // `renderRider` lifts the bike by its `bottom` inset once it is airborne.
    expect(bottom(), `bottom stayed ${grounded} — the key never reached jump()`).not.toBe(grounded);
  });

  it("jumps from the browser's ArrowUp as well", () => {
    const { hooks, press, bottom } = runBrowserBranch(30);
    hooks.setAuto(false);
    hooks.step(0.016);
    const grounded = bottom();

    press("keydown", "ArrowUp", "ArrowUp");
    press("keyup", "ArrowUp", "ArrowUp");
    hooks.step(0.016);

    expect(bottom()).not.toBe(grounded);
  });

  it("does not name any engine-only window API outside its shim", () => {
    const script = inlineScript();
    // `window.on` / `isOpen` / `close` / `driver` may only appear in the shim
    // block (the `inGui` selection), never later in the game.
    const shimEnd = script.indexOf("const dom =");
    const afterShim = script.slice(script.indexOf("};", shimEnd));
    expect(afterShim).not.toContain("window.on(");
    expect(afterShim).not.toContain('window["on"]');
    expect(afterShim).not.toContain("window.isOpen");
    expect(afterShim).not.toContain("window.close(");
    expect(afterShim).not.toContain("window.driver");
    // And nothing may cast through `any`, which the engine's parser rejects.
    expect(script).not.toContain("as any");
  });
});
