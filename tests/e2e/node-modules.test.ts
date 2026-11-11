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

  it("exposes buffer.isUtf8, buffer.constants, os.constants and http.STATUS_CODES", () => {
    const source = `
      import { Buffer, constants as bufferConstants, isUtf8 } from "node:buffer";
      import { constants as osConstants } from "node:os";
      import { STATUS_CODES } from "node:http";
      console.log(isUtf8(Buffer.from("hello")), isUtf8(Buffer.from([0xff, 0xfe])));
      console.log(bufferConstants.MAX_STRING_LENGTH > 0, bufferConstants.MAX_LENGTH > 0);
      console.log(osConstants.signals.SIGINT, osConstants.signals.SIGTERM, osConstants.errno.ENOENT);
      console.log(STATUS_CODES[200], STATUS_CODES[404]);
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual([
      "true false",
      "true true",
      "2 15 2",
      "OK Not Found",
    ]);
  });

  it("computes zlib.crc32 and exposes zlib constants", () => {
    const source = `
      import { crc32, constants, Z_DEFAULT_WINDOWBITS } from "node:zlib";
      console.log(crc32(Buffer.from("hello")));
      console.log(crc32(Buffer.from("The quick brown fox jumps over the lazy dog")));
      console.log(Z_DEFAULT_WINDOWBITS, constants.Z_DEFAULT_WINDOWBITS, constants.Z_FINISH);
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual(["907060870", "1095738169", "15 15 4"]);
  });

  it("parses paths and exposes path.posix / path.win32", () => {
    const source = `
      import { parse, posix, win32 } from "node:path";
      const parts = parse("/home/user/file.txt");
      console.log(parts.root, parts.dir, parts.base, parts.ext, parts.name);
      console.log(posix.join("a", "b"), posix.sep, posix.parse("/x/y.txt").name);
      console.log(win32.normalize("C:/a//b"), win32.sep);
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual([
      "/ /home/user file.txt .txt file",
      "a/b / y",
      "C:/a/b /",
    ]);
  });

  it("provides util.debuglog as a callable no-op logger", () => {
    const source = `
      import { debuglog } from "node:util";
      const log = debuglog("section");
      log("dropped", 1, true);
      console.log(typeof log);
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim()).toBe("function");
  });

  it("provides the deprecated constants module and its object methods", () => {
    const source = `
      import * as constants from "constants";
      console.log(constants.O_WRONLY | constants.O_SYMLINK, constants.hasOwnProperty("O_SYMLINK"));
      console.log(constants.F_OK, constants.hasOwnProperty("nope"));
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual(["4097 true", "0 false"]);
  });

  it("compresses with deflateRawSync via the miniz link feature", () => {
    const source = `
      import { deflateRawSync } from "node:zlib";
      const input = Buffer.from("a".repeat(200));
      const out = deflateRawSync(input);
      console.log(Buffer.isBuffer(out), out.length < input.length);
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim()).toBe("true true");
  });

  it("provides dns.lookup and readline/promises.createInterface", () => {
    const source = `
      import { lookup } from "node:dns";
      import { createInterface } from "node:readline/promises";
      lookup("localhost", (err: unknown, addresses: unknown) => {
        console.log("lookup", (err === null || err === undefined), Array.isArray(addresses));
      });
      const rl = createInterface({ input: process.stdin, output: process.stdout });
      console.log(typeof rl.question, typeof rl.on, typeof rl.off, typeof rl.close);
      rl.close();
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual([
      "lookup true true",
      "function function function function",
    ]);
  });

  it("provides stream helpers and http.IncomingMessage", () => {
    const source = `
      import { Readable, isReadable, isErrored, isDisturbed, finished, pipeline } from "node:stream";
      import { IncomingMessage } from "node:http";
      const readable = new Readable();
      console.log(isReadable(readable), isErrored(readable), isDisturbed(readable));
      finished(readable, (err: unknown) => console.log("finished", err === undefined));
      pipeline(readable, (err: unknown) => console.log("piped", err === undefined));
      console.log(typeof IncomingMessage);
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual([
      "true false false",
      "finished true",
      "piped true",
      "function",
    ]);
  });

  it("exposes timers/promises, perf_hooks, string_decoder, util/types, tty and console", () => {
    const source = `
      import { setTimeout as sleep, setImmediate } from "node:timers/promises";
      import { performance } from "node:perf_hooks";
      import { StringDecoder } from "node:string_decoder";
      import { isProxy, isUint8Array } from "node:util/types";
      import { isatty } from "node:tty";
      import { Console } from "node:console";
      import { createRequire } from "node:module";
      import { channel } from "node:diagnostics_channel";
      async function main() {
        await sleep(1);
        await setImmediate();
        const start = performance.now();
        const decoder = new StringDecoder("utf8");
        console.log(decoder.write(Buffer.from("hello")), start >= 0);
        console.log(isProxy({}), isUint8Array(new Uint8Array(1)), typeof isatty(0));
        const logger = new Console({ stdout: { write() {} } });
        const require = createRequire(import.meta.url);
        const ch = channel("x");
        console.log(typeof require, typeof ch.subscribe, typeof logger.table);
      }
      main();
    `;
    const { status, stdout } = run(source);
    expect(status).toBe(0);
    expect(stdout.trim().split("\n")).toEqual([
      "hello true",
      "false true boolean",
      "function function function",
    ]);
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
