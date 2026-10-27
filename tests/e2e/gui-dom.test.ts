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

  it("writes style and dataset properties through named access", () => {
    const { value } = harness.compileAndRun(
      "dom-properties",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        #box { width: 100px; height: 40px; margin-top: 10px; }
        #box::before { content: "hi"; color: #123456; }
      </style></head><body>
        <div id="box" data-role="lead" data-user-id="7">x</div>
      </body></html>\`;

      const win = createWindow({ title: "dom-properties", width: 400, height: 300 });
      win.on("ready", () => {
        const doc = win.document;
        const box: any = doc.getElementById("box");

        // Property assignment used to park a dead field on the JS object.
        box.style.width = "50%";
        box.style.height = "30px";
        console.log("width=" + box.style.width + ":" + win.computedStyle("#box", "width"));
        console.log("rect=" + box.getBoundingClientRect().width + "x" + box.getBoundingClientRect().height);
        box.style.backgroundColor = "#ff0000";
        console.log("bg=" + box.style.backgroundColor + ":" + win.computedStyle("#box", "background-color"));
        box.style.display = "none";
        console.log("hidden=" + win.computedStyle("#box", "display"));
        box.style.display = "";
        console.log("shown=" + win.computedStyle("#box", "display"));
        box.style.zIndex = "5";
        console.log("z=" + win.computedStyle("#box", "z-index"));

        // setProperty and named access address the same declaration block.
        box.style.setProperty("color", "#00ff00");
        console.log("color=" + box.style.color + ":" + box.style.getPropertyValue("color") +
                    ":" + win.computedStyle("#box", "color"));
        box.style.setProperty("--accent", "#abcdef");
        console.log("custom=" + box.style.getPropertyValue("--accent") + ":" + box.style["--accent"]);
        console.log("decl=" + box.style.length + ":" + box.style.item(0) + ":" + box.style.item(5));
        box.style.removeProperty("z-index");
        console.log("removed=" + ("zIndex" in box.style) + ":" + box.style.zIndex);

        // dataset maps camelCase keys to data-* attributes, both ways.
        console.log("dataset=" + box.dataset.role + ":" + box.dataset.userId);
        box.dataset.count = "3";
        console.log("data-set=" + box.getAttribute("data-count") + ":" + ("count" in box.dataset) +
                    ":" + box.dataset.hasOwnProperty("count"));
        box.dataset.role = "hero";
        console.log("data-write=" + box.getAttribute("data-role"));
        delete box.dataset.count;
        console.log("data-delete=" + box.hasAttribute("data-count") + ":" + (box.dataset.count === undefined));
        console.log("data-keys=" + Object.keys(box.dataset).join(","));

        // getComputedStyle answers named properties from the cascade too.
        const cs: any = getComputedStyle(box);
        console.log("cs=" + cs.width + ":" + cs.height + ":" + cs.backgroundColor + ":" +
                    cs.marginTop + ":" + cs.getPropertyValue("background-color") +
                    ":" + cs.getPropertyValue("--accent"));
        const before: any = getComputedStyle(box, "::before");
        console.log("before=" + before.content + ":" + before.color);
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    expect(value("width")).toBe("50%:50%");
    expect(value("rect")).toBe("200x30");
    expect(value("bg")).toBe("#ff0000:rgb(255, 0, 0)");
    expect(value("hidden")).toBe("none");
    expect(value("shown")).toBe("block");
    expect(value("z")).toBe("5");
    expect(value("color")).toBe("#00ff00:#00ff00:rgb(0, 255, 0)");
    expect(value("custom")).toBe("#abcdef:#abcdef");
    expect(value("decl")).toBe("6:width:--accent");
    expect(value("removed")).toBe("false:");
    expect(value("dataset")).toBe("lead:7");
    expect(value("data-set")).toBe("3:true:true");
    expect(value("data-write")).toBe("hero");
    expect(value("data-delete")).toBe("false:true");
    expect(value("data-keys")).toBe("role,userId");
    expect(value("cs")).toBe("50%:30px:rgb(255, 0, 0):10px:rgb(255, 0, 0):#abcdef");
    expect(value("before")).toBe('"hi":rgb(18, 52, 86)');
  });
});
