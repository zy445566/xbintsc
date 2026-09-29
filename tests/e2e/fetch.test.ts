/**
 * End-to-end tests for the global `fetch` builtin.
 *
 * `fetch` performs a blocking HTTP/1.1 exchange and resolves an already-settled
 * promise, so the compiled program (the client) runs synchronously. The server
 * therefore lives in its own child process: `spawnSync` blocks this test's event
 * loop, which would otherwise stop an in-process server from ever replying.
 *
 * The programs are compiled without the Node extension, proving `fetch` is a
 * core global. Skipped when no clang-compatible compiler is available.
 */

import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawn, spawnSync } from "node:child_process";
import type { ChildProcess } from "node:child_process";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { build } from "../../src/driver/compiler.js";
import { hasClang } from "../helpers.js";

const describeWithClang = hasClang() ? describe : describe.skip;

const SERVER_SOURCE = `
const { createServer } = require("node:http");
const { writeFileSync } = require("node:fs");

const server = createServer((req, res) => {
  const url = req.url.split("?")[0];
  if (url === "/json") {
    res.writeHead(200, { "content-type": "application/json" });
    res.end(JSON.stringify({ hello: "world", n: 42 }));
  } else if (url === "/fixed") {
    const body = "fixed-length";
    res.writeHead(200, { "content-type": "text/plain", "content-length": String(body.length) });
    res.end(body);
  } else if (url === "/empty") {
    res.writeHead(204);
    res.end();
  } else if (url === "/post") {
    let body = "";
    req.on("data", (chunk) => (body += chunk));
    req.on("end", () => {
      res.writeHead(200, { "content-type": "application/json" });
      res.end(JSON.stringify({ method: req.method, body, echo: req.headers["x-echo"] }));
    });
  } else if (url === "/redirect") {
    res.writeHead(302, { location: "/json" });
    res.end();
  } else if (url === "/redirect-post") {
    res.writeHead(302, { location: "/post" });
    res.end();
  } else if (url === "/cookies") {
    res.writeHead(200, { "set-cookie": ["a=1", "b=2"], "content-type": "text/plain" });
    res.end("cookies");
  } else if (url === "/bytes") {
    res.writeHead(200, { "content-type": "application/octet-stream" });
    res.end(Buffer.from([0, 1, 2, 255]));
  } else {
    res.writeHead(404, { "content-type": "text/plain" });
    res.end("missing");
  }
});

server.listen(0, "127.0.0.1", () => {
  writeFileSync(process.argv[2], String(server.address().port));
});
`;

describeWithClang("fetch builtin", () => {
  let workdir: string;
  let cacheDir: string;
  let server: ChildProcess | undefined;

  beforeAll(async () => {
    workdir = mkdtempSync(join(tmpdir(), "xbintsc-fetch-"));
    cacheDir = join(workdir, ".cache");
    const serverFile = join(workdir, "fetch-server.cjs");
    const portFile = join(workdir, "port.txt");
    writeFileSync(serverFile, SERVER_SOURCE);
    server = spawn(process.execPath, [serverFile, portFile], { stdio: ["ignore", "ignore", "inherit"] });
    while (!existsSync(portFile)) {
      await new Promise((resolve) => setTimeout(resolve, 25));
    }
  });

  afterAll(() => {
    server?.kill();
    rmSync(workdir, { recursive: true, force: true });
  });

  function run(source: string): { stdout: string; stderr: string; status: number | null } {
    const entry = join(workdir, `program_${Math.random().toString(36).slice(2)}.ts`);
    writeFileSync(entry, source);
    // No `extensions`: `fetch` must work from the core runtime alone.
    const result = build(entry, { emit: "exe", outDir: join(workdir, "out"), cacheDir, force: true });
    expect(result.diagnostics.filter((d) => d.category === "error")).toEqual([]);
    const executed = spawnSync(result.outputPath, [], { encoding: "utf8", timeout: 120000 });
    return { stdout: executed.stdout ?? "", stderr: executed.stderr ?? "", status: executed.status };
  }

  function baseUrl(): string {
    const port = readFileSync(join(workdir, "port.txt"), "utf8").trim();
    return `http://127.0.0.1:${port}`;
  }

  it("GETs JSON and text responses", () => {
    const base = baseUrl();
    const { status, stdout } = run(`
      async function main() {
        const res = await fetch("${base}/json");
        console.log(res.ok, res.status, res.statusText);
        console.log(res.headers.get("content-type"));
        console.log(JSON.stringify(await res.json()));

        const fixed = await fetch("${base}/fixed");
        console.log("fixed", await fixed.text());

        const empty = await fetch("${base}/empty");
        console.log("empty", empty.status, JSON.stringify(await empty.text()));

        const missing = await fetch("${base}/missing");
        console.log("missing", missing.ok, missing.status, await missing.text());

        const head = await fetch("${base}/fixed", { method: "HEAD" });
        console.log("head", head.status, JSON.stringify(await head.text()));

        const bytes = await (await fetch("${base}/bytes")).arrayBuffer();
        console.log("bytes", bytes.join(","));

        const reused = await (await fetch("${base}/bytes")).bytes();
        console.log("bytes2", reused.length);
      }
      main();
    `);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual([
      "true 200 OK",
      "application/json",
      '{"hello":"world","n":42}',
      "fixed fixed-length",
      "empty 204 \"\"",
      "missing false 404 missing",
      'head 200 ""',
      "bytes 0,1,2,255",
      "bytes2 4",
    ]);
  });

  it("sends request bodies and headers", () => {
    const base = baseUrl();
    const { status, stdout } = run(`
      async function main() {
        const res = await fetch("${base}/post", {
          method: "post",
          headers: { "x-echo": "hi", "content-type": "text/plain" },
          body: "payload",
        });
        console.log(JSON.stringify(await res.json()));

        const arrayHeaders = await fetch("${base}/post", {
          method: "POST",
          headers: [["x-echo", "arr"], ["content-type", "text/plain"]],
          body: "b",
        });
        console.log(JSON.stringify(await arrayHeaders.json()));
      }
      main();
    `);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual([
      '{"method":"POST","body":"payload","echo":"hi"}',
      '{"method":"POST","body":"b","echo":"arr"}',
    ]);
  });

  it("follows redirects and honours the redirect mode", () => {
    const base = baseUrl();
    const { status, stdout } = run(`
      async function main() {
        const followed = await fetch("${base}/redirect");
        console.log("follow", followed.status, followed.url, JSON.stringify(await followed.json()));

        const manual = await fetch("${base}/redirect", { redirect: "manual" });
        console.log("manual", manual.status, manual.headers.get("location"));

        const redirected = await fetch("${base}/redirect-post", { method: "POST", body: "x" });
        console.log("post", JSON.stringify(await redirected.json()));

        try {
          await fetch("${base}/redirect", { redirect: "error" });
          console.log("error-resolved");
        } catch (error) {
          console.log("error-rejected", error instanceof TypeError);
        }
      }
      main();
    `);
    expect(status).toBe(0);
    const lines = stdout.trim().split("\n");
    expect(lines[0]!).toBe(`follow 200 ${base}/json {"hello":"world","n":42}`);
    expect(lines[1]!).toBe("manual 302 /json");
    expect(lines[2]!).toBe('post {"method":"GET","body":""}');
    expect(lines[3]!).toBe("error-rejected true");
  });

  it("exposes Headers helpers", () => {
    const base = baseUrl();
    const { status, stdout } = run(`
      async function main() {
        const res = await fetch("${base}/cookies");
        const headers = res.headers;
        console.log("has", headers.has("Content-Type"), headers.has("missing"));
        console.log("cookies", headers.get("set-cookie"), JSON.stringify(headers.getSetCookie()));
        console.log("keys", headers.keys().join(","));
        const collected: string[] = [];
        headers.forEach((value: string, name: string) => collected.push(name + "=" + value));
        console.log("forEach", collected.join(";"));
      }
      main();
    `);
    expect(status).toBe(0);
    const lines = stdout.trim().split("\n");
    expect(lines[0]!).toBe("has true false");
    expect(lines[1]!).toBe('cookies a=1, b=2 ["a=1","b=2"]');
    expect(lines[2]!.startsWith("keys ")).toBe(true);
    expect(lines[2]!).toContain("content-type");
    expect(lines[2]!).toContain("set-cookie");
    expect(lines[3]!).toContain("content-type=text/plain");
    expect(lines[3]!).toContain("set-cookie=a=1, b=2");
  });

  it("rejects invalid requests with a TypeError", () => {
    const { status, stdout } = run(`
      async function main() {
        const cases = [
          "not-a-url",
          "ftp://example.com/",
          "https://example.com/",
          "http://",
          "http://127.0.0.1:1/nope",
          "",
        ];
        for (const url of cases) {
          try {
            await fetch(url);
            console.log("resolved", url);
          } catch (error) {
            console.log("rejected", error instanceof TypeError);
          }
        }
        try {
          await fetch();
        } catch (error) {
          console.log("missing", error instanceof TypeError);
        }
      }
      main();
    `);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual([
      "rejected true",
      "rejected true",
      "rejected true",
      "rejected true",
      "rejected true",
      "rejected true",
      "missing true",
    ]);
  });

  it("is usable as a first-class value", () => {
    const base = baseUrl();
    const { status, stdout } = run(`
      async function main() {
        const send: typeof fetch = fetch;
        console.log(typeof send, typeof fetch);
        const res = await send("${base}/fixed");
        console.log(await res.text());
      }
      main();
    `);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual(["function function", "fixed-length"]);
  });
});
