/**
 * End-to-end tests for the `gui` extension — form controls and text input.
 *
 * The engine has no browser DOM: `<input>`/`<textarea>`/`<select>` are native
 * leaf boxes whose value/caret state lives in the C++ node. These tests drive
 * the editing path with synthetic `keydown`/`textInput`/composition events and
 * inspect both the exposed DOM properties and the painted display list.
 */

import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { createGuiHarness, guiAvailable } from "./gui-helpers.js";

const harness = createGuiHarness();

describe.skipIf(!guiAvailable)("gui extension — form controls", () => {
  beforeAll(() => harness.setup());
  afterAll(() => harness.cleanup());

  it("edits a text field, tracks selection and dispatches keypress/input", () => {
    const { value } = harness.compileAndRun(
      "form-text",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; font-size: 14px; }
        #user { width: 140px; height: 22px; }
      </style></head><body>
        <input id="user" type="text" value="abc" placeholder="name">
        <textarea id="note" rows="2">hello</textarea>
      </body></html>\`;

      const win = createWindow({ title: "form-text", width: 320, height: 220 });
      win.on("ready", () => {
        const doc: any = win.document;
        win.loadHTML(HTML);
        const user: any = doc.getElementById("user");
        const note: any = doc.getElementById("note");
        const log: string[] = [];
        user.addEventListener("keypress", (e: any) => log.push("press:" + e.key));
        user.addEventListener("input", (e: any) => log.push("input:" + e.data + ":" + e.inputType));

        user.focus();
        win.sendEvent("keydown", { key: "X" });
        win.sendEvent("keydown", { key: "y" });
        win.sendEvent("keydown", { key: "Backspace" });
        win.sendEvent("textInput", { text: "中" });
        console.log("value=" + user.value);
        console.log("log=" + log.join(","));

        user.setSelectionRange(1, 3, "backward");
        console.log("sel=" + user.selectionStart + "," + user.selectionEnd + ":" +
                    user.selectionDirection);
        user.value = "reset";
        console.log("set=" + user.value + ":" + user.selectionStart + ":" + user.selectionEnd);
        console.log("initial=" + note.value);
        console.log("types=" + user.type + ":" + note.type);
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    // The typed characters land at the caret; Backspace removes that character.
    expect(value("value")).toBe("abcX中");
    expect(value("log")).toBe(
      "press:X,input:X:insertText,press:y,input:y:insertText,input::deleteContentBackward," +
        "press:中,input:中:insertText",
    );
    // `setSelectionRange(..., "backward")` keeps the direction and the order.
    expect(value("sel")).toBe("1,3:backward");
    // Assigning `value` moves the caret to the end.
    expect(value("set")).toBe("reset:5:5");
    expect(value("initial")).toBe("hello");
    expect(value("types")).toBe("text:textarea");
  });

  it("exposes every input type's live state and size", () => {
    const { value } = harness.compileAndRun(
      "form-types",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>body { margin: 0; font-size: 14px; }</style></head><body>
        <input id="t" type="text" value="a">
        <input id="num" type="number" value="3">
        <input id="cb" type="checkbox" checked>
        <input id="rd" type="radio" name="r" value="r1">
        <input id="rg" type="range" min="0" max="10" value="4">
        <input id="cl" type="color" value="#00ff00">
        <input id="dt" type="date" value="2023-11-14">
        <input id="fl" type="file">
        <input id="btn" type="submit" value="Send">
        <input id="hid" type="hidden" value="secret">
        <textarea id="ta" rows="2">hi</textarea>
        <select id="sel"><option value="a">A</option><option value="b">B</option></select>
      </body></html>\`;

      const win = createWindow({ title: "form-types", width: 360, height: 300 });
      win.on("ready", () => {
        const doc: any = win.document;
        win.loadHTML(HTML);
        const ids = ["t", "num", "cb", "rd", "rg", "cl", "dt", "fl", "btn", "hid", "ta", "sel"];
        const parts: string[] = [];
        for (const id of ids) {
          const el: any = doc.getElementById(id);
          parts.push(id + ":" + el.type + ":" + (el.offsetWidth > 0));
        }
        console.log("types=" + parts.join("|"));
        const cb: any = doc.getElementById("cb");
        const rd: any = doc.getElementById("rd");
        const rgBox: any = doc.getElementById("rg");
        console.log("sizes=" + Math.round(cb.offsetWidth) + "x" + Math.round(cb.offsetHeight) + ":" +
                    Math.round(rd.offsetWidth) + "x" + Math.round(rd.offsetHeight) + ":" +
                    Math.round(rgBox.offsetWidth) + "x" + Math.round(rgBox.offsetHeight));
        cb.click();
        rd.click();
        console.log("checked=" + cb.checked + ":" + rd.checked);
        const rg: any = doc.getElementById("rg");
        rg.value = "7";
        console.log("range=" + rg.value + ":" + rg.min + ":" + rg.max);
        const sel: any = doc.getElementById("sel");
        sel.selectedIndex = 1;
        console.log("select=" + sel.value + ":" + sel.selectedIndex + ":" + sel.options.length);
        const ta: any = doc.getElementById("ta");
        ta.value = "line1\\nline2";
        console.log("ta=" + ta.value.split("\\n").length + ":" + ta.offsetHeight);
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    expect(value("types")).toBe(
      "t:text:true|num:number:true|cb:checkbox:true|rd:radio:true|rg:range:true|" +
        "cl:color:true|dt:date:true|fl:file:true|btn:submit:true|hid:hidden:false|" +
        "ta:textarea:true|sel:select-one:true",
    );
    expect(value("sizes")).toBe("13x13:13x13:129x20");
    expect(value("checked")).toBe("false:true");
    expect(value("range")).toBe("7:0:10");
    expect(value("select")).toBe("b:1:2");
    expect(value("ta")).toMatch(/^2:\d+$/);
  });

  it("composes IME text and commits it on compositionend", () => {
    const { value, stdout } = harness.compileAndRun(
      "form-ime",
      `
      import { createWindow, run } from "gui";

      const HTML = \`<html><body style="margin:0">
        <input id="user" value="ab">
      </body></html>\`;

      const win = createWindow({ title: "form-ime", width: 300, height: 160 });
      win.on("ready", () => {
        const doc: any = win.document;
        win.loadHTML(HTML);
        const user: any = doc.getElementById("user");
        const log: string[] = [];
        user.addEventListener("compositionstart", (e: any) => log.push("start:" + e.data));
        user.addEventListener("compositionupdate", (e: any) => log.push("update:" + e.data));
        user.addEventListener("compositionend", (e: any) => log.push("end:" + e.data));
        user.addEventListener("input", (e: any) => log.push("input:" + e.data));
        user.focus();
        user.setSelectionRange(0, 0);
        win.sendEvent("compositionstart", { data: "zh" });
        win.sendEvent("compositionupdate", { data: "zhong" });
        console.log("during=" + user.value);
        win.sendEvent("compositionend", { data: "中" });
        console.log("after=" + user.value);
        console.log("log=" + log.join(","));
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    // The pre-edit text is not committed to the value until compositionend.
    expect(value("during")).toBe("ab");
    expect(value("after")).toBe("中ab");
    expect(value("log")).toBe("start:zh,update:zhong,input:中,end:中");
    expect(stdout).toContain("after=中ab");
  });

  it("tracks document.activeElement, Tab order, disabled and placeholder", () => {
    const { value } = harness.compileAndRun(
      "form-focus",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><body style="margin:0">
        <input id="one" tabindex="1" value="a">
        <input id="two" tabindex="2" value="b">
        <input id="zero" value="c">
        <button id="btn">go</button>
        <input id="off" disabled value="d">
        <div id="box" tabindex="-1">box</div>
        <textarea id="note" placeholder="type..."></textarea>
        <label id="lab" for="agree">agree</label>
        <input id="agree" type="checkbox">
      </body></html>\`;

      const win = createWindow({ title: "form-focus", width: 400, height: 320 });
      win.on("ready", () => {
        const doc: any = win.document;
        win.loadHTML(HTML);
        console.log("initial=" + doc.activeElement.tagName + ":" + doc.activeElement.id);
        const order: string[] = [];
        for (let i = 0; i < 5; i++) {
          win.sendEvent("keydown", { key: "Tab" });
          order.push(doc.activeElement.id);
        }
        console.log("order=" + order.join(","));
        win.sendEvent("keydown", { key: "Tab", shift: true });
        console.log("back=" + doc.activeElement.id);
        const off: any = doc.getElementById("off");
        off.focus();
        console.log("disabled=" + (doc.activeElement.id !== "off") + ":" + off.disabled);
        const box: any = doc.getElementById("box");
        box.focus();
        console.log("box=" + doc.activeElement.id + ":" + box.tabIndex);
        const one: any = doc.getElementById("one");
        console.log("tabindex=" + one.tabIndex + ":" + box.tabIndex);
        one.tabIndex = 5;
        console.log("tabindex-set=" + one.tabIndex);
        const note: any = doc.getElementById("note");
        console.log("placeholder=" + note.placeholder);
        note.placeholder = "write";
        console.log("placeholder2=" + note.placeholder);
        // A <label for=...> focuses and activates its control.
        const lab: any = doc.getElementById("lab");
        lab.click();
        const agree: any = doc.getElementById("agree");
        console.log("label=" + (doc.activeElement.id === "agree") + ":" + agree.checked);
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    expect(value("initial")).toBe("BODY:");
    expect(value("order")).toBe("one,two,zero,btn,note");
    expect(value("back")).toBe("btn");
    expect(value("disabled")).toBe("true:true");
    expect(value("box")).toBe("box:-1");
    expect(value("tabindex")).toBe("1:-1");
    expect(value("tabindex-set")).toBe("5");
    expect(value("placeholder")).toBe("type...");
    expect(value("placeholder2")).toBe("write");
    expect(value("label")).toBe("true:true");
  });

  it("dispatches beforeinput/input/change and submits/resets forms", () => {
    const { value } = harness.compileAndRun(
      "form-events",
      `
      import { createWindow, run } from "gui";

      const HTML = \`
      <html><head><style>
        body { margin: 0; }
        input:checked { background-color: #00ff00; }
      </style></head><body>
        <form id="f">
          <input id="user" value="abc">
          <input id="cb" type="checkbox">
          <select id="sel"><option value="a">A</option><option value="b">B</option></select>
          <input id="rg" type="range" min="0" max="10" value="5">
          <button id="sub" type="submit">Send</button>
          <button id="rst" type="reset">Reset</button>
        </form>
      </body></html>\`;

      const win = createWindow({ title: "form-events", width: 400, height: 320 });
      win.on("ready", () => {
        const doc: any = win.document;
        win.loadHTML(HTML);
        const user: any = doc.getElementById("user");
        const cb: any = doc.getElementById("cb");
        const sel: any = doc.getElementById("sel");
        const rg: any = doc.getElementById("rg");
        const form: any = doc.getElementById("f");
        const log: string[] = [];
        user.addEventListener("input", (e: any) => log.push("u.input:" + e.inputType));
        user.addEventListener("beforeinput", (e: any) => {
          if ((e.data || "") === "!") e.preventDefault();
        });
        user.addEventListener("change", () => log.push("u.change:" + user.value));
        cb.addEventListener("input", () => log.push("cb.input"));
        cb.addEventListener("change", () => log.push("cb.change:" + cb.checked));
        sel.addEventListener("input", () => log.push("sel.input"));
        sel.addEventListener("change", () => log.push("sel.change:" + sel.value));
        rg.addEventListener("input", () => log.push("rg.input"));
        rg.addEventListener("change", () => log.push("rg.change"));
        form.addEventListener("submit", (e: any) => {
          log.push("submit:" + (e.submitter ? e.submitter.id : "-"));
          if (user.value === "block") e.preventDefault();
        });
        form.addEventListener("reset", () => log.push("reset"));

        user.focus();
        win.sendEvent("keydown", { key: "d" });
        win.sendEvent("keydown", { key: "!" });
        cb.click();
        sel.click();
        user.focus();
        win.sendEvent("keydown", { key: "Tab" });
        console.log("log1=" + log.join("|"));
        console.log("checked-style=" + win.computedStyle("#cb", "background-color"));

        user.focus();
        win.sendEvent("keydown", { key: "Enter" });
        console.log("log2=" + log.join("|"));
        doc.getElementById("sub").click();
        user.value = "block";
        doc.getElementById("sub").click();
        console.log("prevented=" + log.filter((line: string) => line === "submit:sub").length);

        user.value = "changed";
        cb.checked = true;
        doc.getElementById("rst").click();
        console.log("reset=" + user.value + ":" + cb.checked + ":" + sel.value);
        form.reset();
        console.log("reset2=" + user.value + ":" + cb.checked);

        // A range drag emits input while moving and change when the button is
        // released.
        const rect = rg.getBoundingClientRect();
        const y = rect.y + rect.height / 2;
        win.sendEvent("mousedown", { x: rect.x + 5, y: y });
        win.sendEvent("mousemove", { x: rect.x + rect.width - 5, y: y });
        win.sendEvent("mouseup", { x: rect.x + rect.width - 5, y: y });
        console.log("range=" + rg.value + ":" +
                    log.filter((line: string) => line === "rg.input").length + ":" +
                    log.filter((line: string) => line === "rg.change").length);
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    expect(value("log1")).toBe(
      "u.input:insertText|cb.input|cb.change:true|sel.input|sel.change:b|u.change:abcd",
    );
    expect(value("checked-style")).toBe("rgb(0, 255, 0)");
    // Enter in the input commits the value and submits the form implicitly
    // (no submitter).
    expect(value("log2")).toContain("|submit:-");
    expect(value("prevented")).toBe("2");
    expect(value("reset")).toBe("abc:false:a");
    expect(value("reset2")).toBe("abc:false");
    expect(value("range")).toMatch(/^10:2:1$/);
  });

  it("masks a password field's painted value", () => {
    const { value } = harness.compileAndRun(
      "form-password",
      `
      import { createWindow, run } from "gui";

      const HTML = \`<html><body style="margin:0">
        <input id="pass" type="password" value="secret">
      </body></html>\`;

      const win = createWindow({ title: "form-password", width: 300, height: 160 });
      win.on("ready", () => {
        const doc: any = win.document;
        win.loadHTML(HTML);
        const pass: any = doc.getElementById("pass");
        pass.focus();
        win.sendEvent("keydown", { key: "!" });
        const dump: string = win.paintList();
        console.log("has-mask=" + (dump.indexOf("\\u2022\\u2022\\u2022\\u2022\\u2022\\u2022\\u2022") >= 0));
        console.log("has-plain=" + (dump.indexOf("secret") >= 0));
        console.log("value=" + pass.value);
        win.close();
      });
      win.loadHTML(HTML);
      run();
      `,
    );

    expect(value("has-mask")).toBe("true");
    expect(value("has-plain")).toBe("false");
    expect(value("value")).toBe("secret!");
  });
});
