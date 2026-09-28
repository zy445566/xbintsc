/**
 * End-to-end tests for the `gui` extension — DOM element handles, mutation,
 * attribute/class/style APIs and element event bubbling.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — dom", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("exposes DOM element handles, mutation and element events", () => {
    const { value, stdout } = harness.compileAndRun(
      "dom",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #box { width: 100px; height: 50px; }
        #inner { width: 20px; height: 20px; color: #888888; }
      </style></head><body>
        <div id="box"><div id="inner">x</div></div>
      </body></html>\`;

      const win = createWindow({ title: "dom", width: 400, height: 300 });
      const log = [];
      win.on("click", (e) => log.push("win:" + e.target));
      win.on("ready", () => {
        const doc = win.document;
        const box = doc.querySelector("#box");
        const inner = doc.querySelector("#inner");
        console.log("tag=" + inner.tagName + ":" + inner.nodeType);
        console.log("text=" + inner.textContent);
        console.log("same=" + (doc.querySelector("#box") === box));
        console.log("parent=" + inner.parentNode.id + ":" + inner.parentElement.tagName);
        console.log("count=" + doc.querySelectorAll("div").length);
        console.log("body=" + (doc.body != null) + ":" + (doc.documentElement.tagName === "HTML"));
        console.log("byId=" + doc.getElementById("inner").id);
        inner.setAttribute("data-x", "1");
        console.log("attr=" + inner.getAttribute("data-x") + ":" + inner.hasAttribute("data-x"));
        inner.removeAttribute("data-x");
        console.log("attr2=" + inner.hasAttribute("data-x"));
        inner.classList.add("hot", "warm");
        console.log("class=" + inner.className + ":" + inner.classList.contains("warm"));
        console.log("toggle=" + inner.classList.toggle("hot") + ":" + inner.className);
        inner.style.setProperty("color", "#00ff00");
        console.log("style=" + inner.style.getPropertyValue("color") + ":" + win.computedStyle("#inner", "color"));
        console.log("offset=" + box.offsetWidth + "x" + box.offsetHeight);
        console.log("contains=" + box.contains(inner) + ":" + inner.contains(box));
        const created = doc.createElement("div");
        created.id = "new";
        created.textContent = "hi";
        box.appendChild(created);
        console.log("created=" + created.isConnected + ":" + doc.querySelector("#new").textContent + ":" + win.computedStyle("#new", "font-size"));
        const rect = doc.querySelector("#new").getBoundingClientRect();
        console.log("rect=" + rect.width + "x" + rect.height);
        box.removeChild(created);
        console.log("removed=" + created.isConnected + ":" + (doc.querySelector("#new") === undefined));
        // Element listeners bubble from target up to ancestors.
        box.addEventListener("click", (e) => log.push("box:" + e.target.id));
        inner.addEventListener("click", (e) => log.push("inner:" + e.target.id));
        inner.click();
        for (const line of log) console.log("event=" + line);
        // Window-level handlers keep their string target descriptor.
        win.sendEvent("click", { x: 5, y: 5, button: 0 });
        console.log("winCompat=" + log[log.length - 1]);
        // stopPropagation halts bubbling but the window handler still runs.
        const log2 = [];
        const box2 = doc.querySelector("#box");
        box2.addEventListener("click", () => log2.push("box"));
        inner.addEventListener("click", (e) => { log2.push("inner"); e.stopPropagation(); });
        inner.click();
        console.log("stopped=" + log2.join(","));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    expect(value("tag")).toBe("DIV:1");
    expect(value("text")).toBe("x");
    expect(value("same")).toBe("true"); // handle identity is stable
    expect(value("parent")).toBe("box:DIV");
    expect(value("count")).toBe("2");
    expect(value("body")).toBe("true:true");
    expect(value("byId")).toBe("inner");
    expect(value("attr")).toBe("1:true");
    expect(value("attr2")).toBe("false");
    expect(value("class")).toBe("hot warm:true");
    expect(value("toggle")).toBe("false:warm"); // toggle removed the existing class
    expect(value("style")).toBe("#00ff00:rgb(0, 255, 0)");
    expect(value("offset")).toBe("100x50");
    expect(value("contains")).toBe("true:false");
    expect(value("created")).toBe("true:hi:16px");
    expect(value("removed")).toBe("false:true");
    // Element listeners fire target-first, then bubble to the ancestor. The
    // legacy window handler runs last and still receives a descriptor string.
    expect(stdout).toContain("event=inner:inner");
    expect(stdout).toContain("event=box:inner");
    expect(value("winCompat")).toBe("win:div#inner.warm");
    expect(value("stopped")).toBe("inner");
  });
});
