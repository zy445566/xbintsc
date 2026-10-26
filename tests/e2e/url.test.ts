/**
 * URL: relative-reference resolution against a base plus the read-only
 * components (`href`, `protocol`, `host`, `pathname`, `search`, `hash`).
 */

import { it } from "vitest";
import { describeE2E } from "./harness.js";

describeE2E("URL", (harness) => {
  it("resolves relative references and exposes the components", () => {
    harness.expectSameOutputAsNode(`
      const base = new URL("https://example.com/a/b/index.html?x=1#h");
      console.log(base.href);
      console.log(new URL("../img/pic.png", base).href);
      console.log(new URL("/root", base).href);
      console.log(new URL("//cdn.example.com/x", base).href);
      console.log(new URL("./x", "https://example.com/a/b/").href);
      console.log(base.protocol, base.host, base.hostname, base.port);
      console.log(base.pathname, base.search, base.hash);
      console.log(base.toString());

      const file = new URL("file:///D:/proj/build/app.exe");
      console.log(file.origin, file.protocol, new URL("../../audio/clip.mp3", file).href);
    `);
  });
});
