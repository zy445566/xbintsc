/**
 * URL: relative-reference resolution and the components a compiled program
 * reads. A reference with no scheme is a filesystem path and defaults to the
 * `file:` protocol (resolved against the working directory).
 */

import { join } from "node:path";
import { tmpdir } from "node:os";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { afterAll, expect, it } from "vitest";
import { describeE2E, type E2EHarness } from "./harness.js";

const workdir = mkdtempSync(join(tmpdir(), "xbintsc-url-"));
afterAll(() => rmSync(workdir, { recursive: true, force: true }));

describeE2E("URL", (harness: E2EHarness) => {
  it("resolves relative references and exposes the components", () => {
    const out = harness.runProgram(`
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
    expect(out).toBe(
      [
        "https://example.com/a/b/index.html?x=1#h",
        "https://example.com/a/img/pic.png",
        "https://example.com/root",
        "https://cdn.example.com/x",
        "https://example.com/a/b/x",
        "https: example.com example.com ",
        "/a/b/index.html ?x=1 #h",
        "https://example.com/a/b/index.html?x=1#h",
        "null file: file:///D:/audio/clip.mp3",
      ].join("\n"),
    );
  });

  it("defaults a scheme-less reference to file://", () => {
    const out = harness.runProgram(`
      const url = new URL("./assets/model.glb?rev=2#top");
      console.log(url.protocol);
      console.log(url.origin);
      console.log(url.href.startsWith("file:///"));
      console.log(url.pathname.endsWith("/assets/model.glb"));
      console.log(url.search, url.searchParams.get("rev"));
      console.log(url.hash);
      console.log(new URL("/tmp/shared.txt").href.startsWith("file:///"));
      console.log(new URL("nested/file.txt").pathname.endsWith("/nested/file.txt"));
    `);
    expect(out).toBe(
      [
        "file:",
        "null",
        "true",
        "true",
        "?rev=2 2",
        "#top",
        "true",
        "true",
      ].join("\n"),
    );
  });

  it("reads relative and file: URLs with fetch", async () => {
    const target = join(workdir, "payload.json");
    writeFileSync(target, '{"value":"ok"}');
    const out = harness.runProgram(`
      async function main(): Promise<void> {
        const response = await fetch(${JSON.stringify(target)});
        console.log("status=" + response.status + " ok=" + response.ok);
        const data: any = await response.json();
        console.log("value=" + data.value);
        console.log("file=" + (await fetch(${JSON.stringify("file://" + target)})).status);
        const missing = await fetch(${JSON.stringify(join(workdir, "missing.txt"))}).then(
          () => "resolved",
          (error: any) => "rejected:" + (error instanceof TypeError),
        );
        console.log("missing=" + missing);
        const empty = await fetch("").then(
          () => "resolved",
          (error: any) => "rejected:" + (error instanceof TypeError),
        );
        console.log("empty=" + empty);
      }
      main();
    `);
    expect(out).toBe(
      [
        "status=200 ok=true",
        "value=ok",
        "file=200",
        "missing=rejected:true",
        "empty=rejected:true",
      ].join("\n"),
    );
  });
});
