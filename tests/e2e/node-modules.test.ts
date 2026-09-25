/**
 * End-to-end tests for the Node compatibility modules that need real host I/O:
 * `buffer`, `fs/promises`, `stream`, `net`, `dgram` and `http`.
 *
 * Every program is compiled to a native binary and executed. The event-loop
 * based modules open servers on an ephemeral port (`listen(0)` / `bind(0)`) so
 * the tests stay hermetic. Skipped when no clang-compatible compiler is found.
 */

import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { spawnSync } from "node:child_process";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { build } from "../../src/driver/compiler.js";
import { createDefaultRegistry } from "../../src/extensions/registry.js";
import { nodeExtension } from "../../src/extensions/node/index.js";
import { hasClang } from "../helpers.js";

const describeWithClang = hasClang() ? describe : describe.skip;

describeWithClang("node compatibility modules", () => {
  let workdir: string;
  let cacheDir: string;

  beforeAll(() => {
    workdir = mkdtempSync(join(tmpdir(), "xbintsc-node-mods-"));
    cacheDir = join(workdir, ".cache");
  });

  afterAll(() => {
    rmSync(workdir, { recursive: true, force: true });
  });

  function run(source: string): { stdout: string; stderr: string; status: number | null } {
    const entry = join(workdir, `program_${Math.random().toString(36).slice(2)}.ts`);
    writeFileSync(entry, source);
    const extensions = createDefaultRegistry().register(nodeExtension);
    // `force` keeps the runtime objects honest: the whole-program cache does
    // not key on the C sources, only the generated object files do.
    const result = build(entry, { emit: "exe", outDir: join(workdir, "out"), cacheDir, extensions, force: true });
    expect(result.diagnostics.filter((d) => d.category === "error")).toEqual([]);
    const executed = spawnSync(result.outputPath, [], { encoding: "utf8", timeout: 120000 });
    return { stdout: executed.stdout ?? "", stderr: executed.stderr ?? "", status: executed.status };
  }

  it("implements Buffer construction and instance methods", () => {
    const source = `
      const buf = Buffer.from("hello");
      console.log(buf.toString(), buf.length, Buffer.isBuffer(buf));
      console.log(Buffer.alloc(4, 65).toString());
      console.log(Buffer.from("deadbeef", "hex").toString("hex"));
      console.log(Buffer.concat([Buffer.from("foo"), Buffer.from("bar")]).toString());
      const wide = Buffer.alloc(4);
      wide.writeUInt32BE(305419896, 0);
      console.log(wide.readUInt32BE(0), wide.toString("hex"));
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim()).toBe(
      ["hello 5 true", "AAAA", "deadbeef", "foobar", "305419896 12345678"].join("\n"),
    );
  });

  it("supports named imports from buffer", () => {
    const source = `
      import { Buffer, alloc, concat, from, isBuffer } from "buffer";
      console.log(from("hello").toString(), alloc(4, 65).toString());
      console.log(isBuffer(from("x")), concat([from("foo"), from("bar")]).toString());
      console.log(new Buffer("hey").toString(), Buffer.from("yo").toString(), Buffer.isBuffer(Buffer.from("z")));
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual(["hello AAAA", "true foobar", "hey yo true"]);
  });

  it("wraps fs operations in promises", () => {
    const target = join(workdir, `promise_${Math.random().toString(36).slice(2)}.txt`);
    const source = `
      import { readFile, rm, stat, writeFile } from "fs/promises";
      async function main(): Promise<void> {
        await writeFile(${JSON.stringify(target)}, "hello promises");
        const data = await readFile(${JSON.stringify(target)});
        const info = await stat(${JSON.stringify(target)});
        console.log(data, info.size, info.isFile());
        await rm(${JSON.stringify(target)});
      }
      main();
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim()).toBe("hello promises 14 true");
  });

  it("streams data through Readable, Transform and pipe", () => {
    const source = `
      const readable = new Readable();
      readable.on("data", (chunk: string) => console.log("data", chunk));
      readable.on("end", () => console.log("end"));
      readable.push("hello");
      readable.push(null);

      const transform = new Transform({ transform(chunk: string, enc: string, cb: (err: unknown, out: string) => void) {
        cb(null, chunk.toUpperCase());
      } });
      transform.on("data", (chunk: string) => console.log("upper", chunk));
      transform.write("abc");

      const source = new Readable();
      const sink = new Writable();
      sink.on("finish", () => console.log("piped"));
      source.pipe(sink);
      source.push("payload");
      source.push(null);
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual(["data hello", "end", "upper ABC", "piped"]);
  });

  it("supports named imports from stream", () => {
    const source = `
      import { PassThrough, Readable, Transform, Writable } from "stream";
      const readable = new Readable();
      readable.on("data", (chunk: string) => console.log("data", chunk));
      readable.push("hello");
      readable.push(null);
      const transform = new Transform({ transform(chunk: string, enc: string, cb: (err: unknown, out: string) => void) {
        cb(null, chunk.toUpperCase());
      } });
      transform.on("data", (chunk: string) => console.log("upper", chunk));
      transform.write("abc");
      const pass = new PassThrough();
      pass.on("data", (chunk: string) => console.log("pass", chunk));
      pass.write("through");
      const sink = new Writable();
      sink.on("finish", () => console.log("finish"));
      sink.end();
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual(["data hello", "upper ABC", "pass through", "finish"]);
  });

  it("flushes chunks buffered before a data listener attaches", () => {
    const source = `
      const readable = new Readable();
      readable.push("early");
      readable.on("data", (chunk: string) => console.log("data", chunk));
      readable.push(null);

      const from = Readable.from(["a", "b"]);
      from.on("data", (chunk: string) => console.log("from", chunk));
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual(["data early", "from a", "from b"]);
  });

  it("exchanges data over a TCP server and client", () => {
    const source = `
      const server = net.createServer((socket: any) => {
        socket.on("data", (data: string) => {
          socket.write("echo:" + data);
          socket.end();
        });
      });
      server.listen(0, () => {
        const client = net.connect(server.address().port, "127.0.0.1");
        client.on("data", (data: string) => console.log("client", data));
        client.on("end", () => {
          console.log("end");
          server.close();
        });
        client.write("ping");
      });
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual(["client echo:ping", "end"]);
  });

  it("supports named imports from node:net", () => {
    const source = `
      import { createServer, connect } from "node:net";
      const server = createServer((socket: any) => {
        socket.on("data", (data: string) => {
          socket.write("echo:" + data);
          socket.end();
        });
      });
      server.listen(0, () => {
        const client = connect(server.address().port, "127.0.0.1");
        client.on("data", (data: string) => console.log("client", data));
        client.on("end", () => {
          console.log("end");
          server.close();
        });
        client.write("ping");
      });
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual(["client echo:ping", "end"]);
  });

  it("exchanges a datagram over UDP", () => {
    const source = `
      const server = dgram.createSocket("udp4");
      server.on("message", (msg: string, rinfo: any) => {
        server.send("pong:" + msg, rinfo.port, rinfo.address);
      });
      server.bind(0, () => {
        const client = dgram.createSocket("udp4");
        client.on("message", (msg: string) => {
          console.log(msg);
          client.close();
          server.close();
        });
        client.send("ping", server.address().port, "127.0.0.1");
      });
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim()).toBe("pong:ping");
  });

  it("serves HTTP requests and reads responses", () => {
    const source = `
      const server = http.createServer((req: any, res: any) => {
        let body = "";
        req.on("data", (chunk: string) => { body += chunk; });
        req.on("end", () => {
          res.writeHead(200, { "Content-Type": "text/plain" });
          res.end("hello " + req.url + " " + body);
        });
      });
      server.listen(0, () => {
        const port = server.address().port;
        http.get("http://127.0.0.1:" + port + "/world", (res: any) => {
          let body = "";
          console.log("status", res.statusCode);
          res.on("data", (chunk: string) => { body += chunk; });
          res.on("end", () => {
            console.log("get", body);
            const req = http.request({ host: "127.0.0.1", port, path: "/submit", method: "POST" }, (res2: any) => {
              let reply = "";
              res2.on("data", (chunk: string) => { reply += chunk; });
              res2.on("end", () => {
                console.log("post", reply);
                server.close();
              });
            });
            req.write("payload");
            req.end();
          });
        });
      });
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual([
      "status 200",
      "get hello /world ",
      "post hello /submit payload",
    ]);
  });

  it("supports named imports from node:http", () => {
    const source = `
      import { createServer, get } from "node:http";
      const server = createServer((req: any, res: any) => {
        res.writeHead(200, { "Content-Type": "text/plain" });
        res.end("hello " + req.url);
      });
      server.listen(0, () => {
        const port = server.address().port;
        get("http://127.0.0.1:" + port + "/world", (res: any) => {
          let body = "";
          console.log("status", res.statusCode);
          res.on("data", (chunk: string) => { body += chunk; });
          res.on("end", () => {
            console.log("get", body);
            server.close();
          });
        });
      });
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual(["status 200", "get hello /world"]);
  });

  it("supports named imports from dgram", () => {
    const source = `
      import { createSocket } from "node:dgram";
      const server = createSocket("udp4");
      server.on("message", (msg: string, rinfo: any) => {
        server.send("pong:" + msg, rinfo.port, rinfo.address);
      });
      server.bind(0, () => {
        const client = createSocket("udp4");
        client.on("message", (msg: string) => {
          console.log(msg);
          client.close();
          server.close();
        });
        client.send("ping", server.address().port, "127.0.0.1");
      });
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim()).toBe("pong:ping");
  });

  it("provides a standalone EventEmitter from events", () => {
    const source = `
      import { EventEmitter, addAbortListener, getEventListeners } from "events";
      const em = new EventEmitter();
      let count = 0;
      let onceCount = 0;
      em.on("tick", () => { count++; });
      em.once("tick", () => { onceCount++; });
      console.log("before", em.listenerCount("tick"));
      em.emit("tick");
      em.emit("tick");
      console.log("after", count, onceCount, em.listenerCount("tick"));
      em.removeAllListeners("tick");
      console.log("names", em.eventNames().length);
      // The constructor is also available as a global.
      const global = new EventEmitter();
      let seen = 0;
      global.on("x", () => { seen++; });
      global.emit("x");
      console.log("global", seen, EventEmitter.listenerCount(global, "x"));
      // addAbortListener registers on the signal's abort listeners.
      const signal: any = {};
      let aborted = 0;
      addAbortListener(signal, () => { aborted++; });
      const abortListeners = getEventListeners(signal, "abort");
      const first: any = abortListeners[0];
      first();
      console.log("abort", aborted);
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual(["before 2", "after 2 1 1", "names 0", "global 1 1", "abort 1"]);
  });

  it("implements util helpers", () => {
    const source = `
      import { format, inspect, isDeepStrictEqual, promisify, isString } from "util";
      function addAsync(a: number, b: number, cb: (e: any, v: number) => void): void { cb(null, a + b); }
      const add = promisify(addAsync);
      add(2, 3).then((v: number) => { console.log("promise", v); });
      console.log(format("%s:%d:%j", "k", 5, { a: 1 }));
      console.log(inspect({ a: 1, b: [2, 3] }));
      console.log(isDeepStrictEqual({ a: [1, 2] }, { a: [1, 2] }), isString("x"), isString(1));
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual([
      'k:5:{"a":1}',
      "{ a: 1, b: [ 2, 3 ] }",
      "true true false",
      "promise 5",
    ]);
  });

  it("implements querystring parse and stringify", () => {
    const source = `
      import { parse, stringify, escape, unescape } from "querystring";
      const q: any = parse("a=1&b=2&b=3&c=hello+world");
      console.log(q.a, JSON.stringify(q.b), q.c);
      console.log(stringify({ x: "a b", y: ["1", "2"] }));
      console.log(escape("a b&c"), unescape("a%20b%26c"));
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual([
      '1 ["2","3"] hello world',
      "x=a%20b&y=1&y=2",
      "a%20b%26c a b&c",
    ]);
  });
});
