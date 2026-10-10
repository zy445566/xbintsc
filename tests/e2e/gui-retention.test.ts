/**
 * End-to-end test for the `gui` extension — DOM memory retention.
 *
 * A game loop mutates the document every frame: `innerHTML = …` rebuilds a
 * sprite, `textContent = …` refreshes a HUD field. The engine used to park every
 * replaced subtree in the document's detached-node pool and keep an element
 * handle (plus its slot) for every node it ever handed out, so both grew with
 * the frame count and a long run leaked tens of megabytes per minute.
 *
 * The counters come from `window.stats()`. Everything the churn is allowed to
 * change is the tree itself: the detached pool, the handle table and the handle
 * slots must come back to where they started.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — dom retention", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("releases the nodes innerHTML/textContent/replaceChild drop", () => {
    const { value } = harness.compileAndRun(
      "dom-retention",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #box { width: 100px; height: 50px; }
        .row { display: block; width: 40px; height: 10px; }
      </style></head><body>
        <div id="box"></div>
      </body></html>\`;

      const win = createWindow({ title: "retention", width: 400, height: 300 });
      win.on("ready", () => {
        const doc = win.document;
        const box = doc.getElementById("box");
        /* Growth of the pooled nodes, the handles and the handle slots. The
           document tree itself is allowed to change; these three are not. */
        const leak = (a: any, b: any): string =>
          (b.detachedRoots - a.detachedRoots) + ":" + (b.detachedNodes - a.detachedNodes) +
          ":" + (b.handles - a.handles) + ":" + (b.handleSlots - a.handleSlots);

        const before = win.stats();
        console.log("empty=" + before.treeNodes + ":" + leak(before, before));
        const sprite = "<div class='row'><span>a</span></div><div class='row'><span>b</span></div>";
        for (let i = 0; i < 500; i++) box.innerHTML = sprite;
        const afterInner = win.stats();
        console.log("inner=" + leak(before, afterInner));
        const treeAfterInner = afterInner.treeNodes;
        for (let i = 0; i < 500; i++) box.innerHTML = sprite;
        console.log("innerAgain=" + leak(afterInner, win.stats()) + ":" +
          (win.stats().treeNodes === treeAfterInner));
        for (let i = 0; i < 500; i++) box.textContent = "value " + i;
        console.log("text=" + leak(afterInner, win.stats()));

        /* A node the script removed itself is *not* dropped: DOM semantics keep
           it alive and usable until it is re-attached. */
        const held = doc.createElement("b");
        held.textContent = "held";
        box.appendChild(held);
        box.removeChild(held);
        console.log("removed=" + held.isConnected + ":" + held.textContent + ":" + win.stats().detachedRoots);
        box.appendChild(held);
        console.log("reattached=" + held.isConnected + ":" + win.stats().detachedRoots);

        /* replaceChild drops the node it replaces, so its handle goes stale. */
        const first = box.firstElementChild;
        const replacement = doc.createElement("i");
        box.replaceChild(replacement, first);
        console.log("replaced=" + replacement.isConnected + ":" + first.isConnected + ":" +
          win.stats().detachedRoots);

        /* A create/drop loop must reuse handle slots rather than grow the table:
           each iteration creates an element, attaches it and replaces it with a
           plain text node, which releases it again. */
        const slotsBefore = win.stats().handleSlots;
        for (let i = 0; i < 200; i++) {
          const node = doc.createElement("u");
          box.appendChild(node);
          box.innerHTML = "x";
        }
        const recycled = win.stats();
        console.log("recycled=" + (recycled.handleSlots - slotsBefore) + ":" + recycled.detachedNodes);
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    expect(value("empty")).toBe("10:0:0:0:0");
    // 500 sprite writes, then 500 more: no pooled node, handle or slot survives.
    expect(value("inner")).toBe("0:0:0:0");
    expect(value("innerAgain")).toBe("0:0:0:0:true");
    expect(value("text")).toBe("0:0:0:0");
    expect(value("removed")).toBe("false:held:1");
    expect(value("reattached")).toBe("true:0");
    expect(value("replaced")).toBe("true:false:0");
    expect(value("recycled")).toBe("0:0");
  });
});
